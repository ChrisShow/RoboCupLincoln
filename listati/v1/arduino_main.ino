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
#define MOTOR_LEFT_IN1 44
#define MOTOR_LEFT_IN2 45

// Motor Driver L298N - LATO DESTRO
#define MOTOR_RIGHT_ENB 4  // PWM
#define MOTOR_RIGHT_IN3 46
#define MOTOR_RIGHT_IN4 47

// =================== COSTANTI ===================

// Labirinto
#define TILE_SIZE 300              // mm
#define ROBOT_SIZE 200             // mm
#define WALL_DISTANCE_THRESHOLD 70 // mm
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
#define SERIAL_TIMEOUT 5000        // ms

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

// =================== VARIABILI GLOBALI ===================

volatile bool systemRunning = false;
int currentDirection = 0;  // 0=iniziale, poi ruota in base ai movimenti

enum CameraPosition {
  CAM_FRONT,
  CAM_RIGHT,
  CAM_LEFT
};

CameraPosition currentCameraPos = CAM_FRONT;

// =================== STRUTTURE DATI ===================

struct SensorReadings {
  // VL53L0X: 2 sensori per lato
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
  lcd.print("RoboCup 2026");
  lcd.setCursor(0, 1);
  lcd.print("Inizializzazione...");
  delay(1000);
  
  // Servo motori
  servoCamera.attach(SERVO_CAMERA_PIN);
  servoMedikit.attach(SERVO_MEDIKIT_PIN);
  servoCamera.write(CAMERA_CENTER);
  delay(500);
  
  // VL53L0X
  Serial.println("[INFO] Inizializzazione VL53L0X...");
  initVL53L0X();
  
  // MPU6050
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("[ERROR] MPU6050 non trovato");
    lcd.setCursor(0, 2);
    lcd.print("ERR: MPU6050");
    delay(2000);
  } else {
    Serial.println("[INFO] MPU6050 OK");
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Pronto");
  lcd.setCursor(0, 1);
  lcd.print("Premi START");
  
  blinkReady();
  
  Serial.println("[INFO] Sistema inizializzato");
  Serial.println("[INFO] Premi pulsante START per avviare");
}

// =================== LOOP PRINCIPALE ===================

void loop() {
  // Gestione pulsante START
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(START_BUTTON_PIN);
  
  if (lastButtonState == HIGH && buttonState == LOW) {
    delay(50);  // Debounce
    
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
      Serial.println("[INFO] Autocalibrazione...");
      autocalibrazione();
      currentState = STATE_SCAN_FRONT;
      break;
      
    case STATE_SCAN_FRONT:
      Serial.println("[INFO] Scansione FRONTALE");
      scanDirection("FRONT");
      currentState = STATE_SCAN_RIGHT;
      break;
      
    case STATE_SCAN_RIGHT:
      Serial.println("[INFO] Scansione DESTRA");
      scanDirection("RIGHT");
      currentState = STATE_SCAN_LEFT;
      break;
      
    case STATE_SCAN_LEFT:
      Serial.println("[INFO] Scansione SINISTRA");
      scanDirection("LEFT");
      
      // Riposiziona camera al centro
      servoCamera.write(CAMERA_CENTER);
      delay(500);
      
      // Segnala fine scansione a Raspberry
      Serial.println("1");
      
      currentState = STATE_WAIT_DIRECTION;
      break;
      
    case STATE_WAIT_DIRECTION:
      {
        int direction = waitForDirection();
        
        if (direction == DIR_TERMINA) {
          Serial.println("[INFO] Ricevuto DIR_TERMINA");
          stopSystem();
          currentState = STATE_CALIBRATION;
        } else {
          Serial.print("[INFO] Direzione ricevuta: ");
          Serial.println(direction);
          executeMovement(direction);
          currentState = STATE_WAIT_CONFIRM;
        }
      }
      break;
      
    case STATE_WAIT_CONFIRM:
      {
        int confirm = waitForConfirm();
        if (confirm == 1) {
          Serial.println("[INFO] Conferma ricevuta, nuovo ciclo");
          currentState = STATE_CALIBRATION;
        }
      }
      break;
  }
}

// =================== SCANSIONE DIREZIONI ===================

