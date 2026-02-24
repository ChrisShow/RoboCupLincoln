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
#define MOTOR_LEFT_ENA 3
#define MOTOR_LEFT_IN1 44
#define MOTOR_LEFT_IN2 45

// Motor Driver L298N - LATO DESTRO
#define MOTOR_RIGHT_ENB 4
#define MOTOR_RIGHT_IN3 46
#define MOTOR_RIGHT_IN4 47

// Giroscopio MPU6050 (I2C: SDA=20, SCL=21)

// LCD I2C (I2C: SDA=20, SCL=21)

// =================== COSTANTI ===================

// Labirinto
#define TILE_SIZE 300
#define ROBOT_SIZE 200
#define WALL_DISTANCE_THRESHOLD 70
#define CENTER_TOLERANCE 10
#define ANGLE_TOLERANCE 3

// Calibrazione
#define CALIBRATION_SAMPLES 5
#define VL53L0X_WALL_THRESHOLD 70

// Movimento
#define STEP_DISTANCE 30
#define STEPS_PER_TILE 10
#define MOTOR_SPEED_NORMAL 150
#define MOTOR_SPEED_SLOW 100
#define ROTATION_DELAY_90 500

// Sensore colore
#define BLACK_THRESHOLD 50
#define RED_THRESHOLD 80

// Giroscopio
#define TILT_THRESHOLD 17

// Comunicazione seriale
#define SERIAL_BAUD 115200
#define SERIAL_TIMEOUT 5000

// Heartbeat e Watchdog
#define HEARTBEAT_INTERVAL 1000      // Invia heartbeat ogni 1s
#define WATCHDOG_TIMEOUT 5000         // Timeout dopo 5s senza heartbeat

// Filtro sensori
#define FILTER_SIZE 5

// Codici direzione
#define DIR_TERMINA 0
#define DIR_DRITTO 1
#define DIR_DESTRA 2
#define DIR_SINISTRA 3
#define DIR_DIETRO 4

// Codici rilevazione
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

// =================== CLASSE FILTRO SENSORI ===================

class SensorFilter {
  private:
    uint16_t buffer[FILTER_SIZE];
    int index;
    int count;
  
  public:
    SensorFilter() : index(0), count(0) {
      for (int i = 0; i < FILTER_SIZE; i++) buffer[i] = 0;
    }
    
    uint16_t addReading(uint16_t value) {
      buffer[index] = value;
      index = (index + 1) % FILTER_SIZE;
      if (count < FILTER_SIZE) count++;
      
      // Calcola media
      uint32_t sum = 0;
      for (int i = 0; i < count; i++) {
        sum += buffer[i];
      }
      return sum / count;
    }
    
    void reset() {
      index = 0;
      count = 0;
      for (int i = 0; i < FILTER_SIZE; i++) buffer[i] = 0;
    }
};

// =================== OGGETTI GLOBALI ===================

VL53L0X sensors[NUM_VL53L0X];
uint8_t sensorAddresses[NUM_VL53L0X] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

LiquidCrystal_I2C lcd(0x27, 20, 4);
Servo servoCamera;
Servo servoMedikit;
MPU6050 mpu;

// Filtri per ultrasuoni
SensorFilter filterFront, filterRight, filterLeft;

// =================== VARIABILI GLOBALI ===================

volatile bool systemRunning = false;
int currentDirection = 0;

// Heartbeat e Watchdog
unsigned long lastHeartbeatSent = 0;
unsigned long lastHeartbeatReceived = 0;
bool raspberryAlive = true;

// Posizione camera
enum CameraPosition {
  CAM_FRONT,
  CAM_RIGHT,
  CAM_LEFT
};
CameraPosition currentCameraPos = CAM_FRONT;

// =================== STRUTTURE DATI ===================

struct SensorReadings {
  uint16_t frontLeft1, frontLeft2;
  uint16_t frontRight1, frontRight2;
  uint16_t backRight1, backRight2;
  uint16_t backLeft1, backLeft2;
  
  uint16_t usFront, usRight, usLeft;
  
  float pitch, roll;
  
  int colorR, colorG, colorB;
};

SensorReadings sensorData;

