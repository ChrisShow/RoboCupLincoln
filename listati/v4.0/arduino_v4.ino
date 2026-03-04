/*
 * RoboCup Rescue Maze 2026 - Arduino Mega V4
 * Versione con Blue Tiles + LED Victim Signal
 * 
 * NOVITÀ V4 (rispetto a V3):
 * - Rilevamento celle BLU con TCS3200
 * - Stazionamento 5s su cella blu + invio CODE_BLUE_TILE a Raspberry
 * - LED lampeggio bianco 5s (500ms ON/OFF) prima rilascio medikit quando vittima rilevata
 * - Gestione vittima: signalVictimDetection() → releaseMediakit()
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <LiquidCrystal_I2C.h>
#include <MPU6050.h>
#include <Servo.h>
#include <avr/wdt.h>  // Watchdog timer

// =================== CONFIGURAZIONE ===================

// Pin Motori
#define MOTOR_LEFT_ENA 3
#define MOTOR_LEFT_IN1 24
#define MOTOR_LEFT_IN2 25
#define MOTOR_RIGHT_ENB 4
#define MOTOR_RIGHT_IN3 26
#define MOTOR_RIGHT_IN4 27

// Pin Servo
#define SERVO_CAMERA_PIN 9
#define SERVO_MEDIKIT_PIN 10

// Pin Pulsante e LED
#define BUTTON_START 2
#define LED_STATUS 13

// Pin Ultrasuoni
#define TRIG_FRONT 30
#define ECHO_FRONT 31
#define TRIG_RIGHT 32
#define ECHO_RIGHT 33
#define TRIG_LEFT 34
#define ECHO_LEFT 35

// Pin Sensore Colore TCS3200
#define TCS_S0 36
#define TCS_S1 37
#define TCS_S2 38
#define TCS_S3 39
#define TCS_OUT 40

// Pin XSHUT VL53L0X (reset selettivo)
#define XSHUT_FL1 42
#define XSHUT_FL2 43
#define XSHUT_FR1 44
#define XSHUT_FR2 45
#define XSHUT_BR1 46
#define XSHUT_BR2 47
#define XSHUT_BL1 48
#define XSHUT_BL2 49

// Indirizzi I2C VL53L0X
#define VL_ADDR_FL1 0x30
#define VL_ADDR_FL2 0x31
#define VL_ADDR_FR1 0x32
#define VL_ADDR_FR2 0x33
#define VL_ADDR_BR1 0x34
#define VL_ADDR_BR2 0x35
#define VL_ADDR_BL1 0x36
#define VL_ADDR_BL2 0x37

// Codici comunicazione
#define CODE_NESSUNA 0
#define CODE_HARMED 1    // Phi o Somma=2
#define CODE_STABLE 2    // Psi o Somma=1
#define CODE_UNHARMED 3  // Omega o Somma=0
#define CODE_BLUE_TILE 5 // NUOVO V4: Cella blu rilevata

#define DIR_TERMINA 0
#define DIR_DRITTO 1
#define DIR_DESTRA 2
#define DIR_SINISTRA 3
#define DIR_DIETRO 4

// Parametri movimento
#define MOTOR_SPEED_NORMAL 150
#define MOTOR_SPEED_TURN 120
#define STEP_DISTANCE_MM 300  // 30 cm
#define STEP_INCREMENT_MM 30

// Soglie sensori
#define WALL_THRESHOLD_MM 150
#define CALIBRATION_TARGET_MM 80
#define CALIBRATION_TOLERANCE_MM 5
#define SLOPE_THRESHOLD_ANGLE 17.0

// Heartbeat
#define HEARTBEAT_INTERVAL 1000  // ms

// NUOVO V4: LED Victim Signal
#define LED_BLINK_DURATION 5000  // 5 secondi
#define LED_BLINK_INTERVAL 500   // 500ms ON/OFF

// =================== OGGETTI GLOBALI ===================

// Sensori VL53L0X
VL53L0X sensorFL1, sensorFL2, sensorFR1, sensorFR2;
VL53L0X sensorBR1, sensorBR2, sensorBL1, sensorBL2;

// Display LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Giroscopio
MPU6050 mpu;

// Servo
Servo servoCamera;
Servo servoMedikit;

// =================== STRUTTURE DATI ===================

struct SensorData {
  uint16_t frontLeft1;
  uint16_t frontLeft2;
  uint16_t frontRight1;
  uint16_t frontRight2;
  uint16_t backRight1;
  uint16_t backRight2;
  uint16_t backLeft1;
  uint16_t backLeft2;
  uint16_t ultraFront;
  uint16_t ultraRight;
  uint16_t ultraLeft;
};

SensorData sensorData;

// State machine
enum State {
  IDLE,
  CALIBRATION,
  SCAN_FRONT,
  SCAN_RIGHT,
  SCAN_LEFT,
  WAIT_DIRECTION,
  MOVE,
  WAIT_CONFIRM
};

State currentState = IDLE;

// Variabili globali
bool systemActive = false;
unsigned long lastHeartbeat = 0;

// =================== SETUP ===================

void setup() {
  // Watchdog timer (4 secondi)
  wdt_enable(WDTO_4S);
  
  // Seriale
  Serial.begin(115200);
  Serial.println(F("[BOOT] Arduino V4 inizializzato"));
  
  // Pin motori
  pinMode(MOTOR_LEFT_ENA, OUTPUT);
  pinMode(MOTOR_LEFT_IN1, OUTPUT);
  pinMode(MOTOR_LEFT_IN2, OUTPUT);
  pinMode(MOTOR_RIGHT_ENB, OUTPUT);
  pinMode(MOTOR_RIGHT_IN3, OUTPUT);
  pinMode(MOTOR_RIGHT_IN4, OUTPUT);
  
  // Pin pulsante e LED
  pinMode(BUTTON_START, INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT);
  
  // Pin ultrasuoni
  pinMode(TRIG_FRONT, OUTPUT);
  pinMode(ECHO_FRONT, INPUT);
  pinMode(TRIG_RIGHT, OUTPUT);
  pinMode(ECHO_RIGHT, INPUT);
  pinMode(TRIG_LEFT, OUTPUT);
  pinMode(ECHO_LEFT, INPUT);
  
  // Pin sensore colore
  pinMode(TCS_S0, OUTPUT);
  pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT);
  pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S0, HIGH);
  digitalWrite(TCS_S1, LOW);  // Frequenza 2%
  
  // Servo
  servoCamera.attach(SERVO_CAMERA_PIN);
  servoMedikit.attach(SERVO_MEDIKIT_PIN);
  servoCamera.write(90);  // Posizione frontale
  servoMedikit.write(0);  // Chiuso
  
  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("RoboCup 2026 - V4"));
  lcd.setCursor(0, 1);
  lcd.print(F("Premi START"));
  
  // I2C
  Wire.begin();
  Wire.setClock(400000);
  
  // Inizializza VL53L0X
  initVL53L0X();
  
  // Inizializza MPU6050
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println(F("[ERROR] MPU6050 non trovato!"));
    lcd.setCursor(0, 2);
    lcd.print(F("ERR: MPU6050"));
  }
  
  Serial.println(F("[OK] Sistema pronto"));
  
  wdt_reset();
}

// =================== LOOP PRINCIPALE ===================

void loop() {
  wdt_reset();  // Reset watchdog
  
  // Invia heartbeat periodico
  sendHeartbeat();
  
  // Controlla pulsante START
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(BUTTON_START);
  
  if (buttonState == LOW && lastButtonState == HIGH) {
    delay(50);  // Debounce
    if (digitalRead(BUTTON_START) == LOW) {
      systemActive = !systemActive;
      digitalWrite(LED_STATUS, systemActive ? HIGH : LOW);
      
      if (systemActive) {
        Serial.println(F("[INFO] Sistema ATTIVO"));
        lcd.clear();
        lcd.print(F("SISTEMA ATTIVO"));
        currentState = CALIBRATION;
      } else {
        Serial.println(F("[INFO] Sistema FERMO"));
        stopMotors();
        lcd.clear();
        lcd.print(F("SISTEMA FERMO"));
        currentState = IDLE;
      }
    }
  }
  lastButtonState = buttonState;
  
  if (!systemActive) {
    return;
  }
  
  // State machine
  switch (currentState) {
    case IDLE:
      break;
      
    case CALIBRATION:
      handleCalibration();
      break;
      
    case SCAN_FRONT:
      handleScanFront();
      break;
      
    case SCAN_RIGHT:
      handleScanRight();
      break;
      
    case SCAN_LEFT:
      handleScanLeft();
      break;
      
    case WAIT_DIRECTION:
      handleWaitDirection();
      break;
      
    case MOVE:
      handleMove();
      break;
      
    case WAIT_CONFIRM:
      handleWaitConfirm();
      break;
  }
}

// =================== HEARTBEAT ===================

void sendHeartbeat() {
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    Serial.println(F("HB"));
    lastHeartbeat = millis();
  }
}

// =================== COMUNICAZIONE SERIALE ===================

String readSerialCommand() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    // Filtra heartbeat da Raspberry
    if (input == "HB") {
      return "";
    }
    
    Serial.print(F("[← Raspberry] "));
    Serial.println(input);
    return input;
  }
  return "";
}

void sendToRaspberry(int value) {
  Serial.println(value);
  Serial.print(F("[→ Raspberry] "));
  Serial.println(value);
}

// =================== INIZIALIZZAZIONE VL53L0X ===================

void initVL53L0X() {
  Serial.println(F("[INFO] Inizializzazione VL53L0X..."));
  
  // Reset tutti i sensori
  pinMode(XSHUT_FL1, OUTPUT);
  pinMode(XSHUT_FL2, OUTPUT);
  pinMode(XSHUT_FR1, OUTPUT);
  pinMode(XSHUT_FR2, OUTPUT);
  pinMode(XSHUT_BR1, OUTPUT);
  pinMode(XSHUT_BR2, OUTPUT);
  pinMode(XSHUT_BL1, OUTPUT);
  pinMode(XSHUT_BL2, OUTPUT);
  
  digitalWrite(XSHUT_FL1, LOW);
  digitalWrite(XSHUT_FL2, LOW);
  digitalWrite(XSHUT_FR1, LOW);
  digitalWrite(XSHUT_FR2, LOW);
  digitalWrite(XSHUT_BR1, LOW);
  digitalWrite(XSHUT_BR2, LOW);
  digitalWrite(XSHUT_BL1, LOW);
  digitalWrite(XSHUT_BL2, LOW);
  delay(10);
  
  // Inizializza sensori uno alla volta
  initSingleVL53L0X(XSHUT_FL1, VL_ADDR_FL1, sensorFL1, "FL1");
  initSingleVL53L0X(XSHUT_FL2, VL_ADDR_FL2, sensorFL2, "FL2");
  initSingleVL53L0X(XSHUT_FR1, VL_ADDR_FR1, sensorFR1, "FR1");
  initSingleVL53L0X(XSHUT_FR2, VL_ADDR_FR2, sensorFR2, "FR2");
  initSingleVL53L0X(XSHUT_BR1, VL_ADDR_BR1, sensorBR1, "BR1");
  initSingleVL53L0X(XSHUT_BR2, VL_ADDR_BR2, sensorBR2, "BR2");
  initSingleVL53L0X(XSHUT_BL1, VL_ADDR_BL1, sensorBL1, "BL1");
  initSingleVL53L0X(XSHUT_BL2, VL_ADDR_BL2, sensorBL2, "BL2");
  
  Serial.println(F("[OK] VL53L0X inizializzati"));
}

void initSingleVL53L0X(int xshutPin, uint8_t addr, VL53L0X &sensor, const char* name) {
  digitalWrite(xshutPin, HIGH);
  delay(10);
  
  if (!sensor.init()) {
    Serial.print(F("[ERROR] VL53L0X "));
    Serial.print(name);
    Serial.println(F(" non trovato!"));
    lcd.setCursor(0, 3);
    lcd.print(F("ERR: VL53 "));
    lcd.print(name);
    return;
  }
  
  sensor.setAddress(addr);
  sensor.setTimeout(500);
  sensor.startContinuous();
  
  Serial.print(F("[OK] VL53L0X "));
  Serial.print(name);
  Serial.print(F(" @ 0x"));
  Serial.println(addr, HEX);
}

// =================== LETTURA SENSORI ===================

void readAllSensors() {
  sensorData.frontLeft1 = sensorFL1.readRangeContinuousMillimeters();
  sensorData.frontLeft2 = sensorFL2.readRangeContinuousMillimeters();
  sensorData.frontRight1 = sensorFR1.readRangeContinuousMillimeters();
  sensorData.frontRight2 = sensorFR2.readRangeContinuousMillimeters();
  sensorData.backRight1 = sensorBR1.readRangeContinuousMillimeters();
  sensorData.backRight2 = sensorBR2.readRangeContinuousMillimeters();
  sensorData.backLeft1 = sensorBL1.readRangeContinuousMillimeters();
  sensorData.backLeft2 = sensorBL2.readRangeContinuousMillimeters();
  
  sensorData.ultraFront = readUltrasonic(TRIG_FRONT, ECHO_FRONT);
  sensorData.ultraRight = readUltrasonic(TRIG_RIGHT, ECHO_RIGHT);
  sensorData.ultraLeft = readUltrasonic(TRIG_LEFT, ECHO_LEFT);
}

uint16_t readUltrasonic(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000);
  uint16_t distance = duration * 0.034 / 2;
  
  return (distance > 0 && distance < 400) ? distance : 8000;
}

// =================== RILEVAMENTO COLORE CELLE ===================

uint32_t readTCS3200Color(char filter) {
  /*
   * NUOVO V4: Legge frequenza sensore colore TCS3200
   * filter: 'R' = rosso, 'G' = verde, 'B' = blu, 'C' = clear
   */
  
  switch (filter) {
    case 'R':
      digitalWrite(TCS_S2, LOW);
      digitalWrite(TCS_S3, LOW);
      break;
    case 'G':
      digitalWrite(TCS_S2, HIGH);
      digitalWrite(TCS_S3, HIGH);
      break;
    case 'B':
      digitalWrite(TCS_S2, LOW);
      digitalWrite(TCS_S3, HIGH);
      break;
    case 'C':
      digitalWrite(TCS_S2, HIGH);
      digitalWrite(TCS_S3, LOW);
      break;
  }
  
  delay(10);  // Stabilizzazione
  
  // Leggi frequenza (periodo in us)
  uint32_t frequency = pulseIn(TCS_OUT, LOW, 50000);
  
  return frequency;
}

