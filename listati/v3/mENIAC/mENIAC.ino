#include <Wire.h>
#include <VL53L0X.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <MPU6050.h>

// =================== CONFIGURAZIONE PIN ===================

// Pulsante START e LED
#define START_BUTTON_PIN 2
#define LED_PIN 13

// Sensori VL53L0X (8 sensori, 2 per lato)
#define NUM_VL53L0X 8
#define XSHUT_PIN_START 22  // Pin 22-29 per XSHUT

// Sensori ultrasuoni
#define US_FRONT_TRIG 30
#define US_FRONT_ECHO 31
#define US_RIGHT_TRIG 32
#define US_RIGHT_ECHO 33
#define US_LEFT_TRIG 34
#define US_LEFT_ECHO 35

// Sensore colore TCS3200
#define TCS_S0 36
#define TCS_S1 37
#define TCS_S2 38
#define TCS_S3 39
#define TCS_OUT 40

// Servo motori
#define SERVO_CAMERA_PIN 9
#define SERVO_MEDIKIT_PIN 10

// Motor Driver L298N - LATO SINISTRO
#define MOTOR_LEFT_ENA 3   // PWM
#define MOTOR_LEFT_IN1 24
#define MOTOR_LEFT_IN2 25

// Motor Driver L298N - LATO DESTRO
#define MOTOR_RIGHT_ENB 4  // PWM
#define MOTOR_RIGHT_IN3 26
#define MOTOR_RIGHT_IN4 27

// =================== COSTANTI ===================

// Labirinto
#define TILE_SIZE 300              // mm
#define ROBOT_SIZE 200             // mm
#define WALL_DISTANCE_THRESHOLD 70 // mm - distanza max per rilevare muro
#define CENTER_TOLERANCE 10        // mm
#define ANGLE_TOLERANCE 3          // gradi

// Calibrazione
#define CALIBRATION_SAMPLES 5
#define VL53L0X_WALL_THRESHOLD 70  // mm

// Movimento
#define STEP_DISTANCE 30           // mm per step
#define STEPS_PER_TILE 10          // 300mm / 30mm
#define MOTOR_SPEED_NORMAL 150     // PWM 0-255
#define MOTOR_SPEED_SLOW 100       // Per calibrazione
#define ROTATION_DELAY_90 500      // ms per ruotare 90°

// Sensore colore
#define BLACK_THRESHOLD 50         // Soglia per rilevare nero
#define RED_THRESHOLD 80           // Soglia per rilevare rosso

// Giroscopio
#define TILT_THRESHOLD 17          // gradi - pendenza massima

// Comunicazione seriale
#define SERIAL_BAUD 115200
#define SERIAL_TIMEOUT 5000

// Codici direzione (da Raspberry)
#define DIR_TERMINA 0
#define DIR_DRITTO 1
#define DIR_DESTRA 2
#define DIR_SINISTRA 3
#define DIR_DIETRO 4

// Codici rilevazione (per Raspberry)
#define CODE_NESSUNA 0
#define CODE_ROSSO 1
#define CODE_GIALLO 2
#define CODE_VERDE 3
#define CODE_PHI 4
#define CODE_PSI 5
#define CODE_OMEGA 6

// Posizioni servo camera
#define CAMERA_CENTER 90
#define CAMERA_RIGHT 0
#define CAMERA_LEFT 180

// =================== OGGETTI GLOBALI ===================

VL53L0X sensors[NUM_VL53L0X];
uint8_t sensorAddresses[NUM_VL53L0X] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

LiquidCrystal_I2C lcd(0x27, 20, 4);  // Prova 0x27, se non funziona usa 0x3F
Servo servoCamera;
Servo servoMedikit;
MPU6050 mpu;

// =================== STRUTTURE DATI ===================

struct SensorReadings {
  // VL53L0X: 2 sensori per lato (Front-Left, Front-Right, Back-Right, Back-Left)
  uint16_t frontLeft1, frontLeft2;
  uint16_t frontRight1, frontRight2;
  uint16_t backRight1, backRight2;
  uint16_t backLeft1, backLeft2;
  