// =================== SETUP ===================

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.setTimeout(SERIAL_TIMEOUT);
  
  Wire.begin();
  Wire.setClock(400000);
  
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Ultrasuoni
  pinMode(US_FRONT_TRIG, OUTPUT);
  pinMode(US_FRONT_ECHO, INPUT);
  pinMode(US_RIGHT_TRIG, OUTPUT);
  pinMode(US_RIGHT_ECHO, INPUT);
  pinMode(US_LEFT_TRIG, OUTPUT);
  pinMode(US_LEFT_ECHO, INPUT);
  
  // Sensore colore
  pinMode(TCS_S0, OUTPUT);
  pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT);
  pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S0, HIGH);
  digitalWrite(TCS_S1, LOW);
  
  // Motori
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
  lcd.print("RoboCup 2026");
  lcd.setCursor(0, 1);
  lcd.print("Inizializzazione...");
  delay(1000);
  
  // Servo
  servoCamera.attach(SERVO_CAMERA_PIN);
  servoMedikit.attach(SERVO_MEDIKIT_PIN);
  servoCamera.write(CAMERA_CENTER);
  delay(500);
  
  // VL53L0X
  initVL53L0X();
  
  // MPU6050
  mpu.initialize();
  if (!mpu.testConnection()) {
    lcd.setCursor(0, 2);
    lcd.print("ERR: MPU6050");
    delay(2000);
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Pronto");
  lcd.setCursor(0, 1);
  lcd.print("Premi START");
  
  blinkReady();
  
  logInfo("Sistema inizializzato");
}

// =================== LOOP PRINCIPALE ===================

void loop() {
  // Gestione Heartbeat
  handleHeartbeat();
  
  // Controlla watchdog Raspberry
  if (systemRunning && raspberryAlive == false) {
    emergencyStop();
    lcd.setCursor(0, 2);
    lcd.print("TIMEOUT RASPBERRY!");
    delay(5000);
    raspberryAlive = true;  // Reset per riprova
  }
  
  // Gestione pulsante START
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(START_BUTTON_PIN);
  
  if (lastButtonState == HIGH && buttonState == LOW) {
    delay(50);
    
    if (!systemRunning) {
      startSystem();
    } else {
      stopSystem();
    }
  }
  
  lastButtonState = buttonState;
  
  // Ciclo principale se attivo
  if (systemRunning) {
    mainCycle();
  }
  
  delay(50);
}

// =================== HEARTBEAT E WATCHDOG ===================

void handleHeartbeat() {
  // Invia heartbeat periodicamente
  if (millis() - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
    Serial.println("HB");
    lastHeartbeatSent = millis();
  }
  
  // Controlla timeout Raspberry
  if (systemRunning && (millis() - lastHeartbeatReceived > WATCHDOG_TIMEOUT)) {
    raspberryAlive = false;
  } else {
    raspberryAlive = true;
  }
}

void updateHeartbeat() {
  lastHeartbeatReceived = millis();
  raspberryAlive = true;
}

// =================== CICLO PRINCIPALE ROBOT ===================

void mainCycle() {
  static enum State {
    STATE_CALIBRATION,
    STATE_SCAN_FRONT,
    STATE_SCAN_RIGHT,
    STATE_SCAN_LEFT,
    STATE_WAIT_DIRECTION,
    STATE_MOVE,
    STATE_WAIT_CONFIRM
  };
  
  static State currentState = STATE_CALIBRATION;
  
  switch (currentState) {
    case STATE_CALIBRATION:
      autocalibrazione();
      currentState = STATE_SCAN_FRONT;
      break;
      
    case STATE_SCAN_FRONT:
      scanDirection("FRONT");
      currentState = STATE_SCAN_RIGHT;
      break;
      
    case STATE_SCAN_RIGHT:
      scanDirection("RIGHT");
      currentState = STATE_SCAN_LEFT;
      break;
      
    case STATE_SCAN_LEFT:
      scanDirection("LEFT");
      servoCamera.write(CAMERA_CENTER);
      delay(500);
      Serial.println("1");  // Segnala fine scansione
      currentState = STATE_WAIT_DIRECTION;
      break;
      
    case STATE_WAIT_DIRECTION:
      {
        int direction = waitForDirection();
        
        if (direction == DIR_TERMINA) {
          stopSystem();
          currentState = STATE_CALIBRATION;
        } else {
          executeMovement(direction);
          currentState = STATE_WAIT_CONFIRM;
        }
      }
      break;
      
    case STATE_WAIT_CONFIRM:
      {
        int confirm = waitForConfirm();
        if (confirm == 1) {
          currentState = STATE_CALIBRATION;  // Nuovo ciclo
        }
      }
      break;
  }
}

// =================== SCANSIONE DIREZIONI ===================

