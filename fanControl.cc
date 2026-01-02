/*
  Spin coater controller

  Hardware
  - Pot (coarse) wiper -> A0
  - Pot (fine)   wiper -> A1
  - Fan PWM -> D9 (must be a PWM pin)

  - I2C LCD:
    SDA -> A4
    SCL -> A5
    NOTE: Your message said A4/A5 are SDA/SCL respectively. On Arduino UNO/Nano:
    A4 = SDA, A5 = SCL.

  - 4x4 membrane keypad:
    R1..R4 and C1..C4 -> D0..D7 (in that order)
    NOTE: D0/D1 are also Serial RX/TX on UNO/Nano. If you use D0/D1 for keypad,
    avoid Serial and disconnect keypad when uploading if uploads act weird.

  Behavior
  - Coarse + Fine pots combine into a single PWM output (0..255).
  - Keypad enters job duration (seconds).
  - LCD shows speed (PWM + %) and duration (set + remaining while running).

  Keypad controls
  - Digits 0-9: enter duration (seconds)
  - * : clear duration
  - # : start job
  - D : stop job (abort) while running
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>


LiquidCrystal_I2C lcd(0x27, 16, 2);

/* Pins */
const int potCoarsePin = A0;
const int potFinePin   = A1;
const int fanPwmPin    = 9;

/* Keypad wiring: R1..R4 then C1..C4 mapped to D0..D7 */
const byte rows = 4;
const byte cols = 4;

/* Keypad layout (standard 4x4) */
char keys[rows][cols] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

/* Row pins: R1..R4 -> D0..D3 */
byte rowPins[rows] = {0, 1, 2, 3};
/* Col pins: C1..C4 -> D4..D7 */
byte colPins[cols] = {4, 5, 6, 7};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, rows, cols);
/* RPM estimate calibration
   Fill these with measurements from your build.
   Use a phone tachometer app or a cheap laser tach.
*/
const int CAL_N = 6;

const int pwmCal[CAL_N] = {  0,  60, 100, 140, 180, 220 };
const int rpmCal[CAL_N] = {  0, 800,1500,2200,2900,3500 };

/* Duration input */
unsigned long durationSeconds = 0;
/* Running state */
bool isRunning = false;
/* Job timing */
unsigned long jobStartMs = 0;
/* Latched duration for the run */
unsigned long jobDurationSeconds = 0;

/* UI refresh timing */
unsigned long lastUiMs = 0;

/* Helpers */
int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

/*
  Coarse/fine mapping strategy:
  - Coarse sets a base PWM in steps of 16 (0..240)
  - Fine adds 0..15
  => total is 0..255 with stable coarse adjustment + precise fine trim
*/
int readPwmFromPots() {
  /* Read coarse pot */
  int coarseRaw = analogRead(potCoarsePin);
  /* Read fine pot */
  int fineRaw = analogRead(potFinePin);

  /* Map coarse to 0..15 steps */
  int coarseStep = map(coarseRaw, 0, 1023, 0, 15);
  /* Map fine to 0..15 */
  int fineStep = map(fineRaw, 0, 1023, 0, 15);

  /* Base PWM is coarse * 16 */
  int basePwm = coarseStep * 16;
  /* Total PWM adds fine */
  int pwm = basePwm + fineStep;

  /* Clamp just in case */
  pwm = clampInt(pwm, 0, 255);

  return pwm;
}

void writeFanPwm(int pwm) {
  /* Write PWM to fan */
  analogWrite(fanPwmPin, pwm);
}

void clearDuration() {
  /* Reset entered duration */
  durationSeconds = 0;
}

void startJob() {
  /* Ignore if duration is zero */
  if (durationSeconds == 0) return;

  /* Latch duration */
  jobDurationSeconds = durationSeconds;
  /* Start timer */
  jobStartMs = millis();
  /* Set running */
  isRunning = true;
}

void stopJob() {
  /* Stop running */
  isRunning = false;
  /* Clear latched duration */
  jobDurationSeconds = 0;
}