  // Ultrasuoni
  uint16_t usFront, usRight, usLeft;
  
  // Giroscopio
  float pitch, roll;
  
  // Sensore colore
  int colorR, colorG, colorB;
};

SensorReadings sensorData;

// =================== VARIABILI GLOBALI ===================

volatile bool systemRunning = false;
volatile bool buttonPressed = false;

int currentDirection = 0;  // 0=iniziale, poi ruota in base ai movimenti

enum CameraPosition {
  CAM_FRONT,
  CAM_RIGHT,
  CAM_LEFT
};

CameraPosition currentCameraPos = CAM_FRONT;

// =================== SETUP ===================

void setup() {
  // Comunicazione seriale con Raspberry Pi
  Serial.begin(SERIAL_BAUD);
  Serial.setTimeout(SERIAL_TIMEOUT);
  
  Wire.begin();
  Wire.setClock(400000);
  
  // Pin pulsante e LED
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Pin ultrasuoni
  pinMode(US_FRONT_TRIG, OUTPUT);
  pinMode(US_FRONT_ECHO, INPUT);
  pinMode(US_RIGHT_TRIG, OUTPUT);
  pinMode(US_RIGHT_ECHO, INPUT);
  pinMode(US_LEFT_TRIG, OUTPUT);
  pinMode(US_LEFT_ECHO, INPUT);
  
  // Pin sensore colore TCS3200
  pinMode(TCS_S0, OUTPUT);
  pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT);
  pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  
  // Frequenza scaling TCS3200 (20%)
  digitalWrite(TCS_S0, HIGH);
  digitalWrite(TCS_S1, LOW);
  
  // Pin motori
  pinMode(MOTOR_LEFT_ENA, OUTPUT);
  pinMode(MOTOR_LEFT_IN1, OUTPUT);
  pinMode(MOTOR_LEFT_IN2, OUTPUT);
  pinMode(MOTOR_RIGHT_ENB, OUTPUT);
  pinMode(MOTOR_RIGHT_IN3, OUTPUT);
  pinMode(MOTOR_RIGHT_IN4, OUTPUT);
  
  stopMotors();
  
  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RoboCup Rescue 2026");
  lcd.setCursor(0, 1);
  lcd.print("Inizializzazione...");
  
  // Servo motori
  servoCamera.attach(SERVO_CAMERA_PIN);
  servoMedikit.attach(SERVO_MEDIKIT_PIN);
  servoCamera.write(CAMERA_CENTER);
  servoMedikit.write(0);  // Posizione di riposo
  
  // Sensori VL53L0X
  initVL53L0X();
  
  // Giroscopio MPU6050
  mpu.initialize();
  if (!mpu.testConnection()) {
    lcd.setCursor(0, 2);
    lcd.print("ERR: Giroscopio!");
    blinkError();
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Pronto!");
  lcd.setCursor(0, 1);
  lcd.print("Premi START");
  
  blinkReady();
  
  Serial.println("ARDUINO_READY");
}

// =================== LOOP PRINCIPALE ===================

void loop() {
  // Attendi pressione pulsante
  if (digitalRead(START_BUTTON_PIN) == LOW && !systemRunning) {
    delay(50);  // Debounce
    if (digitalRead(START_BUTTON_PIN) == LOW) {
      systemRunning = true;
      digitalWrite(LED_PIN, HIGH);
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Sistema Attivo!");
      
      delay(1000);
      
      // Esegui ciclo principale
      mainNavigationLoop();
    }
  }
  
  // Pulsante per stop emergenza
  if (digitalRead(START_BUTTON_PIN) == LOW && systemRunning) {
    delay(50);
    if (digitalRead(START_BUTTON_PIN) == LOW) {
      emergencyStop();
    }
  }
  
  delay(100);
}

// =================== CICLO NAVIGAZIONE PRINCIPALE ===================