void scanDirection(String dirName) {
  logInfo("Scansione " + dirName);
  
  // Autocalibrazione
  autocalibrazione();
  
  // Posiziona camera
  if (dirName == "FRONT") {
    servoCamera.write(CAMERA_CENTER);
    delay(500);
  } else if (dirName == "RIGHT") {
    servoCamera.write(CAMERA_RIGHT);
    delay(500);
  } else if (dirName == "LEFT") {
    servoCamera.write(CAMERA_LEFT);
    delay(500);
  }
  
  // Rileva muro
  uint16_t distance = 0;
  
  if (dirName == "FRONT") {
    distance = readUltrasonicFiltered(US_FRONT_TRIG, US_FRONT_ECHO, filterFront);
  } else if (dirName == "RIGHT") {
    distance = readUltrasonicFiltered(US_RIGHT_TRIG, US_RIGHT_ECHO, filterRight);
  } else if (dirName == "LEFT") {
    distance = readUltrasonicFiltered(US_LEFT_TRIG, US_LEFT_ECHO, filterLeft);
  }
  
  logDebug("Distanza: " + String(distance) + " mm");
  
  // Determina presenza muro
  int wallPresent = (distance < WALL_DISTANCE_THRESHOLD && distance > 20) ? 1 : 0;
  
  // Invia a Raspberry
  Serial.println(wallPresent);
  
  // Attendi risposta (codice rilevazione)
  int code = waitForDetectionCode();
  
  // Gestisci codice
  handleDetectionCode(code);
}

// =================== LETTURA SENSORI CON FILTRO ===================

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

uint16_t readUltrasonicFiltered(uint8_t trigPin, uint8_t echoPin, SensorFilter& filter) {
  uint16_t raw = readUltrasonic(trigPin, echoPin);
  uint16_t filtered = filter.addReading(raw);
  
  logDebug("US raw: " + String(raw) + " filtered: " + String(filtered));
  
  return filtered;
}

void readAllVL53L0X() {
  sensorData.frontLeft1 = sensors[0].readRangeSingleMillimeters();
  sensorData.frontLeft2 = sensors[1].readRangeSingleMillimeters();
  sensorData.frontRight1 = sensors[2].readRangeSingleMillimeters();
  sensorData.frontRight2 = sensors[3].readRangeSingleMillimeters();
  sensorData.backRight1 = sensors[4].readRangeSingleMillimeters();
  sensorData.backRight2 = sensors[5].readRangeSingleMillimeters();
  sensorData.backLeft1 = sensors[6].readRangeSingleMillimeters();
  sensorData.backLeft2 = sensors[7].readRangeSingleMillimeters();
}

// =================== GESTIONE CODICI RILEVAZIONE ===================

void handleDetectionCode(int code) {
  switch (code) {
    case CODE_ROSSO:
      lcd.setCursor(0, 2);
      lcd.print("ROSSO            ");
      delay(2000);
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      break;
      
    case CODE_GIALLO:
      lcd.setCursor(0, 2);
      lcd.print("GIALLO           ");
      delay(2000);
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      break;
      
    case CODE_VERDE:
      lcd.setCursor(0, 2);
      lcd.print("VERDE            ");
      delay(2000);
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      break;
      
    case CODE_PHI:
      deliverMedikit("Harmed", 2);
      break;
      
    case CODE_PSI:
      deliverMedikit("Secured", 1);
      break;
      
    case CODE_OMEGA:
      lcd.setCursor(0, 2);
      lcd.print("Unharmed         ");
      delay(5000);
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      break;
      
    case CODE_NESSUNA:
    default:
      // Nessuna azione
      break;
  }
}

void deliverMedikit(String victimType, int rotations) {
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print(victimType);
  
  // Ruota 180° (direzione opposta)
  rotate180();
  
  // Rilascia medikit
  for (int i = 0; i < rotations; i++) {
    servoMedikit.write(90);
    delay(500);
    servoMedikit.write(0);
    delay(500);
  }
  
  // Torna in posizione originale
  rotate180();
  
  // Autocalibrazione
  autocalibrazione();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Medikit consegnato");
  delay(2000);
}

// =================== MOVIMENTO ===================

void executeMovement(int direction) {
  logInfo("Movimento: " + String(direction));
  
  switch (direction) {
    case DIR_DRITTO:
      moveForwardTile();
      break;
      
    case DIR_DESTRA:
      rotate90(true);
      moveForwardTile();
      currentDirection++;
      break;
      
    case DIR_SINISTRA:
      rotate90(false);
      moveForwardTile();
      currentDirection--;
      break;
      
    case DIR_DIETRO:
      rotate180();
      moveForwardTile();
      currentDirection += 2;
      break;
  }
}

