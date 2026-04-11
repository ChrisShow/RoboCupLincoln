/* 
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * Descrizione: Codice principale Arduino Mega 2560 (Slave/Riflessi)
 * Architettura: Arduino Mega 2560 (Slave) + Raspberry Pi 4B (Master)
 * 
 * Basato su: schema_funzionamento_algoritmo.md e collegamenti.md
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <MPU6050_light.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// --- PIN DEFINITIONS (da collegamenti.md) ---
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

#define TCS_S0 38
#define TCS_S1 39
#define TCS_S2 40
#define TCS_S3 41
#define TCS_OUT 42

#define SERVO_PIN 4
#define BTN_START 43
#define LED_STATUS 45

// --- COSTANTI ALGORITMO (da schema_funzionamento_algoritmo.md) ---
const float SOGLIA_MURO_FRONT = 12.0; // cm
const float SOGLIA_MURO_LAT = 15.0;   // cm
const float TARGET_LAT_DIST = 10.0;   // cm (centratura)
const float TARGET_FRONT_DIST = 6.5;  // cm (allineamento frontale)

// --- OGGETTI E VARIABILI GLOBALI ---
VL53L0X sensors[6]; // 0:Front, 1:Back, 2:DxAnt, 3:DxPost, 4:SxAnt, 5:SxPost
int xshutPins[] = {XSHUT_FRONT, XSHUT_BACK, XSHUT_DX_ANT, XSHUT_DX_POST, XSHUT_SX_ANT, XSHUT_SX_POST};

MPU6050 mpu(Wire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo medikitServo;

float initialYaw = 0;
bool isRunning = false;
unsigned long lastMoveTime = 0;
int moveCounter = 0;

// --- PROTOTIPI FUNZIONI ---
void scanSensors();
void rotate(float degrees);
void moveForward();
void handleRescue(char pos, int type);
void sendToPi(String msg);
int readColor();

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // Inizializzazione LCD
  lcd.init();
  lcd.backlight();
  lcd.print("mENIAC 2026");
  
  // Inizializzazione Pin
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT);
  pinMode(DX_IN1, OUTPUT); pinMode(DX_IN2, OUTPUT);
  pinMode(SX_IN1, OUTPUT); pinMode(SX_IN2, OUTPUT);
  
  // Inizializzazione Sensore Colore
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW); // Scaling 20%

  // Inizializzazione MPU6050
  byte status = mpu.begin();
  lcd.setCursor(0,1);
  if (status != 0) lcd.print("MPU ERR"); else lcd.print("MPU OK");
  delay(1000);
  mpu.calcOffsets(); // TODO: Calibrazione dinamica o caricamento offset salvati
  
  // Inizializzazione ToF
  for(int i=0; i<6; i++) {
    pinMode(xshutPins[i], OUTPUT);
    digitalWrite(xshutPins[i], LOW);
  }
  delay(50);
  for(int i=0; i<6; i++) {
    digitalWrite(xshutPins[i], HIGH);
    delay(50);
    if (sensors[i].init()) {
      sensors[i].setAddress(0x30 + i);
      sensors[i].startContinuous();
    }
  }

  medikitServo.attach(SERVO_PIN);
  medikitServo.write(90); // TODO: Verificare angolo di chiusura corretto

  lcd.clear();
  lcd.print("READY. PRESS START");
}

void loop() {
  mpu.update();

  // Gestione Pulsante START / LOP (Lack of Progress)
  if (digitalRead(BTN_START) == LOW) {
    delay(200); // Debounce
    if (!isRunning) {
      isRunning = true;
      sendToPi("<INIT>");
      lcd.clear(); lcd.print("RUNNING...");
    } else {
      // Lack of Progress
      isRunning = false;
      stopMotors();
      sendToPi("<LOP>");
      lcd.clear(); lcd.print("LACK OF PROGRESS");
    }
    while(digitalRead(BTN_START) == LOW); // Attesa rilascio
  }

  if (isRunning) {
    checkSerial();
  }
}

// --- LOGICA DI COMUNICAZIONE ---
void checkSerial() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('>');
    if (cmd.startsWith("<")) cmd = cmd.substring(1); else return;
    
    // TODO: Implementare parser robusto per i comandi
    if (cmd == "SCAN:FRONT") {
      rotate(-90); // 90° Antiorario (schema punto 5)
      sendToPi("<SCAN:OK>");
    } else if (cmd == "SCAN:DONE") {
      rotate(90);  // Ritorno a 0° (schema punto 7)
    } else if (cmd.startsWith("VIC:")) {
      // Formato <VIC:POS,TIPO>
      char pos = cmd.charAt(4);
      int type = cmd.substring(6).toInt();
      handleRescue(pos, type);
    } else if (cmd == "VIC:DONE") {
      // Fine sequenza vittime
    } else if (cmd.startsWith("GO:")) {
      int dir = cmd.substring(3).toInt();
      // 0=Dritto, 1=Destra, 2=Dietro, 3=Sinistra
      handleMovement(dir);
    }
  }
}

void sendToPi(String msg) {
  Serial.print(msg);
  Serial.flush();
}

// --- LOGICA SENSORI ---
void scanSensors() {
  float f = sensors[0].readRangeContinuousMillimeters() / 10.0;
  float r = (sensors[2].readRangeContinuousMillimeters() + sensors[3].readRangeContinuousMillimeters()) / 20.0;
  float l = (sensors[4].readRangeContinuousMillimeters() + sensors[5].readRangeContinuousMillimeters()) / 20.0;
  int color = readColor();
  
  int F = (f <= SOGLIA_MURO_FRONT) ? 1 : 0;
  int R = (r <= SOGLIA_MURO_LAT) ? 1 : 0;
  int L = (l <= SOGLIA_MURO_LAT) ? 1 : 0;
  
  String data = "<DATA:" + String(color) + "," + String(F) + String(R) + String(L) + ">";
  sendToPi(data);
}

int readColor() {
  // TODO: Implementare logica TCS3200 per distinguere 0=Normale, 1=Grigia, 2=Rossa, 3=Blu, 4=Nera
  // Al momento restituisce 0 (Normale)
  return 0; 
}

// --- LOGICA MOVIMENTO ---
void rotate(float degrees) {
  float targetYaw = mpu.getAngleZ() + degrees;
  // TODO: Implementare loop PID per rotazione precisa con tolleranza (es. +- 1 grado)
  // Esempio semplificato:
  while(abs(mpu.getAngleZ() - targetYaw) > 2) {
    mpu.update();
    if (mpu.getAngleZ() < targetYaw) {
      setMotors(-100, 100); // Ruota DX
    } else {
      setMotors(100, -100); // Ruota SX
    }
  }
  stopMotors();
}

void handleMovement(int dir) {
  // Ruota verso la direzione desiderata prima di avanzare
  if (dir == 1) rotate(90);
  else if (dir == 2) rotate(180);
  else if (dir == 3) rotate(-90);
  
  moveForward();
}

void moveForward() {
  moveCounter = 0;
  lastMoveTime = millis();
  
  float startFront = sensors[0].readRangeContinuousMillimeters() / 10.0;
  float startBack = sensors[1].readRangeContinuousMillimeters() / 10.0;
  bool useFront = (startFront < 45.0);
  bool useBack = (startBack < 10.0);

  // TODO: Implementare FASE A, B (Allineamento e Centratura) nel loop
  
  while(true) {
    mpu.update();
    if (millis() - lastMoveTime >= 20) {
      lastMoveTime = millis();
      moveCounter++;
      
      // Controllo Trappola Nera (Fase C)
      if (readColor() == 4) { // 4 = NERO
        handleBlackTile();
        return;
      }

      // Fase D: Condizione di Arrivo a 30 cm
      float currentFront = sensors[0].readRangeContinuousMillimeters() / 10.0;
      float currentBack = sensors[1].readRangeContinuousMillimeters() / 10.0;

      if (useFront) {
        if (currentFront <= TARGET_FRONT_DIST) break;
      } else if (useBack) {
        if (currentBack >= (startBack + 30.0)) break;
      } else {
        // Corridoio vuoto: muoviti per tempo stimato (es. 1500ms per 30cm a velocità 150)
        // TODO: Calibrare tempo esatto in base alla velocità reale
        if (moveCounter >= 75) break; // 75 * 20ms = 1500ms
      }
      
      setMotors(150, 150); 
    }
  }
  
  stopMotors();
  sendToPi("<MOVE:OK>");
  scanSensors(); 
}

void handleBlackTile() {
  // Inverte i motori per tornare indietro (Schema 11.C)
  unsigned long backTime = moveCounter * 20;
  setMotors(-150, -150);
  delay(backTime);
  stopMotors();
  sendToPi("<MOVE:BLACK>");
}

// --- LOGICA SOCCORSO ---
void handleRescue(char pos, int type) {
  if (type == 0) return;

  // Segnalazione LED 5s (Schema 9)
  digitalWrite(LED_STATUS, HIGH);
  unsigned long startBlink = millis();
  while(millis() - startBlink < 5000) {
    digitalWrite(LED_STATUS, (millis() / 500) % 2);
  }
  digitalWrite(LED_STATUS, LOW);

  if (type == 1) { // Unharmed: basta segnalazione (Schema 9)
    // Opzionale: invio diretto a fine rescue
  } else {
    // Manovra Allineamento Posteriore
    if (pos == 'R') rotate(-90);
    else if (pos == 'L') rotate(90);
    else if (pos == 'F') rotate(180);

    // Erogazione Medikit
    for(int i=0; i<type; i++) {
      medikitServo.write(0); // APERTO
      delay(1000);
      medikitServo.write(90); // CHIUSO
      delay(500);
    }

    // Ripristino
    if (pos == 'R') rotate(90);
    else if (pos == 'L') rotate(-90);
    else if (pos == 'F') rotate(180);
  }
  
  sendToPi("<VIC:DONE>");
}

// --- HELPER MOTORI ---
void setMotors(int left, int right) {
  if (left >= 0) {
    digitalWrite(SX_IN1, HIGH); digitalWrite(SX_IN2, LOW);
  } else {
    digitalWrite(SX_IN1, LOW); digitalWrite(SX_IN2, HIGH);
  }
  if (right >= 0) {
    digitalWrite(DX_IN1, HIGH); digitalWrite(DX_IN2, LOW);
  } else {
    digitalWrite(DX_IN1, LOW); digitalWrite(DX_IN2, HIGH);
  }
  analogWrite(PWM_SX, abs(left));
  analogWrite(PWM_DX, abs(right));
}

void stopMotors() {
  digitalWrite(DX_IN1, HIGH); digitalWrite(DX_IN2, HIGH);
  digitalWrite(SX_IN1, HIGH); digitalWrite(SX_IN2, HIGH);
  analogWrite(PWM_DX, 0);
  analogWrite(PWM_SX, 0);
}
