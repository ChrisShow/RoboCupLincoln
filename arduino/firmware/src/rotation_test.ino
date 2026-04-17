/*
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * File:     rotation_test.ino
 * Scopo:    Test rotazione 90° oraria — stessa logica di main_senza_mpu.ino
 *
 *   Ogni pressione BTN_START: ruota 90° in senso orario (destra)
 *   Stesse costanti e stessa logica 2 fasi di main_senza_mpu.ino
 */

#include <LiquidCrystal_I2C.h>

// --- PIN ---
#define PWM_DX   2
#define DX_IN1  22
#define DX_IN2  23
#define PWM_SX   3
#define SX_IN1  24
#define SX_IN2  25
#define BTN_START 43

// --- COSTANTI ROTAZIONE — stesse di main_senza_mpu.ino ---
const int ROT_SPEED  = 185;   // PWM durante rotazione
const int ROT_90_MS  = 550;   // ms per 90° — regola questo
const int ROT_SETTLE = 100;   // pausa post-rotazione

LiquidCrystal_I2C lcd(0x27, 16, 2);

int rotCount = 0;

// ----------------------------------------------------------------
void setMotors(int l, int r) {
  digitalWrite(SX_IN1, l < 0); digitalWrite(SX_IN2, l >= 0);
  digitalWrite(DX_IN1, r < 0); digitalWrite(DX_IN2, r >= 0);
  analogWrite(PWM_SX, abs(l));
  analogWrite(PWM_DX, abs(r));
}

void stopMotors() { setMotors(0, 0); }

// ----------------------------------------------------------------
// Rotazione 90° oraria — 2 fasi: 80% piena + 20% dimezzata
// Identica a rotate(-90) in main_senza_mpu.ino
// ----------------------------------------------------------------
void rotateCW90() {
  lcd.clear();
  lcd.print("ROTAZIONE DX");
  lcd.setCursor(0, 1);
  lcd.print("90deg oraria");
  Serial.println("Rotazione 90° oraria — inizio");

  // Fase 1 (80%): velocità piena
  setMotors( ROT_SPEED,     -ROT_SPEED);
  delay(ROT_90_MS * 4 / 5);

  // Fase 2 (20%): velocità dimezzata — smaltisce inerzia
  setMotors( ROT_SPEED / 2, -ROT_SPEED / 2);
  delay(ROT_90_MS / 5);

  stopMotors();
  delay(ROT_SETTLE);

  rotCount++;
  lcd.clear();
  lcd.print("OK  #" + String(rotCount));
  lcd.setCursor(0, 1);
  lcd.print("Premi per +90deg");
  Serial.println("Rotazione OK — totale: " + String(rotCount) + " x 90deg");
}

// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(DX_IN1, OUTPUT); pinMode(DX_IN2, OUTPUT);
  pinMode(SX_IN1, OUTPUT); pinMode(SX_IN2, OUTPUT);

  lcd.init(); lcd.backlight();
  lcd.print("ROTATION TEST");
  lcd.setCursor(0, 1); lcd.print("Premi START");
  Serial.println("Rotation test pronto. Premi BTN_START per 90deg CW.");
}

// ----------------------------------------------------------------
void loop() {
  if (digitalRead(BTN_START) == LOW) {
    delay(50);                               // debounce
    if (digitalRead(BTN_START) == LOW) {
      rotateCW90();
      while (digitalRead(BTN_START) == LOW); // aspetta rilascio
      delay(100);
    }
  }
}