void mainNavigationLoop() {
  while (systemRunning) {
    // FASE 1: Autocalibrazione
    performAutoAlignment();
    
    // FASE 2: Scansione FRONTE
    positionCamera(CAM_FRONT);
    scanDirection("FRONT");
    
    // FASE 3: Scansione DESTRA
    positionCamera(CAM_RIGHT);
    scanDirection("RIGHT");
    
    // FASE 4: Scansione SINISTRA
    positionCamera(CAM_LEFT);
    scanDirection("LEFT");
    
    // FASE 5: Riposiziona camera al centro
    positionCamera(CAM_FRONT);
    performAutoAlignment();
    
    // FASE 6: Notifica Raspberry che siamo pronti
    Serial.println("1");  // Segnale "pronto per direzione"
    
    // FASE 7: Attendi direzione da Raspberry
    int direction = waitForDirection();
    
    if (direction == DIR_TERMINA) {
      // Fine esplorazione
      systemRunning = false;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Esplorazione");
      lcd.setCursor(0, 1);
      lcd.print("Completata!");
      break;
    }
    
    // FASE 8: Esegui movimento
    bool movementSuccess = executeMovement(direction);
    
    // FASE 9: Invia esito a Raspberry
    if (movementSuccess) {
      Serial.println("1");  // Movimento riuscito
    } else {
      Serial.println("0");  // Movimento fallito (cella nera/rossa/pendenza)
      
      // Se fallito, Raspberry calcolerà nuova direzione
      direction = waitForDirection();
      
      if (direction == DIR_TERMINA) {
        systemRunning = false;
        break;
      }
      
      // Riprova con nuova direzione
      continue;
    }
    
    // Piccola pausa tra cicli
    delay(200);
  }
  
  digitalWrite(LED_PIN, LOW);
}

// =================== SCANSIONE DIREZIONE ===================

void scanDirection(String dirName) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scansione: " + dirName);
  
  // Leggi sensore ultrasuoni appropriato
  uint16_t distance;
  
  if (dirName == "FRONT") {
    distance = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  } else if (dirName == "RIGHT") {
    distance = readUltrasonic(US_RIGHT_TRIG, US_RIGHT_ECHO);
  } else if (dirName == "LEFT") {
    distance = readUltrasonic(US_LEFT_TRIG, US_LEFT_ECHO);
  }
  
  delay(100);
  
  // Determina se c'è muro
  bool wallPresent = (distance < WALL_DISTANCE_THRESHOLD && distance > 10);
  
  // Invia a Raspberry: 0=no muro, 1=muro presente
  Serial.println(wallPresent ? "1" : "0");
  
  // Se c'è muro, attendi codice rilevazione da Raspberry
  if (wallPresent) {
    int detectionCode = waitForDetectionCode();
    handleDetection(detectionCode, dirName);
  }
  
  delay(100);
}

// =================== GESTIONE RILEVAZIONI ===================

void handleDetection(int code, String direction) {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  switch (code) {
    case CODE_ROSSO:
      lcd.print("ROSSO");
      delay(2000);
      break;
      
    case CODE_GIALLO:
      lcd.print("GIALLO");
      delay(2000);
      break;
      
    case CODE_VERDE:
      lcd.print("VERDE");
      delay(2000);
      break;
      
    case CODE_PHI:  // Φ - Harmed (2 medikit)
      lcd.print("Harmed - PHI");
      deliverMedikit(2, direction);
      break;
      
    case CODE_PSI:  // Ψ - Secured (1 medikit)
      lcd.print("Secured - PSI");
      deliverMedikit(1, direction);
      break;
      
    case CODE_OMEGA:  // Ω - Unharmed (0 medikit)
      lcd.print("Unharmed - OMEGA");
      delay(5000);
      break;
      
    case CODE_NESSUNA:
    default:
      // Nessuna rilevazione
      break;
  }
  
  lcd.clear();
}

// =================== CONSEGNA MEDIKIT ===================

