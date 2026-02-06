#include <Wire.h>
#include <VL53L0X.h>

// Pin pulsante START
#define START_BUTTON_PIN 2
#define LED_PIN 13

// Configurazione sensori VL53L0X (8 sensori, 2 per angolo)
#define NUM_VL53L0X 8
#define XSHUT_PIN_START 22

// Pin ultrasuoni
#define US_FRONT_TRIG 30
#define US_FRONT_ECHO 31
#define US_LEFT_TRIG 32
#define US_LEFT_ECHO 33
#define US_RIGHT_TRIG 34
#define US_RIGHT_ECHO 35

// Costanti labirinto
#define TILE_SIZE 300
#define ROBOT_SIZE 200
#define WALL_THRESHOLD 250
#define CENTER_TOLERANCE 10
#define ANGLE_TOLERANCE 3

// Calibrazione
#define CALIBRATION_SAMPLES 10
#define WALL_PRESENT_THRESHOLD 200  // mm - se distanza < 200mm = muro presente

// Stati del sistema
enum SystemState {
  STATE_IDLE,
  STATE_COUNTDOWN,
  STATE_CALIBRATION,
  STATE_WALL_CHECK,     // NUOVO STATO - controlla muro una sola volta
  STATE_RUNNING,
  STATE_STOPPED
};

SystemState currentState = STATE_IDLE;
unsigned long countdownStart = 0;
unsigned long calibrationStart = 0;
bool systemRunning = false;
bool wallChecked = false;  // Flag per controllare una sola volta

// Dati calibrazione
struct CalibrationData {
  uint16_t frontMin;
  uint16_t frontMax;
  uint16_t frontAvg;
  uint16_t leftMin;
  uint16_t leftMax;
  uint16_t rightMin;
  uint16_t rightMax;
  bool completed;
} calibration;

VL53L0X sensors[NUM_VL53L0X];
uint8_t sensorAddresses[NUM_VL53L0X] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

struct SensorData {
  uint16_t distFL1, distFL2;
  uint16_t distFR1, distFR2;
  uint16_t distBR1, distBR2;
  uint16_t distBL1, distBL2;
  uint16_t usFront, usLeft, usRight;
};

SensorData sensorData;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);
  
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  pinMode(US_FRONT_TRIG, OUTPUT);
  pinMode(US_FRONT_ECHO, INPUT);
  pinMode(US_LEFT_TRIG, OUTPUT);
  pinMode(US_LEFT_ECHO, INPUT);
  pinMode(US_RIGHT_TRIG, OUTPUT);
  pinMode(US_RIGHT_ECHO, INPUT);
  
  Serial.println("Inizializzazione sensori...");
  initVL53L0X();
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║  ROBOCUP RESCUE MAZE - READY   ║");
  Serial.println("╚════════════════════════════════╝");
  Serial.println("\n>> Premi il pulsante START per iniziare <<\n");
  
  blinkReady();
}

void loop() {
  handleStateMachine();
  
  // Controllo muro frontale UNA SOLA VOLTA dopo calibrazione
  if (currentState == STATE_WALL_CHECK && !wallChecked) {
    checkFrontWall();
    wallChecked = true;
    
    // Dopo il controllo, passa allo stato RUNNING
    currentState = STATE_RUNNING;
    systemRunning = true;
    
    Serial.println("\n╔════════════════════════════════╗");
    Serial.println("║      SISTEMA ATTIVO!           ║");
    Serial.println("╚════════════════════════════════╝\n");
  }
  
  // Qui puoi aggiungere altre funzionalità (navigazione, ecc.)
  if (currentState == STATE_RUNNING) {
    // Il robot ora può fare altre cose...
  }
  
  delay(100);
}

void handleStateMachine() {
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(START_BUTTON_PIN);
  
  if (lastButtonState == HIGH && buttonState == LOW) {
    delay(50);
    
    switch (currentState) {
      case STATE_IDLE:
        startCountdown();
        break;
        
      case STATE_COUNTDOWN:
        cancelCountdown();
        break;
        
      case STATE_CALIBRATION:
        // Durante calibrazione non si può interrompere
        break;
        
      case STATE_WALL_CHECK:
        // Durante controllo muro non si può interrompere
        break;
        
      case STATE_RUNNING:
        stopSystem();
        break;
        
      case STATE_STOPPED:
        startCountdown();
        break;
    }
  }
  
  lastButtonState = buttonState;
  
  // Gestione countdown
  if (currentState == STATE_COUNTDOWN) {
    unsigned long elapsed = millis() - countdownStart;
    int remaining = 5 - (elapsed / 1000);
    
    if (remaining > 0) {
      digitalWrite(LED_PIN, (millis() / 500) % 2);
      
      static int lastPrinted = -1;
      if (remaining != lastPrinted) {
        Serial.print(">>> Avvio tra ");
        Serial.print(remaining);
        Serial.println(" secondi...");
        lastPrinted = remaining;
      }
    } else {
      startCalibration();
    }
  }
  
  // Gestione calibrazione
  if (currentState == STATE_CALIBRATION) {
    performCalibration();
  }
}

void startCountdown() {
  currentState = STATE_COUNTDOWN;
  countdownStart = millis();
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║     COUNTDOWN AVVIATO!         ║");
  Serial.println("║  Premi di nuovo per annullare  ║");
  Serial.println("╚════════════════════════════════╝\n");
}

void cancelCountdown() {
  currentState = STATE_IDLE;
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("\n⚠ COUNTDOWN ANNULLATO\n");
  Serial.println(">> Premi START per riavviare <<\n");
  
  blinkReady();
}