bool detectBlueTile() {
  /*
   * NUOVO V4: Rileva se cella corrente è BLU
   * Ritorna: true se blu, false altrimenti
   */
  
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');
  
  Serial.print(F("[TCS3200] R="));
  Serial.print(red);
  Serial.print(F(" G="));
  Serial.print(green);
  Serial.print(F(" B="));
  Serial.println(blue);
  
  // Logica rilevamento BLU:
  // - Componente BLU alta
  // - Componenti R e G basse
  // Nota: frequenze invertite (valore basso = colore intenso)
  
  // Soglie da calibrare in base al sensore
  bool isBlue = (blue < 50) && (red > 100) && (green > 100);
  
  if (isBlue) {
    Serial.println(F("[INFO] Cella BLU rilevata!"));
  }
  
  return isBlue;
}

bool detectBlackTile() {
  /*
   * Rileva cella NERA
   */
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');
  
  // NERO: tutte componenti alte (poco riflesso)
  bool isBlack = (red > 200) && (green > 200) && (blue > 200);
  
  if (isBlack) {
    Serial.println(F("[WARN] Cella NERA rilevata!"));
  }
  
  return isBlack;
}

bool detectRedTile() {
  /*
   * Rileva cella ROSSA
   */
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');
  
  // ROSSO: componente R bassa, G e B alte
  bool isRed = (red < 50) && (green > 100) && (blue > 100);
  
  if (isRed) {
    Serial.println(F("[WARN] Cella ROSSA rilevata!"));
  }
  
  return isRed;
}