void deliverMedikit(int count, String victimDirection) {
  // Gira in direzione opposta alla vittima
  if (victimDirection == "FRONT") {
    rotateRobot(180);
  } else if (victimDirection == "RIGHT") {
    rotateRobot(-90);  // Gira a sinistra
  } else if (victimDirection == "LEFT") {
    rotateRobot(90);   // Gira a destra
  }
  
  delay(500);
  
  // Rilascia medikit
  for (int i = 0; i < count; i++) {
    servoMedikit.write(90);   // Ruota per far cadere
    delay(800);
    servoMedikit.write(0);    // Torna a riposo
    delay(500);
  }
  
  delay(500);
  
  // Torna alla posizione originale
  if (victimDirection == "FRONT") {
    rotateRobot(180);
  } else if (victimDirection == "RIGHT") {
    rotateRobot(90);
  } else if (victimDirection == "LEFT") {
    rotateRobot(-90);
  }
  
  // Autocalibrazione dopo consegna
  performAutoAlignment();
}

// =================== MOVIMENTO ===================

bool executeMovement(int direction) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Movimento...");
  
  // Prima ruota nella direzione giusta
  switch (direction) {
    case DIR_DRITTO:
      // Nessuna rotazione
      break;
      
    case DIR_DESTRA:
      rotateRobot(90);
      currentDirection = (currentDirection + 1) % 4;
      break;
      
    case DIR_SINISTRA:
      rotateRobot(-90);
      currentDirection = (currentDirection - 1 + 4) % 4;
      break;
      
    case DIR_DIETRO:
      rotateRobot(180);
      currentDirection = (currentDirection + 2) % 4;
      break;
  }
  
  delay(300);
  
  // Poi avanza di 30cm controllando ad ogni step
  for (int step = 0; step < STEPS_PER_TILE; step++) {
    // Controlla colore pavimento
    readColorSensor();
    if (isBlackTile() || isRedTile()) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Cella Bloccata!");
      
      // Torna indietro
      moveBackward(step * STEP_DISTANCE);
      return false;
    }
    
    // Controlla pendenza
    readGyroscope();
    if (isTilted()) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Pendenza!");
      
      // Torna indietro
      moveBackward(step * STEP_DISTANCE);
      return false;
    }
    
    // Avanza di uno step
    moveForward(STEP_DISTANCE);
    delay(100);
  }
  
  // Movimento completato con successo
  return true;
}

// =================== CONTROLLI SENSORI ===================

bool isBlackTile() {
  int brightness = (sensorData.colorR + sensorData.colorG + sensorData.colorB) / 3;
  return brightness < BLACK_THRESHOLD;
}

bool isRedTile() {
  // Rosso dominante
  return (sensorData.colorR > RED_THRESHOLD && 
          sensorData.colorR > sensorData.colorG * 1.5 && 
          sensorData.colorR > sensorData.colorB * 1.5);
}

bool isTilted() {
  return (abs(sensorData.pitch) > TILT_THRESHOLD || 
          abs(sensorData.roll) > TILT_THRESHOLD);
}

// =================== AUTOCALIBRAZIONE ===================

void performAutoAlignment() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrazione...");
  
  // Leggi tutti i sensori VL53L0X
  for (int i = 0; i < NUM_VL53L0X; i++) {
    uint16_t dist = sensors[i].readRangeSingleMillimeters();
    
    // Assegna alle variabili appropriate
    switch(i) {
      case 0: sensorData.frontLeft1 = dist; break;
      case 1: sensorData.frontLeft2 = dist; break;
      case 2: sensorData.frontRight1 = dist; break;
      case 3: sensorData.frontRight2 = dist; break;
      case 4: sensorData.backRight1 = dist; break;
      case 5: sensorData.backRight2 = dist; break;
      case 6: sensorData.backLeft1 = dist; break;
      case 7: sensorData.backLeft2 = dist; break;
    }
  }
  
  // Allinea frontalmente
  alignToWall(sensorData.frontLeft1, sensorData.frontLeft2, "FRONT");
  
  // Allinea lateralmente (usa i sensori del lato più vicino a un muro)
  if (sensorData.frontLeft1 < VL53L0X_WALL_THRESHOLD) {
    alignToWall(sensorData.frontLeft1, sensorData.backLeft1, "LEFT");
  } else if (sensorData.frontRight1 < VL53L0X_WALL_THRESHOLD) {
    alignToWall(sensorData.frontRight1, sensorData.backRight1, "RIGHT");
  }
  
  delay(200);
}

