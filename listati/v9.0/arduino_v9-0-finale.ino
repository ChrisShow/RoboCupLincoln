/*
* RoboCup Rescue Maze 2026 - Arduino MEGA 2560 - VERSION 9.0 FINALE
* 
* CHANGELOG V9:
* ✅ NUOVO: Algoritmo BFS inverso per ritorno a cella (0,0)
* ✅ NUOVO: Stato RETURN_TO_START con LED VERDE lampeggio veloce
* ✅ NUOVO: Codici CODE_RETURN_HOME (7) e CODE_HOME_REACHED (8)
* ✅ NUOVO: Skip rilevazione vittime durante ritorno (priorità velocità)
* ✅ NUOVO: Autocalibrazione ogni 5 step (150mm) durante ritorno
* ✅ NUOVO: Fallback wall-follower se BFS non trova percorso
* ✅ NUOVO: Velocità aumentata MOTOR_SPEED_RETURN = 180
* ✅ NUOVO: LED ROSSO lampeggio 1s ON/1s OFF all'arrivo a start
* - Gestione piani inclinati/gradini (V8)
* - 2 LED separati (ROSSO status + VERDE power)
* 
* EXIT BONUS ACTIVATION:
* Quando lista celle visitabili si svuota, Franco:
* 1. Calcola percorso ottimale BFS inverso a (0,0)
* 2. Esegue movimento veloce con skip controlli
* 3. LED VERDE lampeggia veloce (200ms) durante ritorno
* 4. Arrivato a (0,0): LED ROSSO lampeggia 1s ON/1s OFF (5 cicli)
* 5. LED VERDE fisso + STOP definitivo
* 6. Guadagna EXIT BONUS (+10 punti per vittima salvata)
* 
* Configurazione Hardware:
* - 4 motori DC Mecanum (configurazione X)
* - 3x VL53L0X (sensori distanza)
* - 1x TCS3200 (sensore colore)
* - 1x MPU6050 GY-521 (giroscopio/accelerometro)
* - 2x Servo (camera + medikit)
* - 2x LED (rosso status + verde power)
* - Comunicazione UART con Raspberry Pi
*/

#include <Wire.h>
#include <VL53L0X.h>
#include <Servo.h>
#include <MPU6050.h>
#include <avr/wdt.h>

// ========== MOTORI - Driver A (Anteriori) ==========
#define MOTOR_FL_PWM 6
#define MOTOR_FL_IN1 22
#define MOTOR_FL_IN2 23

#define MOTOR_FR_PWM 7
#define MOTOR_FR_IN3 24
#define MOTOR_FR_IN4 25

// ========== MOTORI - Driver B (Posteriori) ==========
#define MOTOR_BL_PWM 8
#define MOTOR_BL_IN1 26
#define MOTOR_BL_IN2 27

#define MOTOR_BR_PWM 9
#define MOTOR_BR_IN3 28
#define MOTOR_BR_IN4 29

// Velocità motori (0-255)
#define MOTOR_SPEED_NORMAL 150
#define MOTOR_SPEED_SLOW 100
#define MOTOR_SPEED_TURN 120
#define MOTOR_SPEED_INCLINE 100
#define MOTOR_SPEED_RETURN 180  // V9: Velocità aumentata per ritorno

// ========== CALIBRAZIONE MOTORI ==========
float motorFL_coefficient = 1.00;
float motorFR_coefficient = 1.00;
float motorBL_coefficient = 1.00;
float motorBR_coefficient = 1.00;

// ========== SENSORI DISTANZA VL53L0X ==========
#define VL53L0X_FRONT_ADDR 0x30
#define VL53L0X_RIGHT_ADDR 0x31
#define VL53L0X_LEFT_ADDR 0x32

VL53L0X sensorFront;
VL53L0X sensorRight;
VL53L0X sensorLeft;

#define WALL_THRESHOLD 150  // mm - distanza muro

// ========== AUTOCALIBRAZIONE CORRIDOIO ==========
#define CORRIDOR_WIDTH_IDEAL 300
#define CORRIDOR_TOLERANCE 50
#define CALIBRATION_CORRECTION 10