// =================== CALIBRAZIONE ===================

void handleCalibration() {
  Serial.println(F("[STATE] CALIBRATION"));
  lcd.setCursor(0, 1);
  lcd.print(F("Calibrazione...     "));
  
  readAllSensors();
  
  // Calibrazione laterale sinistra
  int16_t errorLeft = sensorData.frontLeft1 - CALIBRATION_TARGET_MM;
  if (abs(errorLeft) > CALIBRATION_TOLERANCE_MM) {
    if (errorLeft > 0) {
      moveLeft(abs(errorLeft) / 10);
    } else {
      moveRight(abs(errorLeft) / 10);
    }
  }
  
  // Calibrazione laterale destra
  int16_t errorRight = sensorData.frontRight1 - CALIBRATION_TARGET_MM;
  if (abs(errorRight) > CALIBRATION_TOLERANCE_MM) {
    if (errorRight > 0) {
      moveRight(abs(errorRight) / 10);
    } else {
      moveLeft(abs(errorRight) / 10);
    }
  }
  
  Serial.println(F("[OK] Calibrazione completata"));
  currentState = SCAN_FRONT;
}

// =================== SCANSIONE ===================

void handleScanFront() {
  Serial.println(F("[STATE] SCAN_FRONT"));
  servoCamera.write(90);  // Frontale
  delay(500);
  
  readAllSensors();
  
  bool wallPresent = (sensorData.ultraFront < WALL_THRESHOLD_MM);
  sendToRaspberry(wallPresent ? 1 : 0);
  
  Serial.print(F("[DEBUG] Distanza FRONT: "));
  Serial.print(sensorData.ultraFront);
  Serial.println(F(" mm"));
  
  if (wallPresent) {
    // Attendi codice da Raspberry (analisi cognitive target/lettera)
    String cmd = "";
    while (cmd == "") {
      wdt_reset();
      cmd = readSerialCommand();
      sendHeartbeat();
      delay(10);
    }
    
    int code = cmd.toInt();
    handleVictimCode(code);
  }
  
  currentState = SCAN_RIGHT;
}

