/*
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * File:     servo_test.ino
 * Scopo:    Test meccanismo medikit SG90
 *
 *   Avvio  : servo portato lentamente a 0° (da posizione neutra 90°)
 *   Pulsante: ogni pressione avanza il servo di 40° nella stessa direzione
 *             Superati i 170°: ripristino lento a 0° poi riprende da 40°
 *
 *   Pin e costanti identici a main_senza_mpu.ino
 */

#include <Servo.h>
#include <LiquidCrystal_I2C.h>

#define SERVO_PIN   4
#define BTN_START  43

const int SERVO_STEP     = 40;
const int SERVO_SLOW_MS  = 12;  // ms per grado — erogazione (40° = 480ms)
const int SERVO_RESET_MS =  6;  // ms per grado — reset a 0 (160°→0 ≈ 960ms)

Servo             medikitServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

int servoAngle = 0;

// ----------------------------------------------------------------
void servoSlowMove(int target, int msPerDeg = SERVO_SLOW_MS) {
  target = constrain(target, 0, 180);
  while (servoAngle != target) {
    servoAngle += (target > servoAngle) ? 1 : -1;
    medikitServo.write(servoAngle);
    delay(msPerDeg);
  }
}

// ----------------------------------------------------------------
void dispenseKit() {
  int nextAngle = servoAngle + SERVO_STEP;

  if (nextAngle > 170) {
    lcd.clear(); lcd.print("RESET  0deg");
    lcd.setCursor(0, 1); lcd.print(String(servoAngle) + "deg -> 0deg");
    servoSlowMove(0, SERVO_RESET_MS);
    nextAngle = SERVO_STEP;
  }

  lcd.clear(); lcd.print("EROGAZIONE");
  lcd.setCursor(0, 1);
  lcd.print(String(servoAngle) + ">" + String(nextAngle) + "deg");
  servoSlowMove(nextAngle);

  lcd.clear(); lcd.print("OK " + String(servoAngle) + "deg");
  lcd.setCursor(0, 1); lcd.print("Premi per next");
}

// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_START, INPUT_PULLUP);

  lcd.init(); lcd.backlight();
  lcd.print("SERVO TEST");
  lcd.setCursor(0, 1); lcd.print("Init...");

  medikitServo.attach(SERVO_PIN);
  medikitServo.write(90);
  servoAngle = 90;
  delay(400);

  lcd.clear(); lcd.print("INIT: 90->0deg");
  servoSlowMove(0);   // porta lentamente a 0 (≈ 1.1s)

  lcd.clear(); lcd.print("PRONTO");
  lcd.setCursor(0, 1); lcd.print("Premi START");
  Serial.println("Servo test pronto. Premi BTN_START per erogare.");
}

// ----------------------------------------------------------------
void loop() {
  if (digitalRead(BTN_START) == LOW) {
    delay(50);                          // debounce
    if (digitalRead(BTN_START) == LOW) {
      dispenseKit();
      Serial.println("Servo ora a " + String(servoAngle) + " gradi");
      while (digitalRead(BTN_START) == LOW);  // aspetta rilascio
      delay(100);
    }
  }
}