// ========== SENSORE COLORE TCS3200 ==========
#define TCS3200_S0 30
#define TCS3200_S1 31
#define TCS3200_S2 32
#define TCS3200_S3 33
#define TCS3200_OUT 34

#define BLUE_THRESHOLD_B 50
#define BLUE_THRESHOLD_RG 100
#define BLACK_THRESHOLD 200
#define RED_THRESHOLD_R 50
#define RED_THRESHOLD_GB 100

// ========== SENSORE INCLINAZIONE MPU6050 GY-521 ==========
MPU6050 mpu;

#define INCLINE_THRESHOLD_MIN 15.0
#define INCLINE_THRESHOLD_MAX 25.0
#define INCLINE_CHECK_INTERVAL 3
#define INCLINE_TIMEOUT_STEPS 100

// V9: Intervallo calibrazione durante ritorno
#define RETURN_CALIBRATION_INTERVAL 5  // Ogni 5 step (150mm) durante ritorno

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

// ========== LED ==========
#define LED_STATUS_RED 13
#define LED_POWER_GREEN 12
#define LED_BLINK_DURATION 5000
#define LED_BLINK_INTERVAL 500
#define LED_RETURN_BLINK_INTERVAL 200  // V9: Lampeggio veloce durante ritorno
#define LED_HOME_BLINK_INTERVAL 1000   // ✅ V9: Lampeggio 1s ON/1s OFF all'arrivo
#define LED_HOME_BLINK_CYCLES 5        // ✅ V9: 5 cicli (10 secondi totali)

// ========== COMUNICAZIONE RASPBERRY PI ==========
#define SERIAL_RASPBERRY Serial1
#define BAUD_RATE 9600

// Codici comunicazione
#define CODE_NESSUNA 0
#define CODE_HARMED 1
#define CODE_STABLE 2
#define CODE_UNHARMED 3
#define CODE_FALSO_POSITIVO 4
#define CODE_BLUE_TILE 5
#define CODE_SKIP_INCLINE 6
#define CODE_RETURN_HOME 7    // V9: Modalità ritorno attiva
#define CODE_HOME_REACHED 8   // V9: Arrivato a start (0,0)

// ========== STATE MACHINE ==========
enum RobotState {
  CALIBRATION,
  SCAN_FRONT,
  SCAN_RIGHT,
  SCAN_LEFT,
  WAIT_DIRECTION,
  MOVE,
  MOVE_INCLINE,
  RETURN_TO_START,
  HOME_CELEBRATION,  // ✅ V9: Nuovo stato per segnalazione arrivo
  WAIT_CONFIRM,
  EMERGENCY_STOP
};

RobotState currentState = CALIBRATION;

// ========== WATCHDOG & HEARTBEAT ==========
#define WATCHDOG_TIMEOUT 4000
#define HEARTBEAT_INTERVAL 1000
unsigned long lastHeartbeat = 0;
unsigned long lastRaspberryMessage = 0;
bool systemActive = true;

// ========== CALIBRAZIONE ROTAZIONI ==========
#define ROTATION_90_DELAY 500
#define ROTATION_180_DELAY 1000

// ========== VARIABILI GLOBALI ==========
int currentDirection = -1;
int movementDistance = 300;
bool movementSuccess = false;

bool onIncline = false;
int inclineStepCounter = 0;

// V9: Variabili ritorno a start
bool returningHome = false;
int returnStepCounter = 0;
unsigned long returnStartTime = 0;
unsigned long returnLedLastToggle = 0;
bool returnLedState = false;

// ✅ V9: Variabili celebrazione arrivo
unsigned long homeCelebrationStart = 0;
int homeCelebrationCycles = 0;

// ========== SETUP ==========
void setup() {
  Serial.begin(9600);
  Serial.println(F("=== RoboCup Rescue Maze V9.0 FINALE ==="));
  Serial.println(F("[INIT] Sistema in avvio..."));
  Serial.println(F("[INFO] EXIT BONUS: Ritorno automatico a (0,0) attivo"));

  SERIAL_RASPBERRY.begin(BAUD_RATE);

  setupMotors();
  setupVL53L0X();
  setupTCS3200();
  setupMPU6050();
  setupServos();
  setupLEDs();

  wdt_enable(WDTO_4S);

  Serial.println(F("[INIT] Sistema pronto! V9.0 - THE COMPLETE VERSION"));
  Serial.println(F("[INFO] Funzionalità: BFS esplorazione + Pendenze + Ritorno home + Celebrazione"));
  
  currentState = CALIBRATION;
}