unsigned long getRemainingSeconds() {
  /* If not running, remaining is 0 */
  if (!isRunning) return 0;

  /* Elapsed milliseconds */
  unsigned long elapsedMs = millis() - jobStartMs;
  /* Elapsed seconds */
  unsigned long elapsedSec = elapsedMs / 1000UL;

  /* If elapsed passed duration, remaining is 0 */
  if (elapsedSec >= jobDurationSeconds) return 0;

  return (jobDurationSeconds - elapsedSec);
}
int estimateRpmFromPwm(int pwm) {
  if (pwm <= pwmCal[0]) return rpmCal[0];
  if (pwm >= pwmCal[CAL_N - 1]) return rpmCal[CAL_N - 1];

  for (int i = 0; i < CAL_N - 1; i++) {
    int x0 = pwmCal[i];
    int x1 = pwmCal[i + 1];

    if (pwm >= x0 && pwm <= x1) {
      int y0 = rpmCal[i];
      int y1 = rpmCal[i + 1];

      long num = (long)(pwm - x0) * (long)(y1 - y0);
      long den = (long)(x1 - x0);

      return (int)(y0 + (num / den));
    }
  }

  return 0;
}
void updateLcd(int pwm, unsigned long remainingSec) {
  int percent = (pwm * 100) / 255;
  int rpmEst = estimateRpmFromPwm(pwm);

  /* Line 1: speed */
  lcd.setCursor(0, 0);
  lcd.print("SPD ");

  if (percent < 100) lcd.print(" ");
  if (percent < 10)  lcd.print(" ");
  lcd.print(percent);
  lcd.print("% ");

  /* RPM field */
  if (rpmEst < 1000) lcd.print(" ");
  if (rpmEst < 100)  lcd.print(" ");
  if (rpmEst < 10)   lcd.print(" ");
  lcd.print(rpmEst);
  lcd.print("R");

  /* Line 2: duration */
  lcd.setCursor(0, 1);
  if (!isRunning) {
    lcd.print("T ");
    lcd.print(durationSeconds);
    lcd.print("s");
    lcd.print("           ");
  } else {
    lcd.print("RUN ");
    lcd.print(remainingSec);
    lcd.print("s left");
    lcd.print("       ");
  }
}


void handleKeypad() {
  /* Get key press (non-blocking) */
  char key = keypad.getKey();
  if (!key) return;

  /* While running: allow abort */
  if (isRunning) {
    /* D aborts */
    if (key == 'D') {
      stopJob();
    }
    return;
  }

  /* Not running: edit duration */
  if (key >= '0' && key <= '9') {
    /* Build a seconds number, limit digits to avoid overflow */
    if (durationSeconds <= 99999UL) {
      durationSeconds = (durationSeconds * 10UL) + (unsigned long)(key - '0');
    }
  } else if (key == '*') {
    /* Clear */
    clearDuration();
  } else if (key == '#') {
    /* Start */
    startJob();
  } else {
    /* Ignore A/B/C/D when not running */
  }
}

void setup() {
  /* Fan PWM pin output */
  pinMode(fanPwmPin, OUTPUT);

  /* Start I2C */
  Wire.begin();

  /* Init LCD */
  lcd.init();
  lcd.backlight();

  /* Initial screen */
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Spin Coater");
  lcd.setCursor(0, 1);
  lcd.print("Ready");
  delay(700);
  lcd.clear();

  /* Start fan off */
  writeFanPwm(0);
}

void loop() {
  /* Read speed always so you can “set” it before running */
  int pwm = readPwmFromPots();

  /* Handle keypad input */
  handleKeypad();

  /* If running, check countdown */
  if (isRunning) {
    unsigned long remainingSec = getRemainingSeconds();

    /* If done, stop */
    if (remainingSec == 0) {
      stopJob();
    }
  }

  /* Apply PWM only while running */
  if (isRunning) {
    writeFanPwm(pwm);
  } else {
    writeFanPwm(0);
  }

  /* Update LCD at ~10 Hz */
  unsigned long nowMs = millis();
  if (nowMs - lastUiMs >= 100UL) {
    lastUiMs = nowMs;

    unsigned long remainingSec = getRemainingSeconds();
    updateLcd(pwm, remainingSec);
  }
}