void handleScanRight() {
  Serial.println(F("[STATE] SCAN_RIGHT"));
  servoCamera.write(0);  // Destra
  delay(500);
  
  readAllSensors();
  
  bool wallPresent = (sensorData.ultraRight < WALL_THRESHOLD_MM);
  sendToRaspberry(wallPresent ? 1 : 0);
  
  Serial.print(F("[DEBUG] Distanza RIGHT: "));
  Serial.print(sensorData.ultraRight);
  Serial.println(F(" mm"));
  
  if (wallPresent) {
    String cmd = "";
    while (cmd == "") {
      wdt_reset();
      cmd = readSerialCommand();
      sendHeartbeat();
      delay(10);
    }
    
    int code = cmd.toInt();
    handleVictimCode(code);
  }
  
  currentState = SCAN_LEFT;
}

void handleScanLeft() {
  Serial.println(F("[STATE] SCAN_LEFT"));
  servoCamera.write(180);  // Sinistra
  delay(500);
  
  readAllSensors();
  
  bool wallPresent = (sensorData.ultraLeft < WALL_THRESHOLD_MM);
  sendToRaspberry(wallPresent ? 1 : 0);
  
  Serial.print(F("[DEBUG] Distanza LEFT: "));
  Serial.print(sensorData.ultraLeft);
  Serial.println(F(" mm"));
  
  if (wallPresent) {
    String cmd = "";
    while (cmd == "") {
      wdt_reset();
      cmd = readSerialCommand();
      sendHeartbeat();
      delay(10);
    }
    
    int code = cmd.toInt();
    handleVictimCode(code);
  }
  
  servoCamera.write(90);  // Ritorna frontale
  currentState = WAIT_DIRECTION;
}