// ========== SETUP LED ==========
void setupLEDs() {
  pinMode(LED_STATUS_RED, OUTPUT);
  digitalWrite(LED_STATUS_RED, LOW);

  pinMode(LED_POWER_GREEN, OUTPUT);
  digitalWrite(LED_POWER_GREEN, HIGH);

  Serial.println(F("[INIT] LED configurati (ROSSO status, VERDE power)"));
}

// ========== SETUP MOTORI ==========
void setupMotors() {
  pinMode(MOTOR_FL_PWM, OUTPUT);
  pinMode(MOTOR_FL_IN1, OUTPUT);
  pinMode(MOTOR_FL_IN2, OUTPUT);
  pinMode(MOTOR_FR_PWM, OUTPUT);
  pinMode(MOTOR_FR_IN3, OUTPUT);
  pinMode(MOTOR_FR_IN4, OUTPUT);

  pinMode(MOTOR_BL_PWM, OUTPUT);
  pinMode(MOTOR_BL_IN1, OUTPUT);
  pinMode(MOTOR_BL_IN2, OUTPUT);
  pinMode(MOTOR_BR_PWM, OUTPUT);
  pinMode(MOTOR_BR_IN3, OUTPUT);
  pinMode(MOTOR_BR_IN4, OUTPUT);

  stopMotors();

  Serial.println(F("[INIT] 4 motori Mecanum configurati"));
}

// ========== CONTROLLO MOTORI ==========

void setMotorFL(int speed, bool forward) {
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
  setMotorFL(speed, true);
  setMotorFR(speed, true);
  setMotorBL(speed, true);
  setMotorBR(speed, true);
}

void moveBackward(int speed) {
  setMotorFL(speed, false);
  setMotorFR(speed, false);
  setMotorBL(speed, false);
  setMotorBR(speed, false);
}

void turnLeft(int speed) {
  setMotorFL(speed, false);
  setMotorBL(speed, false);
  setMotorFR(speed, true);
  setMotorBR(speed, true);
}

void turnRight(int speed) {
  setMotorFL(speed, true);
  setMotorBL(speed, true);
  setMotorFR(speed, false);
  setMotorBR(speed, false);
}