void moveForwardTile() {
  bool success = true;
  
  for (int step = 0; step < STEPS_PER_TILE; step++) {
    // Controlla colore pavimento
    readColorSensor();
    
    if (isBlackTile() || isRedTile()) {
      logError("Cella non percorribile rilevata");
      
      // Torna indietro
      for (int back = 0; back < step; back++) {
        moveBackwardStep();
      }
      
      Serial.println("0");  // Movimento fallito
      success = false;
      return;
    }
    
    // Controlla inclinazione
    readGyroscope();
    
    if (abs(sensorData.pitch) > TILT_THRESHOLD || abs(sensorData.roll) > TILT_THRESHOLD) {
      logError("Pendenza rilevata");
      
      // Torna indietro
      for (int back = 0; back < step; back++) {
        moveBackwardStep();
      }
      
      Serial.println("0");
      success = false;
      return;
    }
    
    // Avanza di uno step
    moveForwardStep();
  }
  
  if (success) {
    Serial.println("1");  // Movimento riuscito
  }
}

void moveForwardStep() {
  setMotorSpeed(MOTOR_SPEED_NORMAL, MOTOR_SPEED_NORMAL);
  delay(STEP_DISTANCE * 10);  // Tempo proporzionale alla distanza
  stopMotors();
  delay(50);
}

void moveBackwardStep() {
  setMotorSpeed(-MOTOR_SPEED_NORMAL, -MOTOR_SPEED_NORMAL);
  delay(STEP_DISTANCE * 10);
  stopMotors();
  delay(50);
}

void rotate90(bool clockwise) {
  if (clockwise) {
    setMotorSpeed(MOTOR_SPEED_NORMAL, -MOTOR_SPEED_NORMAL);
  } else {
    setMotorSpeed(-MOTOR_SPEED_NORMAL, MOTOR_SPEED_NORMAL);
  }
  
  delay(ROTATION_DELAY_90);
  stopMotors();
  delay(100);
}

void rotate180() {
  setMotorSpeed(MOTOR_SPEED_NORMAL, -MOTOR_SPEED_NORMAL);
  delay(ROTATION_DELAY_90 * 2);
  stopMotors();
  delay(100);
}

// =================== CONTROLLO MOTORI ===================

void setMotorSpeed(int leftSpeed, int rightSpeed) {
  // Sinistra
  if (leftSpeed > 0) {
    digitalWrite(MOTOR_LEFT_IN1, HIGH);
    digitalWrite(MOTOR_LEFT_IN2, LOW);
    analogWrite(MOTOR_LEFT_ENA, leftSpeed);
  } else if (leftSpeed < 0) {
    digitalWrite(MOTOR_LEFT_IN1, LOW);
    digitalWrite(MOTOR_LEFT_IN2, HIGH);
    analogWrite(MOTOR_LEFT_ENA, -leftSpeed);
  } else {
    digitalWrite(MOTOR_LEFT_IN1, LOW);
    digitalWrite(MOTOR_LEFT_IN2, LOW);
    analogWrite(MOTOR_LEFT_ENA, 0);
  }
  
  // Destra
  if (rightSpeed > 0) {
    digitalWrite(MOTOR_RIGHT_IN3, HIGH);
    digitalWrite(MOTOR_RIGHT_IN4, LOW);
    analogWrite(MOTOR_RIGHT_ENB, rightSpeed);
  } else if (rightSpeed < 0) {
    digitalWrite(MOTOR_RIGHT_IN3, LOW);
    digitalWrite(MOTOR_RIGHT_IN4, HIGH);
    analogWrite(MOTOR_RIGHT_ENB, -rightSpeed);
  } else {
    digitalWrite(MOTOR_RIGHT_IN3, LOW);
    digitalWrite(MOTOR_RIGHT_IN4, LOW);
    analogWrite(MOTOR_RIGHT_ENB, 0);
  }
}

void stopMotors() {
  setMotorSpeed(0, 0);
}

// =================== SENSORI ===================

void readColorSensor() {
  // Leggi R
  digitalWrite(TCS_S2, LOW);
  digitalWrite(TCS_S3, LOW);
  sensorData.colorR = pulseIn(TCS_OUT, LOW);
  
  // Leggi G
  digitalWrite(TCS_S2, HIGH);
  digitalWrite(TCS_S3, HIGH);
  sensorData.colorG = pulseIn(TCS_OUT, LOW);
  
  // Leggi B
  digitalWrite(TCS_S2, LOW);
  digitalWrite(TCS_S3, HIGH);
  sensorData.colorB = pulseIn(TCS_OUT, LOW);
}

bool isBlackTile() {
  return (sensorData.colorR < BLACK_THRESHOLD && 
          sensorData.colorG < BLACK_THRESHOLD && 
          sensorData.colorB < BLACK_THRESHOLD);
}

