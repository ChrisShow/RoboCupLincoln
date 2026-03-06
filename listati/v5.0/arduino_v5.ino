/*
 * RoboCup Rescue Maze 2026 - Arduino MEGA 2560
 * VERSIONE V5.1 - 4 Motori DC Mecanum con Versi Corretti
 * 
 * NOVITÀ V5.1:
 * - Versi motori corretti per configurazione Mecanum speculare
 * - FL: IN1=HIGH/IN2=LOW per avanti
 * - FR/BL/BR: IN=LOW/HIGH per avanti (montaggio speculare)
 * 
 * Configurazione Hardware:
 * - 4 motori DC Mecanum (configurazione X)
 * - Driver A: ruote anteriori (FL + FR)
 * - Driver B: ruote posteriori (BL + BR)
 * - 3x VL53L0X (sensori distanza)
 * - 1x TCS3200 (sensore colore)
 * - 1x MPU6050 (giroscopio/accelerometro)
 * - 2x Servo (camera + medikit)
 * - Comunicazione UART con Raspberry Pi
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <Servo.h>
#include <MPU6050.h>
#include <avr/wdt.h>

// ========== MOTORI - Driver A (Anteriori) ==========
#define MOTOR_FL_PWM 6        // Front Left - Velocità
#define MOTOR_FL_IN1 22       // Front Left - IN1
#define MOTOR_FL_IN2 23       // Front Left - IN2

#define MOTOR_FR_PWM 7        // Front Right - Velocità
#define MOTOR_FR_IN3 24       // Front Right - IN3
#define MOTOR_FR_IN4 25       // Front Right - IN4

// ========== MOTORI - Driver B (Posteriori) ==========
#define MOTOR_BL_PWM 8        // Back Left - Velocità
#define MOTOR_BL_IN1 26       // Back Left - IN1
#define MOTOR_BL_IN2 27       // Back Left - IN2

#define MOTOR_BR_PWM 9        // Back Right - Velocità
#define MOTOR_BR_IN3 28       // Back Right - IN3
#define MOTOR_BR_IN4 29       // Back Right - IN4

// Velocità motori (0-255)
#define MOTOR_SPEED_NORMAL 150
#define MOTOR_SPEED_SLOW 100
#define MOTOR_SPEED_TURN 120

// Coefficienti calibrazione motori (per compensare derive)
float motorFL_coefficient = 1.00;  // Front Left
float motorFR_coefficient = 1.00;  // Front Right
float motorBL_coefficient = 1.00;  // Back Left
float motorBR_coefficient = 1.00;  // Back Right

// ========== SENSORI DISTANZA VL53L0X ==========
#define VL53L0X_FRONT_ADDR 0x30
#define VL53L0X_RIGHT_ADDR 0x31
#define VL53L0X_LEFT_ADDR 0x32

VL53L0X sensorFront;
VL53L0X sensorRight;
VL53L0X sensorLeft;

#define WALL_THRESHOLD 150  // mm - distanza muro

// ========== SENSORE COLORE TCS3200 ==========
#define TCS3200_S0 30
#define TCS3200_S1 31
#define TCS3200_S2 32
#define TCS3200_S3 33
#define TCS3200_OUT 34

// ========== SENSORE INCLINAZIONE MPU6050 ==========
MPU6050 mpu;
#define MAX_SLOPE_ANGLE 17.0  // gradi

// ========== SERVO ==========
#define SERVO_CAMERA_PIN 10
#define SERVO_MEDIKIT_PIN 11

Servo servoCamera;
Servo servoMediakit;

#define CAMERA_CENTER 90
#define CAMERA_LEFT 0
#define CAMERA_RIGHT 180

#define MEDIKIT_CLOSED 0
#define MEDIKIT_OPEN 90

// ========== LED STATUS ==========
#define LED_STATUS 13
#define LED_BLINK_DURATION 5000   // 5 secondi
#define LED_BLINK_INTERVAL 500    // 500ms ON/OFF

// ========== COMUNICAZIONE RASPBERRY PI ==========
#define SERIAL_RASPBERRY Serial1  // TX1 18, RX1 19
#define BAUD_RATE 9600

// Codici comunicazione
#define CODE_NESSUNA 0
#define CODE_HARMED 1
#define CODE_STABLE 2
#define CODE_UNHARMED 3
#define CODE_FALSO_POSITIVO 4
#define CODE_BLUE_TILE 5

// ========== STATE MACHINE ==========
enum RobotState {
  CALIBRATION,
  SCAN_FRONT,
  SCAN_RIGHT,
  SCAN_LEFT,
  WAIT_DIRECTION,
  MOVE,
  WAIT_CONFIRM,
  EMERGENCY_STOP
};

RobotState currentState = CALIBRATION;

// ========== WATCHDOG & HEARTBEAT ==========
#define WATCHDOG_TIMEOUT 4000     // 4 secondi
#define HEARTBEAT_INTERVAL 1000   // 1 secondo
unsigned long lastHeartbeat = 0;
unsigned long lastRaspberryMessage = 0;
bool systemActive = true;

// ========== VARIABILI GLOBALI ==========
int currentDirection = -1;  // 0=Nord, 1=Est, 2=Sud, 3=Ovest
int movementDistance = 300; // mm
bool movementSuccess = false;

// ========== SETUP ==========
void setup() {
  // Inizializza seriale debug
  Serial.begin(9600);
  Serial.println(F("=== RoboCup Rescue Maze V5.1 ==="));
  Serial.println(F("[INIT] Sistema in avvio..."));
  
  // Inizializza seriale Raspberry Pi
  SERIAL_RASPBERRY.begin(BAUD_RATE);
  
  // Setup motori
  setupMotors();
  
  // Setup sensori
  setupVL53L0X();
  setupTCS3200();
  setupMPU6050();
  
  // Setup servo
  setupServos();
  
  // Setup LED
  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, HIGH);
  
  // Watchdog timer
  wdt_enable(WDTO_4S);
  
  Serial.println(F("[INIT] Sistema pronto!"));
  Serial.println(F("[INFO] Configurazione Mecanum speculare attiva"));
  currentState = CALIBRATION;
}

// ========== SETUP MOTORI ==========
void setupMotors() {
  // Driver A - Ruote Anteriori
  pinMode(MOTOR_FL_PWM, OUTPUT);
  pinMode(MOTOR_FL_IN1, OUTPUT);
  pinMode(MOTOR_FL_IN2, OUTPUT);
  pinMode(MOTOR_FR_PWM, OUTPUT);
  pinMode(MOTOR_FR_IN3, OUTPUT);
  pinMode(MOTOR_FR_IN4, OUTPUT);
  
  // Driver B - Ruote Posteriori
  pinMode(MOTOR_BL_PWM, OUTPUT);
  pinMode(MOTOR_BL_IN1, OUTPUT);
  pinMode(MOTOR_BL_IN2, OUTPUT);
  pinMode(MOTOR_BR_PWM, OUTPUT);
  pinMode(MOTOR_BR_IN3, OUTPUT);
  pinMode(MOTOR_BR_IN4, OUTPUT);
  
  // Inizializza motori fermi
  stopMotors();
  
  Serial.println(F("[INIT] 4 motori Mecanum configurati"));
}

// ========== CONTROLLO MOTORI 4WD MECANUM - VERSI CORRETTI ==========

void setMotorFL(int speed, bool forward) {
  // FL: avanti = IN1=HIGH, IN2=LOW (configurazione verificata sperimentalmente)
  analogWrite(MOTOR_FL_PWM, abs(speed));
  if (forward) {
    digitalWrite(MOTOR_FL_IN1, HIGH);
    digitalWrite(MOTOR_FL_IN2, LOW);
  } else {
    digitalWrite(MOTOR_FL_IN1, LOW);
    digitalWrite(MOTOR_FL_IN2, HIGH);
  }
}

void setMotorFR(int speed, bool forward) {
  // FR: avanti = IN3=LOW, IN4=HIGH (montaggio speculare)
  analogWrite(MOTOR_FR_PWM, abs(speed));
  if (forward) {
    digitalWrite(MOTOR_FR_IN3, LOW);
    digitalWrite(MOTOR_FR_IN4, HIGH);
  } else {
    digitalWrite(MOTOR_FR_IN3, HIGH);
    digitalWrite(MOTOR_FR_IN4, LOW);
  }
}

void setMotorBL(int speed, bool forward) {
  // BL: avanti = IN1=LOW, IN2=HIGH (montaggio speculare)
  analogWrite(MOTOR_BL_PWM, abs(speed));
  if (forward) {
    digitalWrite(MOTOR_BL_IN1, LOW);
    digitalWrite(MOTOR_BL_IN2, HIGH);
  } else {
    digitalWrite(MOTOR_BL_IN1, HIGH);
    digitalWrite(MOTOR_BL_IN2, LOW);
  }
}

void setMotorBR(int speed, bool forward) {
  // BR: avanti = IN3=LOW, IN4=HIGH (montaggio speculare)
  analogWrite(MOTOR_BR_PWM, abs(speed));
  if (forward) {
    digitalWrite(MOTOR_BR_IN3, LOW);
    digitalWrite(MOTOR_BR_IN4, HIGH);
  } else {
    digitalWrite(MOTOR_BR_IN3, HIGH);
    digitalWrite(MOTOR_BR_IN4, LOW);
  }
}

void stopMotors() {
  // Ferma tutti e 4 i motori
  analogWrite(MOTOR_FL_PWM, 0);
  analogWrite(MOTOR_FR_PWM, 0);
  analogWrite(MOTOR_BL_PWM, 0);
  analogWrite(MOTOR_BR_PWM, 0);
  
  digitalWrite(MOTOR_FL_IN1, LOW);
  digitalWrite(MOTOR_FL_IN2, LOW);
  digitalWrite(MOTOR_FR_IN3, LOW);
  digitalWrite(MOTOR_FR_IN4, LOW);
  digitalWrite(MOTOR_BL_IN1, LOW);
  digitalWrite(MOTOR_BL_IN2, LOW);
  digitalWrite(MOTOR_BR_IN3, LOW);
  digitalWrite(MOTOR_BR_IN4, LOW);
}

void moveForward(int speed) {
  // Tutti "forward" logico con versi cablati corretti
  setMotorFL(speed, true);
  setMotorFR(speed, true);
  setMotorBL(speed, true);
  setMotorBR(speed, true);
}

void moveBackward(int speed) {
  // Tutti "backward" logico
  setMotorFL(speed, false);
  setMotorFR(speed, false);
  setMotorBL(speed, false);
  setMotorBR(speed, false);
}

void turnLeft(int speed) {
  // Ruote sinistre indietro, destre avanti (rotazione sul posto)
  setMotorFL(speed, false);
  setMotorBL(speed, false);
  setMotorFR(speed, true);
  setMotorBR(speed, true);
}

void turnRight(int speed) {
  // Ruote sinistre avanti, destre indietro (rotazione sul posto)
  setMotorFL(speed, true);
  setMotorBL(speed, true);
  setMotorFR(speed, false);
  setMotorBR(speed, false);
}

void moveForwardCalibrated(int baseSpeed) {
  // Applica coefficienti per compensare derive laterali
  setMotorFL(baseSpeed * motorFL_coefficient, true);
  setMotorFR(baseSpeed * motorFR_coefficient, true);
  setMotorBL(baseSpeed * motorBL_coefficient, true);
  setMotorBR(baseSpeed * motorBR_coefficient, true);
}

// ========== SETUP VL53L0X ==========
void setupVL53L0X() {
  Wire.begin();
  
  sensorFront.setAddress(VL53L0X_FRONT_ADDR);
  sensorRight.setAddress(VL53L0X_RIGHT_ADDR);
  sensorLeft.setAddress(VL53L0X_LEFT_ADDR);
  
  sensorFront.init();
  sensorRight.init();
  sensorLeft.init();
  
  sensorFront.setTimeout(500);
  sensorRight.setTimeout(500);
  sensorLeft.setTimeout(500);
  
  Serial.println(F("[INIT] VL53L0X configurati"));
}

int readVL53L0X(VL53L0X &sensor) {
  int distance = sensor.readRangeSingleMillimeters();
  if (sensor.timeoutOccurred()) {
    return -1;
  }
  return distance;
}

// ========== SETUP TCS3200 ==========
void setupTCS3200() {
  pinMode(TCS3200_S0, OUTPUT);
  pinMode(TCS3200_S1, OUTPUT);
  pinMode(TCS3200_S2, OUTPUT);
  pinMode(TCS3200_S3, OUTPUT);
  pinMode(TCS3200_OUT, INPUT);
  
  // Frequency scaling 20%
  digitalWrite(TCS3200_S0, HIGH);
  digitalWrite(TCS3200_S1, LOW);
  
  Serial.println(F("[INIT] TCS3200 configurato"));
}

uint32_t readTCS3200Color(char filter) {
  // R: S2=LOW, S3=LOW
  // G: S2=HIGH, S3=HIGH
  // B: S2=LOW, S3=HIGH
  
  switch(filter) {
    case 'R':
      digitalWrite(TCS3200_S2, LOW);
      digitalWrite(TCS3200_S3, LOW);
      break;
    case 'G':
      digitalWrite(TCS3200_S2, HIGH);
      digitalWrite(TCS3200_S3, HIGH);
      break;
    case 'B':
      digitalWrite(TCS3200_S2, LOW);
      digitalWrite(TCS3200_S3, HIGH);
      break;
    default:
      return 0;
  }
  
  delay(10);
  return pulseIn(TCS3200_OUT, LOW, 50000);
}

bool detectBlueTile() {
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');
  
  // CALIBRARE QUESTI VALORI in base al proprio sensore!
  // BLU: componente blu bassa, rosso e verde alti
  bool isBlue = (blue < 50) && (red > 100) && (green > 100);
  
  return isBlue;
}

bool detectBlackTile() {
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');
  
  // NERO: tutte componenti alte (poco riflesso)
  bool isBlack = (red > 200) && (green > 200) && (blue > 200);
  
  return isBlack;
}

bool detectRedTile() {
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');
  
  // ROSSO: R bassa, G e B alti
  bool isRed = (red < 50) && (green > 100) && (blue > 100);
  
  return isRed;
}

char detectTileColor() {
  // Rileva colore cella
  if (detectBlackTile()) return 'K';  // Black
  if (detectRedTile()) return 'R';    // Red
  if (detectBlueTile()) return 'B';   // Blue
  return 'W';  // White (default)
}

// ========== SETUP MPU6050 ==========
void setupMPU6050() {
  mpu.initialize();
  if (mpu.testConnection()) {
    Serial.println(F("[INIT] MPU6050 connesso"));
  } else {
    Serial.println(F("[ERROR] MPU6050 non risponde!"));
  }
}

float readMPU6050Pitch() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  // Calcola pitch in gradi
  float pitch = atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;
  return pitch;
}

// ========== SETUP SERVO ==========
void setupServos() {
  servoCamera.attach(SERVO_CAMERA_PIN);
  servoMediakit.attach(SERVO_MEDIKIT_PIN);
  
  servoCamera.write(CAMERA_CENTER);
  servoMediakit.write(MEDIKIT_CLOSED);
  
  Serial.println(F("[INIT] Servo configurati"));
}

void setCameraAngle(int angle) {
  servoCamera.write(angle);
  delay(500);  // Attendi movimento
}

// ========== MEDIKIT ==========
void releaseMediakit(int count) {
  if (count == 0) {
    Serial.println(F("[MEDIKIT] Nessun medikit da rilasciare"));
    return;
  }
  
  Serial.print(F("[MEDIKIT] Rilascio "));
  Serial.print(count);
  Serial.println(F(" medikit"));
  
  for (int i = 0; i < count; i++) {
    wdt_reset();
    servoMediakit.write(MEDIKIT_OPEN);
    delay(500);
    servoMediakit.write(MEDIKIT_CLOSED);
    delay(500);
  }
  
  Serial.println(F("[MEDIKIT] Rilascio completato"));
}

// ========== LED VICTIM SIGNAL ==========
void signalVictimDetection() {
  Serial.println(F("[LED] Segnalazione vittima - lampeggio 5s"));
  
  unsigned long startTime = millis();
  bool ledState = false;
  
  while (millis() - startTime < LED_BLINK_DURATION) {
    wdt_reset();  // IMPORTANTE: reset watchdog durante attesa
    
    ledState = !ledState;
    digitalWrite(LED_STATUS, ledState ? HIGH : LOW);
    
    delay(LED_BLINK_INTERVAL);
  }
  
  // Ripristina LED allo stato sistema
  digitalWrite(LED_STATUS, systemActive ? HIGH : LOW);
  
  Serial.println(F("[LED] Segnalazione completata"));
}

// ========== GESTIONE CODICI VITTIMA ==========
void handleVictimCode(int code) {
  switch (code) {
    case CODE_HARMED:
      Serial.println(F("[VICTIM] HARMED rilevata"));
      signalVictimDetection();  // Lampeggio PRIMA
      releaseMediakit(2);        // POI medikit
      break;
    
    case CODE_STABLE:
      Serial.println(F("[VICTIM] STABLE rilevata"));
      signalVictimDetection();
      releaseMediakit(1);
      break;
    
    case CODE_UNHARMED:
      Serial.println(F("[VICTIM] UNHARMED rilevata"));
      signalVictimDetection();  // Lampeggio anche se 0 medikit
      // Nessun medikit da rilasciare
      break;
    
    case CODE_FALSO_POSITIVO:
      Serial.println(F("[INFO] Fake target ignorato"));
      break;
    
    case CODE_NESSUNA:
      Serial.println(F("[INFO] Nessuna vittima rilevata"));
      break;
    
    default:
      Serial.println(F("[ERROR] Codice vittima non valido"));
      break;
  }
}

// ========== MOVIMENTO SICURO ==========
bool moveForwardSafe(int distanceMM) {
  Serial.print(F("[MOVE] Avanzamento sicuro: "));
  Serial.print(distanceMM);
  Serial.println(F(" mm"));
  
  int stepsCompleted = 0;
  const int STEP_INCREMENT_MM = 30;  // 3 cm per step
  int totalSteps = distanceMM / STEP_INCREMENT_MM;
  
  for (int step = 0; step < totalSteps; step++) {
    wdt_reset();
    
    // Controlla ostacoli frontali
    int frontDistance = readVL53L0X(sensorFront);
    if (frontDistance < WALL_THRESHOLD && frontDistance > 0) {
      Serial.println(F("[STOP] Ostacolo frontale rilevato"));
      stopMotors();
      return false;
    }
    
    // Controlla inclinazione
    float pitch = readMPU6050Pitch();
    if (abs(pitch) > MAX_SLOPE_ANGLE) {
      Serial.println(F("[STOP] Pendenza eccessiva"));
      stopMotors();
      moveBackward(MOTOR_SPEED_SLOW);
      delay(500);
      stopMotors();
      return false;
    }
    
    // Leggi colore cella PRIMA del movimento
    char tileColor = detectTileColor();
    
    // Gestione celle speciali
    if (tileColor == 'K') {  // Black tile
      Serial.println(F("[STOP] Cella NERA rilevata"));
      stopMotors();
      moveBackward(MOTOR_SPEED_SLOW);
      delay(300);
      stopMotors();
      return false;
    }
    
    if (tileColor == 'R') {  // Red tile
      Serial.println(F("[STOP] Cella ROSSA rilevata"));
      stopMotors();
      moveBackward(MOTOR_SPEED_SLOW);
      delay(300);
      stopMotors();
      return false;
    }
    
    // Movimento step 30mm
    moveForwardCalibrated(MOTOR_SPEED_NORMAL);
    delay(STEP_INCREMENT_MM * 10 / MOTOR_SPEED_NORMAL);
    stopMotors();
    delay(50);
    
    stepsCompleted++;
    
    // Gestione cella BLU (dopo aver completato movimento)
    if (tileColor == 'B') {  // Blue tile
      Serial.println(F("[INFO] Movimento su cella BLU"));
      
      // Stazionamento obbligatorio 5 secondi
      Serial.println(F("[WAIT] Stazionamento 5s su cella blu..."));
      for (int i = 0; i < 5; i++) {
        wdt_reset();
        delay(1000);
      }
      
      // Notifica Raspberry per blacklist
      sendToRaspberry(CODE_BLUE_TILE);
      Serial.println(F("[→ Raspberry] CODE_BLUE_TILE"));
    }
  }
  
  Serial.print(F("[SUCCESS] Movimento completato: "));
  Serial.print(stepsCompleted * STEP_INCREMENT_MM);
  Serial.println(F(" mm"));
  
  return true;
}

void executeRotation(int targetDirection) {
  int currentDir = currentDirection;
  int rotation = (targetDirection - currentDir + 4) % 4;
  
  switch(rotation) {
    case 0:  // Nessuna rotazione
      Serial.println(F("[MOVE] Nessuna rotazione necessaria"));
      break;
    
    case 1:  // 90° destra
      Serial.println(F("[MOVE] Rotazione 90° DESTRA"));
      turnRight(MOTOR_SPEED_TURN);
      delay(500);  // CALIBRARE QUESTO VALORE
      stopMotors();
      break;
    
    case 2:  // 180°
      Serial.println(F("[MOVE] Rotazione 180°"));
      turnRight(MOTOR_SPEED_TURN);
      delay(1000);  // CALIBRARE QUESTO VALORE
      stopMotors();
      break;
    
    case 3:  // 90° sinistra
      Serial.println(F("[MOVE] Rotazione 90° SINISTRA"));
      turnLeft(MOTOR_SPEED_TURN);
      delay(500);  // CALIBRARE QUESTO VALORE
      stopMotors();
      break;
  }
  
  currentDirection = targetDirection;
}

// ========== COMUNICAZIONE RASPBERRY ==========
void sendToRaspberry(int value) {
  SERIAL_RASPBERRY.println(value);
}

void checkRaspberryMessages() {
  if (SERIAL_RASPBERRY.available()) {
    String message = SERIAL_RASPBERRY.readStringUntil('\n');
    message.trim();
    
    lastRaspberryMessage = millis();
    
    // Parsing messaggio
    if (message.startsWith("DIR:")) {
      currentDirection = message.substring(4).toInt();
      Serial.print(F("[← Raspberry] Direzione ricevuta: "));
      Serial.println(currentDirection);
      
      // Esegui movimento
      currentState = MOVE;
    }
    else if (message.startsWith("CODE:")) {
      int victimCode = message.substring(5).toInt();
      Serial.print(F("[← Raspberry] Codice vittima: "));
      Serial.println(victimCode);
      handleVictimCode(victimCode);
    }
    else if (message == "HEARTBEAT") {
      // Heartbeat ricevuto
      systemActive = true;
    }
  }
}

// ========== WATCHDOG & HEARTBEAT ==========
void sendHeartbeat() {
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    SERIAL_RASPBERRY.println("HEARTBEAT");
    lastHeartbeat = millis();
  }
}

void checkWatchdog() {
  if (millis() - lastRaspberryMessage > WATCHDOG_TIMEOUT) {
    Serial.println(F("[WARNING] Raspberry non risponde!"));
    systemActive = false;
    digitalWrite(LED_STATUS, LOW);
    stopMotors();
    currentState = EMERGENCY_STOP;
  }
}

// ========== SCANSIONE MURO ==========
void scanWall(int angle, const char* direction) {
  Serial.print(F("[SCAN] Scansione muro "));
  Serial.println(direction);
  
  setCameraAngle(angle);
  
  // Leggi distanza
  int distance = -1;
  if (angle == CAMERA_CENTER) {
    distance = readVL53L0X(sensorFront);
  } else if (angle == CAMERA_RIGHT) {
    distance = readVL53L0X(sensorRight);
  } else if (angle == CAMERA_LEFT) {
    distance = readVL53L0X(sensorLeft);
  }
  
  // Invia a Raspberry: 1 se muro, 0 altrimenti
  if (distance < WALL_THRESHOLD && distance > 0) {
    Serial.println(F("[SCAN] MURO rilevato"));
    sendToRaspberry(1);
  } else {
    Serial.println(F("[SCAN] Nessun muro"));
    sendToRaspberry(0);
  }
}

// ========== LOOP PRINCIPALE ==========
void loop() {
  wdt_reset();  // Reset watchdog
  sendHeartbeat();
  checkRaspberryMessages();
  checkWatchdog();
  
  switch (currentState) {
    case CALIBRATION:
      Serial.println(F("[STATE] CALIBRATION"));
      // Calibrazione sensori VL53L0X
      delay(1000);
      currentState = SCAN_FRONT;
      break;
    
    case SCAN_FRONT:
      Serial.println(F("[STATE] SCAN_FRONT"));
      scanWall(CAMERA_CENTER, "FRONT");
      currentState = SCAN_RIGHT;
      break;
    
    case SCAN_RIGHT:
      Serial.println(F("[STATE] SCAN_RIGHT"));
      scanWall(CAMERA_RIGHT, "RIGHT");
      currentState = SCAN_LEFT;
      break;
    
    case SCAN_LEFT:
      Serial.println(F("[STATE] SCAN_LEFT"));
      scanWall(CAMERA_LEFT, "LEFT");
      currentState = WAIT_DIRECTION;
      break;
    
    case WAIT_DIRECTION:
      Serial.println(F("[STATE] WAIT_DIRECTION"));
      // Attende direzione da Raspberry
      // Transizione gestita in checkRaspberryMessages()
      delay(100);
      break;
    
    case MOVE:
      Serial.println(F("[STATE] MOVE"));
      executeRotation(currentDirection);
      movementSuccess = moveForwardSafe(movementDistance);
      
      // Invia feedback
      sendToRaspberry(movementSuccess ? 1 : 0);
      
      currentState = WAIT_CONFIRM;
      break;
    
    case WAIT_CONFIRM:
      Serial.println(F("[STATE] WAIT_CONFIRM"));
      // Attende conferma aggiornamento mappa
      delay(500);
      currentState = CALIBRATION;  // Ricomincia ciclo
      break;
    
    case EMERGENCY_STOP:
      Serial.println(F("[STATE] EMERGENCY_STOP"));
      stopMotors();
      digitalWrite(LED_STATUS, LOW);
      // Sistema bloccato finché Raspberry non risponde
      if (systemActive) {
        currentState = CALIBRATION;
      }
      delay(1000);
      break;
  }
  
  delay(50);
}

// ========== FUNZIONI DI TEST (OPZIONALI) ==========

void testSingleMotors() {
  Serial.println(F("=== TEST MOTORI SINGOLI ==="));
  
  Serial.println(F("1. Front Left Forward"));
  setMotorFL(150, true);
  delay(2000);
  stopMotors();
  delay(1000);
  
  Serial.println(F("2. Front Right Forward"));
  setMotorFR(150, true);
  delay(2000);
  stopMotors();
  delay(1000);
  
  Serial.println(F("3. Back Left Forward"));
  setMotorBL(150, true);
  delay(2000);
  stopMotors();
  delay(1000);
  
  Serial.println(F("4. Back Right Forward"));
  setMotorBR(150, true);
  delay(2000);
  stopMotors();
  
  Serial.println(F("=== TEST COMPLETATO ==="));
}

void testStraightMovement() {
  Serial.println(F("=== TEST MOVIMENTO DRITTO ==="));
  Serial.println(F("Verifica configurazione Mecanum speculare..."));
  
  Serial.println(F("Movimento avanti 3 secondi..."));
  moveForward(MOTOR_SPEED_NORMAL);
  delay(3000);
  stopMotors();
  
  delay(2000);
  
  Serial.println(F("Movimento indietro 3 secondi..."));
  moveBackward(MOTOR_SPEED_NORMAL);
  delay(3000);
  stopMotors();
  
  Serial.println(F("=== TEST COMPLETATO ==="));
}

void testRotations() {
  Serial.println(F("=== TEST ROTAZIONI ==="));
  
  Serial.println(F("Rotazione SINISTRA 2 secondi..."));
  turnLeft(MOTOR_SPEED_TURN);
  delay(2000);
  stopMotors();
  
  delay(2000);
  
  Serial.println(F("Rotazione DESTRA 2 secondi..."));
  turnRight(MOTOR_SPEED_TURN);
  delay(2000);
  stopMotors();
  
  Serial.println(F("=== TEST COMPLETATO ==="));
}