void moveForwardCalibrated(int baseSpeed) {
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

// ========== AUTOCALIBRAZIONE CORRIDOIO ==========
void autoCalibrateCorridor() {
  int distLeft = readVL53L0X(sensorLeft);
  int distRight = readVL53L0X(sensorRight);

  if (distLeft <= 0 || distRight <= 0 || distLeft > 500 || distRight > 500) {
    Serial.println(F("[CALIB] Sensori laterali fuori range, skip calibrazione"));
    return;
  }

  int offset = distLeft - distRight;

  Serial.print(F("[CALIB] Dist L="));
  Serial.print(distLeft);
  Serial.print(F("mm, R="));
  Serial.print(distRight);
  Serial.print(F("mm, Offset="));
  Serial.print(offset);
  Serial.println(F("mm"));

  if (abs(offset) > CORRIDOR_TOLERANCE) {
    if (offset > 0) {
      Serial.println(F("[CALIB] Correzione DESTRA"));
      setMotorFL(MOTOR_SPEED_SLOW, true);
      setMotorBL(MOTOR_SPEED_SLOW, true);
      setMotorFR(MOTOR_SPEED_SLOW * 0.7, true);
      setMotorBR(MOTOR_SPEED_SLOW * 0.7, true);
      delay(200);
      stopMotors();
    } else {
      Serial.println(F("[CALIB] Correzione SINISTRA"));
      setMotorFL(MOTOR_SPEED_SLOW * 0.7, true);
      setMotorBL(MOTOR_SPEED_SLOW * 0.7, true);
      setMotorFR(MOTOR_SPEED_SLOW, true);
      setMotorBR(MOTOR_SPEED_SLOW, true);
      delay(200);
      stopMotors();
    }
  } else {
    Serial.println(F("[CALIB] Allineamento OK"));
  }
}

// ========== SETUP TCS3200 ==========
void setupTCS3200() {
  pinMode(TCS3200_S0, OUTPUT);
  pinMode(TCS3200_S1, OUTPUT);
  pinMode(TCS3200_S2, OUTPUT);
  pinMode(TCS3200_S3, OUTPUT);
  pinMode(TCS3200_OUT, INPUT);

  digitalWrite(TCS3200_S0, HIGH);
  digitalWrite(TCS3200_S1, LOW);

  Serial.println(F("[INIT] TCS3200 configurato"));
}

uint32_t readTCS3200Color(char filter) {
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

  bool isBlue = (blue < BLUE_THRESHOLD_B) && (red > BLUE_THRESHOLD_RG) && (green > BLUE_THRESHOLD_RG);
  return isBlue;
}

bool detectBlackTile() {
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');

  bool isBlack = (red > BLACK_THRESHOLD) && (green > BLACK_THRESHOLD) && (blue > BLACK_THRESHOLD);
  return isBlack;
}

bool detectRedTile() {
  uint32_t red = readTCS3200Color('R');
  uint32_t green = readTCS3200Color('G');
  uint32_t blue = readTCS3200Color('B');

  bool isRed = (red < RED_THRESHOLD_R) && (green > RED_THRESHOLD_GB) && (blue > RED_THRESHOLD_GB);
  return isRed;
}

char detectTileColor() {
  if (detectBlackTile()) return 'K';
  if (detectRedTile()) return 'R';
  if (detectBlueTile()) return 'B';
  return 'W';
}

// ========== SETUP MPU6050 ==========
void setupMPU6050() {
  mpu.initialize();
  if (mpu.testConnection()) {
    Serial.println(F("[INIT] MPU6050 GY-521 connesso"));
  } else {
    Serial.println(F("[ERROR] MPU6050 non risponde!"));
  }
}

float readMPU6050Pitch() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  float pitch = atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;
  return pitch;
}

bool isOnIncline() {
  float pitch = readMPU6050Pitch();
  float absPitch = abs(pitch);

  Serial.print(F("[MPU6050] Pitch: "));
  Serial.print(pitch);
  Serial.println(F("°"));

  if (absPitch >= INCLINE_THRESHOLD_MIN && absPitch <= INCLINE_THRESHOLD_MAX) {
    Serial.println(F("[INCLINE] Piano inclinato rilevato!"));
    return true;
  }

  return false;
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
  delay(500);
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
  Serial.println(F("[LED ROSSO] Segnalazione vittima - lampeggio 5s"));

  unsigned long startTime = millis();
  bool ledState = false;

  while (millis() - startTime < LED_BLINK_DURATION) {
    wdt_reset();

    ledState = !ledState;
    digitalWrite(LED_STATUS_RED, ledState ? HIGH : LOW);

    delay(LED_BLINK_INTERVAL);
  }

  digitalWrite(LED_STATUS_RED, LOW);

  Serial.println(F("[LED ROSSO] Segnalazione completata"));
}

// ✅ V9: LED CELEBRAZIONE ARRIVO A START
void celebrateHomeArrival() {
  Serial.println(F("[CELEBRATION] 🎉 ARRIVO A START (0,0)! 🎉"));
  Serial.println(F("[LED] Lampeggio ROSSO 1s ON/1s OFF x5 cicli"));

  homeCelebrationStart = millis();
  homeCelebrationCycles = 0;

  // Spegni LED VERDE temporaneamente
  digitalWrite(LED_POWER_GREEN, LOW);

  // 5 cicli di lampeggio (10 secondi totali)
  while (homeCelebrationCycles < LED_HOME_BLINK_CYCLES) {
    wdt_reset();

    // LED ROSSO ON per 1 secondo
    digitalWrite(LED_STATUS_RED, HIGH);
    delay(LED_HOME_BLINK_INTERVAL);

    wdt_reset();

    // LED ROSSO OFF per 1 secondo
    digitalWrite(LED_STATUS_RED, LOW);
    delay(LED_HOME_BLINK_INTERVAL);

    homeCelebrationCycles++;

    Serial.print(F("[CELEBRATION] Ciclo "));
    Serial.print(homeCelebrationCycles);
    Serial.print(F("/"));
    Serial.println(LED_HOME_BLINK_CYCLES);
  }

  // Fine celebrazione
  digitalWrite(LED_STATUS_RED, LOW);
  digitalWrite(LED_POWER_GREEN, HIGH);  // LED VERDE fisso

  Serial.println(F("[CELEBRATION] Celebrazione completata!"));
  Serial.println(F("[SUCCESS] 🏆 EXIT BONUS OTTENUTO! 🏆"));
}