void handleVictimCode(int code) {
  /*
   * NUOVO V4: Gestione codice vittima con LED signal
   */
  
  switch (code) {
    case CODE_HARMED:
      Serial.println(F("[VICTIM] HARMED rilevata"));
      signalVictimDetection();  // NUOVO V4: Lampeggio LED 5s
      Serial.println(F("[ACTION] Rilascio 2 medikit"));
      releaseMediakit(2);
      break;
      
    case CODE_STABLE:
      Serial.println(F("[VICTIM] STABLE rilevata"));
      signalVictimDetection();  // NUOVO V4: Lampeggio LED 5s
      Serial.println(F("[ACTION] Rilascio 1 medikit"));
      releaseMediakit(1);
      break;
      
    case CODE_UNHARMED:
      Serial.println(F("[VICTIM] UNHARMED rilevata"));
      signalVictimDetection();  // NUOVO V4: Lampeggio LED 5s
      Serial.println(F("[ACTION] Nessun medikit"));
      break;
      
    default:
      Serial.println(F("[ACTION] Nessuna vittima"));
      break;
  }
}

void signalVictimDetection() {
  /*
   * NUOVO V4: Lampeggio LED bianco per 5 secondi (500ms ON/OFF)
   * Chiamata PRIMA di rilasciare medikit
   */
  
  Serial.println(F("[LED] Segnalazione vittima - lampeggio 5s"));
  lcd.setCursor(0, 2);
  lcd.print(F("VITTIMA RILEVATA!"));
  
  unsigned long startTime = millis();
  bool ledState = false;
  
  while (millis() - startTime < LED_BLINK_DURATION) {
    wdt_reset();  // Importante durante attesa
    
    ledState = !ledState;
    digitalWrite(LED_STATUS, ledState ? HIGH : LOW);
    
    delay(LED_BLINK_INTERVAL);
  }
  
  // Spegni LED dopo lampeggio
  digitalWrite(LED_STATUS, systemActive ? HIGH : LOW);
  
  Serial.println(F("[LED] Segnalazione completata"));
  lcd.setCursor(0, 2);
  lcd.print(F("                    "));
}