void scanDirection(String dirName) {
  // Autocalibrazione prima di ogni scansione
  autocalibrazione();
  
  // Posiziona camera
  if (dirName == "FRONT") {
    servoCamera.write(CAMERA_CENTER);
    currentCameraPos = CAM_FRONT;
  } else if (dirName == "RIGHT") {
    servoCamera.write(CAMERA_RIGHT);
    currentCameraPos = CAM_RIGHT;
  } else if (dirName == "LEFT") {
    servoCamera.write(CAMERA_LEFT);
    currentCameraPos = CAM_LEFT;
  }
  
  delay(500);  // Attendi posizionamento camera
  
  // Rileva muro con ultrasuono appropriato
  uint16_t distance = 0;
  
  if (dirName == "FRONT") {
    distance = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  } else if (dirName == "RIGHT") {
    distance = readUltrasonic(US_RIGHT_TRIG, US_RIGHT_ECHO);
  } else if (dirName == "LEFT") {
    distance = readUltrasonic(US_LEFT_TRIG, US_LEFT_ECHO);
  }
  
  Serial.print("[DEBUG] Distanza ");
  Serial.print(dirName);
  Serial.print(": ");
  Serial.print(distance);
  Serial.println(" mm");
  
  // Determina presenza muro (1=presente, 0=assente)
  int wallPresent = (distance < WALL_DISTANCE_THRESHOLD && distance > 20) ? 1 : 0;
  
  // Invia a Raspberry
  Serial.println(wallPresent);
  
  // Attendi risposta da Raspberry (codice rilevazione)
  int code = waitForDetectionCode();
  
  // Gestisci codice rilevazione
  handleDetectionCode(code);
}

// =================== LETTURA SENSORI ===================

uint16_t readUltrasonic(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000);  // Timeout 30ms
  if (duration == 0) {
    return 8190;  // Fuori range
  }
  
  // Calcola distanza in mm (velocità suono ~343 m/s)
  return (duration * 0.343) / 2;
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

void readColorSensor() {
  // Leggi componente R
  digitalWrite(TCS_S2, LOW);
  digitalWrite(TCS_S3, LOW);
  sensorData.colorR = pulseIn(TCS_OUT, LOW);
  
  // Leggi componente G
  digitalWrite(TCS_S2, HIGH);
  digitalWrite(TCS_S3, HIGH);
  sensorData.colorG = pulseIn(TCS_OUT, LOW);
  
  // Leggi componente B
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
  
  // Calcola angoli pitch e roll (semplificato)
  sensorData.pitch = atan2(ay, sqrt(ax*ax + az*az)) * 180.0 / PI;
  sensorData.roll = atan2(ax, sqrt(ay*ay + az*az)) * 180.0 / PI;
}

// =================== GESTIONE CODICI RILEVAZIONE ===================