// ========== GESTIONE CODICI VITTIMA ==========
void handleVictimCode(int code) {
  switch (code) {
    case CODE_HARMED:
      Serial.println(F("[VICTIM] HARMED rilevata"));
      signalVictimDetection();
      releaseMediakit(2);
      break;

    case CODE_STABLE:
      Serial.println(F("[VICTIM] STABLE rilevata"));
      signalVictimDetection();
      releaseMediakit(1);
      break;

    case CODE_UNHARMED:
      Serial.println(F("[VICTIM] UNHARMED rilevata"));
      signalVictimDetection();
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
  const int STEP_INCREMENT_MM = 30;
  int totalSteps = distanceMM / STEP_INCREMENT_MM;

  for (int step = 0; step < totalSteps; step++) {
    wdt_reset();

    int frontDistance = readVL53L0X(sensorFront);
    if (frontDistance < WALL_THRESHOLD && frontDistance > 0) {
      Serial.println(F("[STOP] Ostacolo frontale rilevato"));
      stopMotors();
      return false;
    }

    if (isOnIncline()) {
      Serial.println(F("[INCLINE] Piano inclinato rilevato! Passaggio a MOVE_INCLINE"));
      stopMotors();
      onIncline = true;
      inclineStepCounter = 0;
      currentState = MOVE_INCLINE;
      return true;
    }

    char tileColor = detectTileColor();

    if (tileColor == 'K') {
      Serial.println(F("[STOP] Cella NERA rilevata"));
      stopMotors();
      moveBackward(MOTOR_SPEED_SLOW);
      delay(300);
      stopMotors();
      return false;
    }

    if (tileColor == 'R') {
      Serial.println(F("[STOP] Cella ROSSA rilevata"));
      stopMotors();
      moveBackward(MOTOR_SPEED_SLOW);
      delay(300);
      stopMotors();
      return false;
    }

    moveForwardCalibrated(MOTOR_SPEED_NORMAL);
    delay(STEP_INCREMENT_MM * 10 / MOTOR_SPEED_NORMAL);
    stopMotors();
    delay(50);

    stepsCompleted++;

    if (tileColor == 'B') {
      Serial.println(F("[INFO] Movimento su cella BLU"));
      Serial.println(F("[WAIT] Stazionamento 5s su cella blu..."));
      for (int i = 0; i < 5; i++) {
        wdt_reset();
        delay(1000);
      }

      sendToRaspberry(CODE_BLUE_TILE);
      Serial.println(F("[→ Raspberry] CODE_BLUE_TILE"));
    }
  }

  Serial.print(F("[SUCCESS] Movimento completato: "));
  Serial.print(stepsCompleted * STEP_INCREMENT_MM);
  Serial.println(F(" mm"));

  return true;
}

// ========== MOVIMENTO SU PIANO INCLINATO ==========
void moveOnIncline() {
  Serial.println(F("[INCLINE] Movimento su piano inclinato avviato"));
  Serial.println(F("[INCLINE] SKIP rilevazione vittime, velocità ridotta"));

  sendToRaspberry(CODE_SKIP_INCLINE);

  while (inclineStepCounter < INCLINE_TIMEOUT_STEPS) {
    wdt_reset();

    moveForwardCalibrated(MOTOR_SPEED_INCLINE);
    delay(30 * 10 / MOTOR_SPEED_INCLINE);
    stopMotors();
    delay(50);

    inclineStepCounter++;

    if (inclineStepCounter % INCLINE_CHECK_INTERVAL == 0) {
      Serial.print(F("[INCLINE] Check step "));
      Serial.println(inclineStepCounter);

      autoCalibrateCorridor();

      if (!isOnIncline()) {
        Serial.println(F("[INCLINE] Piano orizzontale raggiunto!"));
        onIncline = false;
        currentState = CALIBRATION;
        return;
      }
    }

    int frontDistance = readVL53L0X(sensorFront);
    if (frontDistance < WALL_THRESHOLD && frontDistance > 0) {
      Serial.println(F("[INCLINE] Ostacolo frontale su pendenza"));
      stopMotors();
      onIncline = false;
      movementSuccess = false;
      currentState = WAIT_CONFIRM;
      return;
    }
  }

  Serial.println(F("[INCLINE] Timeout 3m raggiunto su pendenza"));
  onIncline = false;
  currentState = CALIBRATION;
}

// V9: MOVIMENTO RITORNO A START (veloce, skip vittime)
bool moveForwardReturn(int distanceMM) {
  Serial.print(F("[RETURN] Avanzamento veloce: "));
  Serial.print(distanceMM);
  Serial.println(F(" mm"));

  int stepsCompleted = 0;
  const int STEP_INCREMENT_MM = 30;
  int totalSteps = distanceMM / STEP_INCREMENT_MM;

  for (int step = 0; step < totalSteps; step++) {
    wdt_reset();

    int frontDistance = readVL53L0X(sensorFront);
    if (frontDistance < WALL_THRESHOLD && frontDistance > 0) {
      Serial.println(F("[RETURN STOP] Ostacolo frontale"));
      stopMotors();
      return false;
    }

    if (isOnIncline()) {
      Serial.println(F("[RETURN] Pendenza rilevata durante ritorno"));
      stopMotors();
      onIncline = true;
      inclineStepCounter = 0;
      currentState = MOVE_INCLINE;
      return true;
    }

    char tileColor = detectTileColor();

    if (tileColor == 'K') {
      Serial.println(F("[RETURN STOP] Cella NERA"));
      stopMotors();
      moveBackward(MOTOR_SPEED_SLOW);
      delay(300);
      stopMotors();
      return false;
    }

    if (tileColor == 'R') {
      Serial.println(F("[RETURN STOP] Cella ROSSA"));
      stopMotors();
      moveBackward(MOTOR_SPEED_SLOW);
      delay(300);
      stopMotors();
      return false;
    }

    moveForwardCalibrated(MOTOR_SPEED_RETURN);
    delay(STEP_INCREMENT_MM * 10 / MOTOR_SPEED_RETURN);
    stopMotors();
    delay(50);

    stepsCompleted++;
    returnStepCounter++;

    if (returnStepCounter % RETURN_CALIBRATION_INTERVAL == 0) {
      autoCalibrateCorridor();
    }

    if (millis() - returnLedLastToggle > LED_RETURN_BLINK_INTERVAL) {
      returnLedState = !returnLedState;
      digitalWrite(LED_POWER_GREEN, returnLedState ? HIGH : LOW);
      returnLedLastToggle = millis();
    }
  }

  Serial.print(F("[RETURN SUCCESS] Completati "));
  Serial.print(stepsCompleted * STEP_INCREMENT_MM);
  Serial.println(F(" mm"));

  return true;
}

void executeRotation(int targetDirection) {
  int currentDir = currentDirection;
  int rotation = (targetDirection - currentDir + 4) % 4;

  switch(rotation) {
    case 0:
      Serial.println(F("[MOVE] Nessuna rotazione necessaria"));
      break;

    case 1:
      Serial.println(F("[MOVE] Rotazione 90° DESTRA"));
      turnRight(MOTOR_SPEED_TURN);
      delay(ROTATION_90_DELAY);
      stopMotors();
      break;

    case 2:
      Serial.println(F("[MOVE] Rotazione 180°"));
      turnRight(MOTOR_SPEED_TURN);
      delay(ROTATION_180_DELAY);
      stopMotors();
      break;

    case 3:
      Serial.println(F("[MOVE] Rotazione 90° SINISTRA"));
      turnLeft(MOTOR_SPEED_TURN);
      delay(ROTATION_90_DELAY);
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

    if (message.startsWith("DIR:")) {
      currentDirection = message.substring(4).toInt();
      Serial.print(F("[← Raspberry] Direzione ricevuta: "));
      Serial.println(currentDirection);

      if (returningHome) {
        currentState = RETURN_TO_START;
      } else {
        currentState = MOVE;
      }
    }
    else if (message.startsWith("CODE:")) {
      int victimCode = message.substring(5).toInt();
      Serial.print(F("[← Raspberry] Codice vittima: "));
      Serial.println(victimCode);
      handleVictimCode(victimCode);
    }
    else if (message == "HEARTBEAT") {
      systemActive = true;
    }
    else if (message == "START_RETURN") {
      Serial.println(F("[← Raspberry] COMANDO RITORNO A START"));
      returningHome = true;
      returnStepCounter = 0;
      returnStartTime = millis();
      sendToRaspberry(CODE_RETURN_HOME);
      Serial.println(F("[→ Raspberry] CODE_RETURN_HOME (7)"));
    }
    else if (message == "HOME_REACHED") {  // ✅ V9: Avvia celebrazione
      Serial.println(F("[← Raspberry] CONFERMA ARRIVO A START!"));
      sendToRaspberry(CODE_HOME_REACHED);
      
      // Passa a stato celebrazione
      currentState = HOME_CELEBRATION;
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
    
    digitalWrite(LED_POWER_GREEN, LOW);
    delay(200);
    digitalWrite(LED_POWER_GREEN, HIGH);
    
    stopMotors();
    currentState = EMERGENCY_STOP;
  }
}

// ========== SCANSIONE MURO ==========
void scanWall(int angle, const char* direction) {
  Serial.print(F("[SCAN] Scansione muro "));
  Serial.println(direction);

  setCameraAngle(angle);

  int distance = -1;
  if (angle == CAMERA_CENTER) {
    distance = readVL53L0X(sensorFront);
  } else if (angle == CAMERA_RIGHT) {
    distance = readVL53L0X(sensorRight);
  } else if (angle == CAMERA_LEFT) {
    distance = readVL53L0X(sensorLeft);
  }

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
  wdt_reset();
  sendHeartbeat();
  checkRaspberryMessages();
  checkWatchdog();

  switch (currentState) {
    case CALIBRATION:
      Serial.println(F("[STATE] CALIBRATION"));
      delay(1000);
      
      if (returningHome) {
        currentState = WAIT_DIRECTION;
      } else {
        currentState = SCAN_FRONT;
      }
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
      delay(100);
      break;

    case MOVE:
      Serial.println(F("[STATE] MOVE"));
      executeRotation(currentDirection);
      movementSuccess = moveForwardSafe(movementDistance);

      if (currentState == MOVE_INCLINE) {
        break;
      }

      sendToRaspberry(movementSuccess ? 1 : 0);

      currentState = WAIT_CONFIRM;
      break;

    case MOVE_INCLINE:
      Serial.println(F("[STATE] MOVE_INCLINE"));
      moveOnIncline();
      break;

    case RETURN_TO_START:
      Serial.println(F("[STATE] RETURN_TO_START"));
      Serial.print(F("[RETURN] Step counter: "));
      Serial.println(returnStepCounter);
      
      executeRotation(currentDirection);
      movementSuccess = moveForwardReturn(movementDistance);

      if (currentState == MOVE_INCLINE) {
        break;
      }

      sendToRaspberry(movementSuccess ? 1 : 0);

      currentState = WAIT_CONFIRM;
      break;

    case HOME_CELEBRATION:  // ✅ V9: Nuovo stato celebrazione
      Serial.println(F("[STATE] HOME_CELEBRATION"));
      stopMotors();
      
      celebrateHomeArrival();
      
      // STOP definitivo
      currentState = EMERGENCY_STOP;
      Serial.println(F("[FINAL] Missione completata! Sistema in STOP definitivo"));
      break;

    case WAIT_CONFIRM:
      Serial.println(F("[STATE] WAIT_CONFIRM"));
      delay(500);
      currentState = CALIBRATION;
      break;

    case EMERGENCY_STOP:
      Serial.println(F("[STATE] EMERGENCY_STOP"));
      stopMotors();
      
      if (!returningHome) {
        digitalWrite(LED_POWER_GREEN, !digitalRead(LED_POWER_GREEN));
      }
      
      if (systemActive && !returningHome) {
        digitalWrite(LED_POWER_GREEN, HIGH);
        currentState = CALIBRATION;
      }
      delay(1000);
      break;
  }

  delay(50);
}