void releaseMediakit(int count) {
  /*
   * Rilascia medikit (chiamata DOPO signalVictimDetection)
   */
  Serial.print(F("[MEDIKIT] Rilascio "));
  Serial.print(count);
  Serial.println(F(" medikit"));
  
  for (int i = 0; i < count; i++) {
    servoMedikit.write(90);  // Apri
    delay(500);
    servoMedikit.write(0);   // Chiudi
    delay(500);
  }
  
  Serial.println(F("[OK] Medikit rilasciati"));
}

// =================== MOVIMENTO ===================

void handleWaitDirection() {
  Serial.println(F("[STATE] WAIT_DIRECTION"));
  lcd.setCursor(0, 1);
  lcd.print(F("Attendo direzione..."));
  
  String cmd = "";
  while (cmd == "") {
    wdt_reset();
    cmd = readSerialCommand();
    sendHeartbeat();
    delay(10);
  }
  
  int direction = cmd.toInt();
  
  if (direction == DIR_TERMINA) {
    Serial.println(F("[INFO] Esplorazione completata"));
    systemActive = false;
    currentState = IDLE;
    return;
  }
  
  executeMovement(direction);
}

void executeMovement(int direction) {
  Serial.print(F("[MOVE] Direzione: "));
  Serial.println(direction);
  
  bool success = true;
  
  switch (direction) {
    case DIR_DRITTO:
      success = moveForwardSafe();
      break;
      
    case DIR_DESTRA:
      turnRight90();
      success = moveForwardSafe();
      break;
      
    case DIR_SINISTRA:
      turnLeft90();
      success = moveForwardSafe();
      break;
      
    case DIR_DIETRO:
      turnRight90();
      turnRight90();
      success = moveForwardSafe();
      break;
  }
  
  sendToRaspberry(success ? 1 : 0);
  currentState = WAIT_CONFIRM;
}

bool moveForwardSafe() {
  /*
   * NUOVO V4: Movimento sicuro con rilevamento celle speciali
   */
  
  for (int traveled = 0; traveled < STEP_DISTANCE_MM; traveled += STEP_INCREMENT_MM) {
    wdt_reset();
    
    // Controlla ostacoli
    readAllSensors();
    
    // NUOVO V4: Rileva cella BLU
    if (detectBlueTile()) {
      Serial.println(F("[INFO] Movimento su cella BLU"));
      // Completa movimento
      setMotorSpeed(MOTOR_SPEED_NORMAL, MOTOR_SPEED_NORMAL);
      delay(STEP_INCREMENT_MM * 10 / MOTOR_SPEED_NORMAL);
      stopMotors();
      
      // Stazionamento 5 secondi
      Serial.println(F("[WAIT] Stazionamento 5s su cella blu..."));
      lcd.setCursor(0, 2);
      lcd.print(F("CELLA BLU - WAIT 5s"));
      
      for (int i = 0; i < 5; i++) {
        wdt_reset();
        delay(1000);
      }
      
      lcd.setCursor(0, 2);
      lcd.print(F("                    "));
      
      // Invia notifica a Raspberry per blacklist
      sendToRaspberry(CODE_BLUE_TILE);
      
      // Prosegui movimento
      continue;
    }
    
    if (detectBlackTile() || detectRedTile() || detectSlope()) {
      Serial.println(F("[WARN] Ostacolo rilevato, torno indietro"));
      stopMotors();
      moveBackward(traveled);
      return false;
    }
    
    // Avanza step
    setMotorSpeed(MOTOR_SPEED_NORMAL, MOTOR_SPEED_NORMAL);
    delay(STEP_INCREMENT_MM * 10 / MOTOR_SPEED_NORMAL);
  }
  
  stopMotors();
  return true;
}