void alignToWall(uint16_t sensor1, uint16_t sensor2, String side) {
  int diff = abs(sensor1 - sensor2);
  
  if (diff > CENTER_TOLERANCE) {
    // Correggi angolazione
    if (sensor1 > sensor2) {
      // Ruota leggermente in senso orario
      rotateRobotSmall(2);
    } else {
      // Ruota leggermente in senso antiorario
      rotateRobotSmall(-2);
    }
  }
}

// =================== POSIZIONAMENTO CAMERA ===================

void positionCamera(CameraPosition pos) {
  switch (pos) {
    case CAM_FRONT:
      servoCamera.write(CAMERA_CENTER);
      currentCameraPos = CAM_FRONT;
      break;
      
    case CAM_RIGHT:
      servoCamera.write(CAMERA_RIGHT);
      currentCameraPos = CAM_RIGHT;
      break;
      
    case CAM_LEFT:
      servoCamera.write(CAMERA_LEFT);
      currentCameraPos = CAM_LEFT;
      break;
  }
  
  delay(500);  // Attendi movimento servo
}

// =================== LETTURA SENSORI ===================

uint16_t readUltrasonic(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 8190;
  
  return (duration * 0.343) / 2;
}

void readColorSensor() {
  // Leggi rosso
  digitalWrite(TCS_S2, LOW);
  digitalWrite(TCS_S3, LOW);
  sensorData.colorR = pulseIn(TCS_OUT, LOW);
  
  delay(10);
  
  // Leggi verde
  digitalWrite(TCS_S2, HIGH);
  digitalWrite(TCS_S3, HIGH);
  sensorData.colorG = pulseIn(TCS_OUT, LOW);
  
  delay(10);
  
  // Leggi blu
  digitalWrite(TCS_S2, LOW);
  digitalWrite(TCS_S3, HIGH);
  sensorData.colorB = pulseIn(TCS_OUT, LOW);
}

void readGyroscope() {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  
  // Calcola angoli (approssimazione semplice)
  sensorData.pitch = atan2(ay, sqrt(ax*ax + az*az)) * 180.0 / PI;
  sensorData.roll = atan2(ax, sqrt(ay*ay + az*az)) * 180.0 / PI;
}

// =================== CONTROLLO MOTORI ===================

void moveForward(int distance_mm) {
  // Calcola durata approssimativa
  int duration = distance_mm * 10;  // Calibra questo valore
  
  digitalWrite(MOTOR_LEFT_IN1, HIGH);
  digitalWrite(MOTOR_LEFT_IN2, LOW);
  analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_NORMAL);
  
  digitalWrite(MOTOR_RIGHT_IN3, HIGH);
  digitalWrite(MOTOR_RIGHT_IN4, LOW);
  analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_NORMAL);
  
  delay(duration);
  stopMotors();
}

void moveBackward(int distance_mm) {
  int duration = distance_mm * 10;
  
  digitalWrite(MOTOR_LEFT_IN1, LOW);
  digitalWrite(MOTOR_LEFT_IN2, HIGH);
  analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_NORMAL);
  
  digitalWrite(MOTOR_RIGHT_IN3, LOW);
  digitalWrite(MOTOR_RIGHT_IN4, HIGH);
  analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_NORMAL);
  
  delay(duration);
  stopMotors();
}

