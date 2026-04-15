/* 
 * Progetto: mENIAC 2026 - Tool di Diagnostica
 * Descrizione: Verifica hardware completa e test comunicazione seriale.
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <MPU6050_light.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// --- PIN DEFINITIONS ---
#define XSHUT_FRONT 30
#define XSHUT_BACK  31
#define XSHUT_DX_ANT 32
#define XSHUT_DX_POST 33
#define XSHUT_SX_ANT 34
#define XSHUT_SX_POST 35

#define PWM_DX 2
#define DX_IN1 22
#define DX_IN2 23
#define PWM_SX 3
#define SX_IN1 24
#define SX_IN2 25

#define SERVO_PIN 4
#define BTN_START 43
#define LED_STATUS 45

// --- OGGETTI ---
VL53L0X sensors[6];
const char* sensorNames[] = {"Front", "Back", "DxAnt", "DxPost", "SxAnt", "SxPost"};
int xshutPins[] = {30, 31, 32, 33, 34, 35};

MPU6050 mpu(Wire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo medikitServo;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("DIAGNOSTIC START");
  delay(2000);

  // 1. Test Giroscopio
  lcd.clear(); lcd.print("1. Testing IMU...");
  byte status = mpu.begin();
  if (status != 0) {
    lcd.setCursor(0,1); lcd.print("IMU ERROR: "); lcd.print(status);
    while(1);
  }
  lcd.setCursor(0,1); lcd.print("IMU OK");
  delay(1000);

  // 2. Test Sensori ToF
  lcd.clear(); lcd.print("2. Testing ToFs");
  for(int i=0; i<6; i++) {
    pinMode(xshutPins[i], OUTPUT);
    digitalWrite(xshutPins[i], LOW);
  }
  delay(100);
  
  for(int i=0; i<6; i++) {
    digitalWrite(xshutPins[i], HIGH);
    delay(100);
    
    sensors[i].setTimeout(500); // Imposta timeout di 500ms per evitare blocchi
    
    if (!sensors[i].init()) {
      lcd.clear();
      lcd.print("ToF ERR: "); lcd.print(sensorNames[i]);
      lcd.setCursor(0,1); lcd.print("Check XSHUT/I2C");
      Serial.print("ERRORE INIZIALIZZAZIONE SENSORE: "); Serial.println(sensorNames[i]);
      while(1);
    }
    
    sensors[i].setAddress(0x30 + i);
    lcd.setCursor(0,1); lcd.print(sensorNames[i]); lcd.print(" OK (0x"); lcd.print(0x30+i, HEX); lcd.print(")");
    delay(500);
  }

  // 3. Test Motori (LOGICA INVERTITA)
  lcd.clear(); lcd.print("3. Testing Motors");
  lcd.setCursor(0,1); lcd.print("Forward...");
  // Per andare avanti ora usiamo LOW/HIGH invece di HIGH/LOW
  analogWrite(PWM_DX, 100); digitalWrite(DX_IN1, LOW); digitalWrite(DX_IN2, HIGH);
  analogWrite(PWM_SX, 100); digitalWrite(SX_IN1, LOW); digitalWrite(SX_IN2, HIGH);
  delay(1000);
  
  lcd.setCursor(0,1); lcd.print("Backward...");
  digitalWrite(DX_IN1, HIGH); digitalWrite(DX_IN2, LOW);
  digitalWrite(SX_IN1, HIGH); digitalWrite(SX_IN2, LOW);
  delay(1000);
  // Stop
  digitalWrite(DX_IN1, HIGH); digitalWrite(DX_IN2, HIGH);
  digitalWrite(SX_IN1, HIGH); digitalWrite(SX_IN2, HIGH);
  lcd.setCursor(0,1); lcd.print("Motors Stopped");
  delay(1000);

  // 4. Test Servo
  lcd.clear(); lcd.print("4. Testing Servo");
  medikitServo.attach(SERVO_PIN);
  lcd.setCursor(0,1); lcd.print("Pos: 0"); medikitServo.write(0); delay(1000);
  lcd.setCursor(0,1); lcd.print("Pos: 180"); medikitServo.write(180); delay(1000);
  lcd.setCursor(0,1); lcd.print("Pos: 90"); medikitServo.write(90); delay(1000);

  // 5. Test Comunicazione con RPi (Timeout 60s)
  lcd.clear(); lcd.print("5. Wait RPi (60s)");
  long startWait = millis();
  bool rpiDetected = false;
  
  while(millis() - startWait < 60000) {
    Serial.println("<DIAG:PING>");
    if (Serial.available() > 0) {
      String msg = Serial.readStringUntil('>');
      if (msg.indexOf("PONG") != -1) {
        rpiDetected = true;
        break;
      }
    }
    lcd.setCursor(0,1); lcd.print("Wait: "); lcd.print((60000 - (millis() - startWait))/1000); lcd.print("s  ");
    delay(500);
  }

  lcd.clear();
  if (rpiDetected) {
    lcd.print("SYSTEM READY!");
    lcd.setCursor(0,1); lcd.print("Comm: OK");
    digitalWrite(LED_STATUS, HIGH);
  } else {
    lcd.print("COMM TIMEOUT");
    lcd.setCursor(0,1); lcd.print("Check RPi USB");
  }
}

void loop() {
  // Diagnostica continua sensori in loop dopo il test iniziale
  lcd.setCursor(0,0);
  lcd.print("F:"); lcd.print(sensors[0].readRangeContinuousMillimeters()/10);
  lcd.print(" B:"); lcd.print(sensors[1].readRangeContinuousMillimeters()/10);
  lcd.print("  ");
  delay(100);
}