bool detectSlope() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  float angleX = atan2(ay, az) * 180 / PI;
  float angleY = atan2(ax, az) * 180 / PI;
  
  return (abs(angleX) > SLOPE_THRESHOLD_ANGLE || abs(angleY) > SLOPE_THRESHOLD_ANGLE);
}

void handleWaitConfirm() {
  Serial.println(F("[STATE] WAIT_CONFIRM"));
  
  String cmd = "";
  while (cmd == "") {
    wdt_reset();
    cmd = readSerialCommand();
    sendHeartbeat();
    delay(10);
  }
  
  currentState = CALIBRATION;
}

// =================== CONTROLLO MOTORI ===================

void setMotorSpeed(int leftSpeed, int rightSpeed) {
  digitalWrite(MOTOR_LEFT_IN1, HIGH);
  digitalWrite(MOTOR_LEFT_IN2, LOW);
  analogWrite(MOTOR_LEFT_ENA, leftSpeed);
  
  digitalWrite(MOTOR_RIGHT_IN3, HIGH);
  digitalWrite(MOTOR_RIGHT_IN4, LOW);
  analogWrite(MOTOR_RIGHT_ENB, rightSpeed);
}

void stopMotors() {
  analogWrite(MOTOR_LEFT_ENA, 0);
  analogWrite(MOTOR_RIGHT_ENB, 0);
}

void turnRight90() {
  Serial.println(F("[TURN] Destra 90°"));
  digitalWrite(MOTOR_LEFT_IN1, HIGH);
  digitalWrite(MOTOR_LEFT_IN2, LOW);
  digitalWrite(MOTOR_RIGHT_IN3, LOW);
  digitalWrite(MOTOR_RIGHT_IN4, HIGH);
  analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_TURN);
  analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_TURN);
  delay(1000);  // Calibrare tempo
  stopMotors();
}

void turnLeft90() {
  Serial.println(F("[TURN] Sinistra 90°"));
  digitalWrite(MOTOR_LEFT_IN1, LOW);
  digitalWrite(MOTOR_LEFT_IN2, HIGH);
  digitalWrite(MOTOR_RIGHT_IN3, HIGH);
  digitalWrite(MOTOR_RIGHT_IN4, LOW);
  analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_TURN);
  analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_TURN);
  delay(1000);  // Calibrare tempo
  stopMotors();
}

void moveBackward(int distance_mm) {
  Serial.print(F("[MOVE] Indietro "));
  Serial.print(distance_mm);
  Serial.println(F(" mm"));
  
  digitalWrite(MOTOR_LEFT_IN1, LOW);
  digitalWrite(MOTOR_LEFT_IN2, HIGH);
  digitalWrite(MOTOR_RIGHT_IN3, LOW);
  digitalWrite(MOTOR_RIGHT_IN4, HIGH);
  analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_NORMAL);
  analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_NORMAL);
  
  delay(distance_mm * 10 / MOTOR_SPEED_NORMAL);
  stopMotors();
}

void moveLeft(int adjustment_mm) {
  Serial.print(F("[CALIB] Sinistra "));
  Serial.print(adjustment_mm);
  Serial.println(F(" mm"));
  
  digitalWrite(MOTOR_LEFT_IN1, LOW);
  digitalWrite(MOTOR_LEFT_IN2, HIGH);
  digitalWrite(MOTOR_RIGHT_IN3, HIGH);
  digitalWrite(MOTOR_RIGHT_IN4, LOW);
  analogWrite(MOTOR_LEFT_ENA, 80);
  analogWrite(MOTOR_RIGHT_ENB, 80);
  delay(adjustment_mm * 5);
  stopMotors();
}

void moveRight(int adjustment_mm) {
  Serial.print(F("[CALIB] Destra "));
  Serial.print(adjustment_mm);
  Serial.println(F(" mm"));
  
  digitalWrite(MOTOR_LEFT_IN1, HIGH);
  digitalWrite(MOTOR_LEFT_IN2, LOW);
  digitalWrite(MOTOR_RIGHT_IN3, LOW);
  digitalWrite(MOTOR_RIGHT_IN4, HIGH);
  analogWrite(MOTOR_LEFT_ENA, 80);
  analogWrite(MOTOR_RIGHT_ENB, 80);
  delay(adjustment_mm * 5);
  stopMotors();
}