void startCalibration() {
  currentState = STATE_CALIBRATION;
  calibrationStart = millis();
  calibration.completed = false;
  
  // Reset valori calibrazione
  calibration.frontMin = 65535;
  calibration.frontMax = 0;
  calibration.frontAvg = 0;
  calibration.leftMin = 65535;
  calibration.leftMax = 0;
  calibration.rightMin = 65535;
  calibration.rightMax = 0;
  
  digitalWrite(LED_PIN, HIGH);
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║   CALIBRAZIONE IN CORSO...     ║");
  Serial.println("║    Attendere prego...          ║");
  Serial.println("╚════════════════════════════════╝\n");
}

void performCalibration() {
  static int sampleCount = 0;
  static unsigned long totalFront = 0;
  
  if (sampleCount < CALIBRATION_SAMPLES) {
    // Leggi campioni
    uint16_t frontDist = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
    uint16_t leftDist = readUltrasonic(US_LEFT_TRIG, US_LEFT_ECHO);
    uint16_t rightDist = readUltrasonic(US_RIGHT_TRIG, US_RIGHT_ECHO);
    
    // Aggiorna min/max/avg
    if (frontDist < calibration.frontMin) calibration.frontMin = frontDist;
    if (frontDist > calibration.frontMax) calibration.frontMax = frontDist;
    totalFront += frontDist;
    
    if (leftDist < calibration.leftMin) calibration.leftMin = leftDist;
    if (leftDist > calibration.leftMax) calibration.leftMax = leftDist;
    
    if (rightDist < calibration.rightMin) calibration.rightMin = rightDist;
    if (rightDist > calibration.rightMax) calibration.rightMax = rightDist;
    
    sampleCount++;
    
    // LED lampeggia durante calibrazione
    digitalWrite(LED_PIN, (millis() / 100) % 2);
    
    Serial.print("Campione ");
    Serial.print(sampleCount);
    Serial.print("/");
    Serial.print(CALIBRATION_SAMPLES);
    Serial.print(" - Front: ");
    Serial.print(frontDist);
    Serial.println(" mm");
    
    delay(200);
    
  } else {
    // Calibrazione completata
    calibration.frontAvg = totalFront / CALIBRATION_SAMPLES;
    calibration.completed = true;
    
    Serial.println("\n╔════════════════════════════════╗");
    Serial.println("║   CALIBRAZIONE COMPLETATA!     ║");
    Serial.println("╚════════════════════════════════╝\n");
    
    Serial.println("--- Risultati Calibrazione ---");
    Serial.print("Frontale - Min: "); Serial.print(calibration.frontMin);
    Serial.print(" mm, Max: "); Serial.print(calibration.frontMax);
    Serial.print(" mm, Avg: "); Serial.print(calibration.frontAvg);
    Serial.println(" mm");
    
    Serial.print("Sinistro - Min: "); Serial.print(calibration.leftMin);
    Serial.print(" mm, Max: "); Serial.println(calibration.leftMax);
    
    Serial.print("Destro - Min: "); Serial.print(calibration.rightMin);
    Serial.print(" mm, Max: "); Serial.println(calibration.rightMax);
    Serial.println();
    
    delay(500);
    
    // Reset contatori per prossima calibrazione
    sampleCount = 0;
    totalFront = 0;
    
    // Passa al controllo muro
    currentState = STATE_WALL_CHECK;
    wallChecked = false;
  }
}

void checkFrontWall() {
  Serial.println("--- Controllo Muro Frontale ---");
  
  // Fai 3 letture e prendi la mediana per maggiore affidabilità
  uint16_t readings[3];
  readings[0] = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  delay(50);
  readings[1] = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  delay(50);
  readings[2] = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  
  // Ordina per trovare mediana
  if (readings[0] > readings[1]) { uint16_t temp = readings[0]; readings[0] = readings[1]; readings[1] = temp; }
  if (readings[1] > readings[2]) { uint16_t temp = readings[1]; readings[1] = readings[2]; readings[2] = temp; }
  if (readings[0] > readings[1]) { uint16_t temp = readings[0]; readings[0] = readings[1]; readings[1] = temp; }
  
  uint16_t frontDistance = readings[1]; // Mediana
  
  // Determina presenza muro (0 = NO, 1 = SI)
  int wallPresent = (frontDistance < WALL_PRESENT_THRESHOLD && frontDistance > 20) ? 1 : 0;
  
  // OUTPUT SERIALE - SOLO 0 o 1
  Serial.println(wallPresent);
  
  // Info dettagliate (opzionali)
  Serial.print("Distanza rilevata: ");
  Serial.print(frontDistance);
  Serial.print(" mm -> ");
  Serial.println(wallPresent ? "MURO PRESENTE" : "MURO ASSENTE");
  Serial.println();
}

void stopSystem() {
  currentState = STATE_STOPPED;
  systemRunning = false;
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║      SISTEMA FERMATO!          ║");
  Serial.println("╚════════════════════════════════╝\n");
  Serial.println(">> Premi START per riavviare <<\n");
  
  // Reset flag per permettere nuovo controllo
  wallChecked = false;
}

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
      Serial.print("❌ Errore sensore ");
      Serial.println(i);
      while(1) {
        digitalWrite(LED_PIN, (millis() / 200) % 2);
      }
    }
    
    sensors[i].setAddress(sensorAddresses[i]);
    sensors[i].setMeasurementTimingBudget(20000);
    
    Serial.print("✓ Sensore ");
    Serial.print(i);
    Serial.println(" OK");
  }
}

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