void handleDetectionCode(int code) {
  switch (code) {
    case CODE_ROSSO:
      lcd.setCursor(0, 2);
      lcd.print("ROSSO               ");
      delay(2000);
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      break;
      
    case CODE_GIALLO:
      lcd.setCursor(0, 2);
      lcd.print("GIALLO              ");
      delay(2000);
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      break;
      
    case CODE_VERDE:
      lcd.setCursor(0, 2);
      lcd.print("VERDE               ");
      delay(2000);
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      break;
      
    case CODE_PHI:
      deliverMedikit("Harmed", 2);  // 2 rotazioni 90°
      break;
      
    case CODE_PSI:
      deliverMedikit("Secured", 1);  // 1 rotazione 90°
      break;
      
    case CODE_OMEGA:
      lcd.setCursor(0, 2);
      lcd.print("Unharmed            ");
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
  Serial.print("[INFO] Consegna medikit: ");
  Serial.println(victimType);
  
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print(victimType);
  
  // Ruota 180° (direzione opposta alla vittima)
  rotate180();
  
  // Rilascia medikit con servo
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
  lcd.clear();
}

// =================== MOVIMENTO ===================

void executeMovement(int direction) {
  switch (direction) {
    case DIR_DRITTO:
      Serial.println("[INFO] Movimento: DRITTO");
      moveForwardTile();
      break;
      
    case DIR_DESTRA:
      Serial.println("[INFO] Movimento: DESTRA + AVANTI");
      rotate90(true);  // Clockwise
      moveForwardTile();
      currentDirection++;
      break;
      
    case DIR_SINISTRA:
      Serial.println("[INFO] Movimento: SINISTRA + AVANTI");
      rotate90(false);  // Counterclockwise
      moveForwardTile();
      currentDirection--;
      break;
      
    case DIR_DIETRO:
      Serial.println("[INFO] Movimento: DIETRO");
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
    
    if (isBlackTile()) {
      Serial.println("[ERROR] Cella NERA rilevata!");
      
      // Torna indietro
      for (int back = 0; back < step; back++) {
        moveBackwardStep();
      }
      
      Serial.println("0");  // Movimento fallito
      success = false;
      return;
    }
    
    if (isRedTile()) {
      Serial.println("[ERROR] Cella ROSSA rilevata!");
      
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
      Serial.print("[ERROR] Pendenza rilevata! Pitch: ");
      Serial.print(sensorData.pitch);
      Serial.print("° Roll: ");
      Serial.print(sensorData.roll);
      Serial.println("°");
      
      // Torna indietro
      for (int back = 0; back < step; back++) {
        moveBackwardStep();
      }
      
      Serial.println("0");  // Movimento fallito
      success = false;
      return;
    }
    
    // Avanza di uno step
    moveForwardStep();
  }
  
  if (success) {
    Serial.println("[INFO] Movimento completato con successo");
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
  Serial.print("[INFO] Rotazione 90° ");
  Serial.println(clockwise ? "orario" : "antiorario");
  
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
  Serial.println("[INFO] Rotazione 180°");
  
  setMotorSpeed(MOTOR_SPEED_NORMAL, -MOTOR_SPEED_NORMAL);
  delay(ROTATION_DELAY_90 * 2);
  stopMotors();
  delay(100);
}

// =================== CONTROLLO MOTORI ===================

void setMotorSpeed(int leftSpeed, int rightSpeed) {
  // Motore SINISTRO
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
  
  // Motore DESTRO
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

// =================== CALIBRAZIONE ===================

void autocalibrazione() {
  readAllVL53L0X();
  
  // Verifica allineamento laterale con VL53L0X
  // Lato frontale: confronta sensori left e right
  int16_t diffFront = sensorData.frontLeft1 - sensorData.frontRight1;
  
  if (abs(diffFront) > CENTER_TOLERANCE) {
    Serial.print("[DEBUG] Disallineamento frontale: ");
    Serial.println(diffFront);
    
    if (diffFront > 0) {
      // Frontale sinistra più lontana → ruota leggermente a destra
      setMotorSpeed(MOTOR_SPEED_SLOW, -MOTOR_SPEED_SLOW);
      delay(50);
      stopMotors();
    } else {
      // Frontale destra più lontana → ruota leggermente a sinistra
      setMotorSpeed(-MOTOR_SPEED_SLOW, MOTOR_SPEED_SLOW);
      delay(50);
      stopMotors();
    }
  }
  
  Serial.println("[DEBUG] Calibrazione completata");
}

// =================== COMUNICAZIONE ===================

int waitForDirection() {
  Serial.println("[DEBUG] Attesa direzione da Raspberry...");
  
  unsigned long startWait = millis();
  
  while (millis() - startWait < SERIAL_TIMEOUT) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      
      int value = input.toInt();
      return value;
    }
    
    delay(10);
  }
  
  Serial.println("[ERROR] Timeout attesa direzione");
  return DIR_TERMINA;
}

int waitForDetectionCode() {
  Serial.println("[DEBUG] Attesa codice rilevazione...");
  
  unsigned long startWait = millis();
  
  while (millis() - startWait < SERIAL_TIMEOUT) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      
      int value = input.toInt();
      return value;
    }
    
    delay(10);
  }
  
  Serial.println("[ERROR] Timeout attesa codice");
  return CODE_NESSUNA;
}

int waitForConfirm() {
  Serial.println("[DEBUG] Attesa conferma...");
  
  unsigned long startWait = millis();
  
  while (millis() - startWait < SERIAL_TIMEOUT) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      
      int value = input.toInt();
      return value;
    }
    
    delay(10);
  }
  
  Serial.println("[WARN] Timeout conferma");
  return 0;
}

// =================== SISTEMA ===================

void startSystem() {
  systemRunning = true;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SISTEMA ATTIVO");
  
  digitalWrite(LED_PIN, HIGH);
  
  Serial.println("[INFO] === SISTEMA AVVIATO ===");
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
  
  Serial.println("[INFO] === SISTEMA FERMATO ===");
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
  // Spegni tutti i sensori
  for (int i = 0; i < NUM_VL53L0X; i++) {
    pinMode(XSHUT_PIN_START + i, OUTPUT);
    digitalWrite(XSHUT_PIN_START + i, LOW);
  }
  delay(10);
  
  // Inizializza uno alla volta
  for (int i = 0; i < NUM_VL53L0X; i++) {
    // Accendi sensore i
    digitalWrite(XSHUT_PIN_START + i, HIGH);
    delay(10);
    
    sensors[i].setTimeout(500);
    
    if (!sensors[i].init()) {
      Serial.print("[ERROR] VL53L0X #");
      Serial.print(i);
      Serial.println(" non trovato!");
      
      lcd.setCursor(0, 2);
      lcd.print("ERR: VL53 #");
      lcd.print(i);
      delay(2000);
      
      // Blocca se sensore critico
      while(1) {
        digitalWrite(LED_PIN, (millis() / 200) % 2);
      }
    }
    
    // Assegna nuovo indirizzo I2C
    sensors[i].setAddress(sensorAddresses[i]);
    
    // Imposta timing budget (velocità/accuratezza)
    sensors[i].setMeasurementTimingBudget(20000);
    
    Serial.print("[INFO] VL53L0X #");
    Serial.print(i);
    Serial.print(" OK @ 0x");
    Serial.println(sensorAddresses[i], HEX);
  }
}