bool isRedTile() {
  return (sensorData.colorR > RED_THRESHOLD && 
          sensorData.colorG < RED_THRESHOLD && 
          sensorData.colorB < RED_THRESHOLD);
}

void readGyroscope() {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  
  // Calcola angoli (semplificato)
  sensorData.pitch = atan2(ay, sqrt(ax*ax + az*az)) * 180.0 / PI;
  sensorData.roll = atan2(ax, sqrt(ay*ay + az*az)) * 180.0 / PI;
}

// =================== CALIBRAZIONE ===================

void autocalibrazione() {
  logDebug("Autocalibrazione...");
  
  readAllVL53L0X();
  
  // Verifica allineamento lato frontale
  int16_t diffFront = sensorData.frontLeft1 - sensorData.frontRight1;
  
  if (abs(diffFront) > CENTER_TOLERANCE) {
    // Correggi rotazione
    if (diffFront > 0) {
      // Ruota leggermente a destra
      setMotorSpeed(MOTOR_SPEED_SLOW, -MOTOR_SPEED_SLOW);
      delay(50);
      stopMotors();
    } else {
      // Ruota leggermente a sinistra
      setMotorSpeed(-MOTOR_SPEED_SLOW, MOTOR_SPEED_SLOW);
      delay(50);
      stopMotors();
    }
  }
  
  logDebug("Calibrazione completata");
}

// =================== COMUNICAZIONE ===================

int waitForDirection() {
  unsigned long startWait = millis();
  
  while (millis() - startWait < WATCHDOG_TIMEOUT) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      
      if (input == "HB") {
        updateHeartbeat();
        continue;
      }
      
      int value = input.toInt();
      logInfo("Direzione ricevuta: " + String(value));
      return value;
    }
    
    delay(10);
  }
  
  logError("Timeout attesa direzione");
  return DIR_TERMINA;
}

int waitForDetectionCode() {
  unsigned long startWait = millis();
  
  while (millis() - startWait < WATCHDOG_TIMEOUT) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      
      if (input == "HB") {
        updateHeartbeat();
        continue;
      }
      
      int value = input.toInt();
      logDebug("Codice rilevazione: " + String(value));
      return value;
    }
    
    delay(10);
  }
  
  logError("Timeout attesa codice");
  return CODE_NESSUNA;
}

int waitForConfirm() {
  unsigned long startWait = millis();
  
  while (millis() - startWait < WATCHDOG_TIMEOUT) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      
      if (input == "HB") {
        updateHeartbeat();
        continue;
      }
      
      int value = input.toInt();
      return value;
    }
    
    delay(10);
  }
  
  return 0;
}

// =================== SISTEMA ===================

void startSystem() {
  systemRunning = true;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SISTEMA ATTIVO");
  
  digitalWrite(LED_PIN, HIGH);
  
  // Reset filtri
  filterFront.reset();
  filterRight.reset();
  filterLeft.reset();
  
  // Reset heartbeat
  lastHeartbeatReceived = millis();
  raspberryAlive = true;
  
  logInfo("Sistema avviato");
}

void stopSystem() {
  systemRunning = false;
  
  stopMotors();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Fermato");
  lcd.setCursor(0, 1);
  lcd.print("Premi START");
  
  digitalWrite(LED_PIN, LOW);
  
  logInfo("Sistema fermato");
}

void emergencyStop() {
  stopMotors();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("STOP DI EMERGENZA");
  
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(200);
  }
  
  digitalWrite(LED_PIN, LOW);
  
  logError("Stop di emergenza");
}

// =================== UTILITY ===================

void blinkReady() {
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(150);
  }
  digitalWrite(LED_PIN, LOW);
}

void initVL53L0X() {
  for (int i = 0; i < NUM_VL53L0X; i++) {
    pinMode(XSHUT_PIN_START + i, OUTPUT);
    digitalWrite(XSHUT_PIN_START + i, LOW);
  }
  delay(10);
  
  for (int i = 0; i < NUM_VL53L0X; i++) {
    digitalWrite(XSHUT_PIN_START + i, HIGH);
    delay(10);
    
    sensors[i].setTimeout(500);
    if (!sensors[i].init()) {
      lcd.setCursor(0, 2);
      lcd.print("ERR: VL53 #");
      lcd.print(i);
      delay(2000);
    }
    
    sensors[i].setAddress(sensorAddresses[i]);
    sensors[i].setMeasurementTimingBudget(20000);
  }
}

void logInfo(String message) {
  Serial.print("[INFO] ");
  Serial.println(message);
}

void logDebug(String message) {
  Serial.print("[DEBUG] ");
  Serial.println(message);
}

void logError(String message) {
  Serial.print("[ERROR] ");
  Serial.println(message);
}