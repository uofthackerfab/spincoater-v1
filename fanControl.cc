#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int FAN_PWM_PIN = 9;
const int POT_COARSE_PIN = A0;
const int POT_FINE_PIN = A1;

const int FAN_MAX_RPM = 2000;

void setup() {
  Serial.begin(9600);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Fan Controller");

  pinMode(FAN_PWM_PIN, OUTPUT);
  pinMode(POT_COARSE_PIN, INPUT);
  pinMode(POT_FINE_PIN, INPUT);

  delay(1000);
  lcd.clear();
}

void loop() {
  int coarseRaw = analogRead(POT_COARSE_PIN);
  int fineRaw = analogRead(POT_FINE_PIN);

  int coarsePWM = map(coarseRaw, 0, 1023, 255, 0);
  int fineTrim = map(fineRaw, 0, 1023, 30, -30);

  int pwmOut = constrain(coarsePWM + fineTrim, 0, 255);

  analogWrite(FAN_PWM_PIN, pwmOut);

  int estRPM = map(pwmOut, 0, 255, 0, FAN_MAX_RPM);
  int dutyPercent = (pwmOut * 100) / 255;

  Serial.print("Coarse Raw: ");
  Serial.print(coarseRaw);
  Serial.print("  Fine Raw: ");
  Serial.println(fineRaw);

  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print("PWM:");
  lcd.print(pwmOut);
  lcd.print("  ");
  lcd.print(dutyPercent);
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print("RPM:");
  lcd.print(estRPM);

  delay(1500);
}