void rotateRobot(int degrees) {
  int duration = abs(degrees) * ROTATION_DELAY_90 / 90;
  
  if (degrees > 0) {
    // Ruota orario (destra)
    digitalWrite(MOTOR_LEFT_IN1, HIGH);
    digitalWrite(MOTOR_LEFT_IN2, LOW);
    analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_SLOW);
    
    digitalWrite(MOTOR_RIGHT_IN3, LOW);
    digitalWrite(MOTOR_RIGHT_IN4, HIGH);
    analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_SLOW);
  } else {
    // Ruota antiorario (sinistra)
    digitalWrite(MOTOR_LEFT_IN1, LOW);
    digitalWrite(MOTOR_LEFT_IN2, HIGH);
    analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_SLOW);
    
    digitalWrite(MOTOR_RIGHT_IN3, HIGH);
    digitalWrite(MOTOR_RIGHT_IN4, LOW);
    analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_SLOW);
  }
  
  delay(duration);
  stopMotors();
}

void rotateRobotSmall(int degrees) {
  // Rotazione piccola per calibrazione
  int duration = abs(degrees) * 50;  // Calibra
  
  if (degrees > 0) {
    digitalWrite(MOTOR_LEFT_IN1, HIGH);
    digitalWrite(MOTOR_LEFT_IN2, LOW);
    analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_SLOW);
    
    digitalWrite(MOTOR_RIGHT_IN3, LOW);
    digitalWrite(MOTOR_RIGHT_IN4, HIGH);
    analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_SLOW);
  } else {
    digitalWrite(MOTOR_LEFT_IN1, LOW);
    digitalWrite(MOTOR_LEFT_IN2, HIGH);
    analogWrite(MOTOR_LEFT_ENA, MOTOR_SPEED_SLOW);
    
    digitalWrite(MOTOR_RIGHT_IN3, HIGH);
    digitalWrite(MOTOR_RIGHT_IN4, LOW);
    analogWrite(MOTOR_RIGHT_ENB, MOTOR_SPEED_SLOW);
  }
  
  delay(duration);
  stopMotors();
}

void stopMotors() {
  analogWrite(MOTOR_LEFT_ENA, 0);
  analogWrite(MOTOR_RIGHT_ENB, 0);
  digitalWrite(MOTOR_LEFT_IN1, LOW);
  digitalWrite(MOTOR_LEFT_IN2, LOW);
  digitalWrite(MOTOR_RIGHT_IN3, LOW);
  digitalWrite(MOTOR_RIGHT_IN4, LOW);
}

// =================== COMUNICAZIONE SERIALE ===================

int waitForDirection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Attendo direzione...");
  
  while (!Serial.available()) {
    delay(10);
  }
  
  String input = Serial.readStringUntil('\n');
  input.trim();
  
  return input.toInt();
}

int waitForDetectionCode() {
  while (!Serial.available()) {
    delay(10);
  }
  
  String input = Serial.readStringUntil('\n');
  input.trim();
  
  return input.toInt();
}

// =================== INIZIALIZZAZIONE VL53L0X ===================

void initVL53L0X() {
  // Disabilita tutti i sensori
  for (int i = 0; i < NUM_VL53L0X; i++) {
    pinMode(XSHUT_PIN_START + i, OUTPUT);
    digitalWrite(XSHUT_PIN_START + i, LOW);
  }
  delay(10);
  
  // Abilita e configura uno alla volta
  for (int i = 0; i < NUM_VL53L0X; i++) {
    digitalWrite(XSHUT_PIN_START + i, HIGH);
    delay(10);
    
    sensors[i].setTimeout(500);
    if (!sensors[i].init()) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ERR: VL53L0X #");
      lcd.print(i);
      blinkError();
    }
    
    sensors[i].setAddress(sensorAddresses[i]);
    sensors[i].setMeasurementTimingBudget(20000);
  }
}

// =================== UTILITÀ ===================

void emergencyStop() {
  systemRunning = false;
  stopMotors();
  digitalWrite(LED_PIN, LOW);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("STOP EMERGENZA!");
  
  Serial.println("0");  // Notifica Raspberry
  
  while(true) {
    digitalWrite(LED_PIN, (millis() / 500) % 2);
    delay(100);
  }
}

void blinkReady() {
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(150);
  }
  digitalWrite(LED_PIN, LOW);
}

void blinkError() {
  while(true) {
    digitalWrite(LED_PIN, (millis() / 200) % 2);
    delay(100);
  }
}
