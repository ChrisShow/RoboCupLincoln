/*
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * File:     servo_test.ino
 * Scopo:    Test meccanismo medikit SG90
 *
 *   Avvio  : servo portato a 0° direttamente (nessuno sweep), poi detach
 *   Pulsante: ogni pressione avanza il servo di 40° in senso orario (lentamente)
 *             Il servo è attivo SOLO durante il movimento, poi si stacca
 */

#include <Servo.h>
#include <LiquidCrystal_I2C.h>

#define SERVO_PIN   4
#define BTN_START  43

const int SERVO_STEP    = 40;
const int SERVO_SLOW_MS = 12;  // ms per grado — 40° = 480ms totali

Servo             medikitServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

int servoAngle = 0;

// ----------------------------------------------------------------
void servoSlowMove(int target) {
  target = constrain(target, 0, 180);
  while (servoAngle != target) {
    servoAngle += (target > servoAngle) ? 1 : -1;
    medikitServo.write(servoAngle);
    delay(SERVO_SLOW_MS);
  }
}

// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_START, INPUT_PULLUP);

  lcd.init(); lcd.backlight();
  lcd.clear(); lcd.print("SERVO TEST");
  lcd.setCursor(0, 1); lcd.print("Init...");

  // Posiziona a 0° senza sweep, poi stacca il PWM
  medikitServo.attach(SERVO_PIN);
  medikitServo.write(0);
  servoAngle = 0;
  delay(600);
  medikitServo.detach();

  lcd.clear(); lcd.print("PRONTO a 0deg");
  lcd.setCursor(0, 1); lcd.print("Premi START");
  Serial.println("Pronto. Premi BTN_START per +40deg.");
}

// ----------------------------------------------------------------
void loop() {
  if (digitalRead(BTN_START) == LOW) {
    delay(50);                                   // debounce
    if (digitalRead(BTN_START) == LOW) {

      int nextAngle = servoAngle + SERVO_STEP;
      if (nextAngle > 180) nextAngle = 180;      // clamp — non supera 180°

      lcd.clear(); lcd.print("GIRO +40deg");
      lcd.setCursor(0, 1);
      lcd.print(String(servoAngle) + " -> " + String(nextAngle) + "deg");

      medikitServo.attach(SERVO_PIN);
      delay(50);                                 // stabilizzazione post-attach
      servoSlowMove(nextAngle);
      delay(200);                                // mantieni posizione
      medikitServo.detach();                     // stop PWM

      lcd.clear(); lcd.print("OK " + String(servoAngle) + "deg");
      lcd.setCursor(0, 1); lcd.print("Premi per +40deg");
      Serial.println("Servo a " + String(servoAngle) + " gradi");

      while (digitalRead(BTN_START) == LOW);    // aspetta rilascio
      delay(100);
    }
  }
}
