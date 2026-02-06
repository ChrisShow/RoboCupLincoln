#include <Wire.h>
#include <VL53L0X.h>

// Configurazione sensori VL53L0X (8 sensori, 2 per angolo)
#define NUM_VL53L0X 8
#define XSHUT_PIN_START 22  // Pin XSHUT dal 22 al 29

// Pin ultrasuoni
#define US_FRONT_TRIG 30
#define US_FRONT_ECHO 31
#define US_LEFT_TRIG 32
#define US_LEFT_ECHO 33
#define US_RIGHT_TRIG 34
#define US_RIGHT_ECHO 35

// Costanti labirinto
#define TILE_SIZE 300        // 30 cm in mm
#define ROBOT_SIZE 200       // 20 cm in mm
#define WALL_THRESHOLD 250   // Soglia per rilevare muro (mm)
#define CENTER_TOLERANCE 10  // Tolleranza centratura (mm)
#define ANGLE_TOLERANCE 3    // Tolleranza angolo (gradi)

// Array sensori VL53L0X
VL53L0X sensors[NUM_VL53L0X];
uint8_t sensorAddresses[NUM_VL53L0X] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

// Posizionamento sensori (2 per angolo)
// Angolo anteriore-sinistro: 0, 1
// Angolo anteriore-destro: 2, 3
// Angolo posteriore-destro: 4, 5
// Angolo posteriore-sinistro: 6, 7

struct SensorData {
  uint16_t distFL1, distFL2;  // Front-Left corner
  uint16_t distFR1, distFR2;  // Front-Right corner
  uint16_t distBR1, distBR2;  // Back-Right corner
  uint16_t distBL1, distBL2;  // Back-Left corner
  uint16_t usFront, usLeft, usRight;
};

SensorData sensorData;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000); // I2C a 400kHz
  
  // Setup ultrasuoni
  pinMode(US_FRONT_TRIG, OUTPUT);
  pinMode(US_FRONT_ECHO, INPUT);
  pinMode(US_LEFT_TRIG, OUTPUT);
  pinMode(US_LEFT_ECHO, INPUT);
  pinMode(US_RIGHT_TRIG, OUTPUT);
  pinMode(US_RIGHT_ECHO, INPUT);
  
  // Inizializza VL53L0X
  initVL53L0X();
  
  Serial.println("Sistema di allineamento inizializzato");
}

void loop() {
  // Leggi tutti i sensori
  readAllSensors();
  
  // Analizza posizione e allineamento
  analyzeAlignment();
  
  delay(100);
}

void initVL53L0X() {
  // Imposta tutti i pin XSHUT come output e mettili LOW
  for (int i = 0; i < NUM_VL53L0X; i++) {
    pinMode(XSHUT_PIN_START + i, OUTPUT);
    digitalWrite(XSHUT_PIN_START + i, LOW);
  }
  delay(10);
  
  // Inizializza sensori uno alla volta
  for (int i = 0; i < NUM_VL53L0X; i++) {
    digitalWrite(XSHUT_PIN_START + i, HIGH);
    delay(10);
    
    sensors[i].setTimeout(500);
    if (!sensors[i].init()) {
      Serial.print("Errore inizializzazione sensore ");
      Serial.println(i);
      while(1);
    }
    
    sensors[i].setAddress(sensorAddresses[i]);
    sensors[i].setMeasurementTimingBudget(20000); // 20ms per lettura veloce
    
    Serial.print("Sensore ");
    Serial.print(i);
    Serial.println(" inizializzato");
  }
}

void readAllSensors() {
  // Leggi VL53L0X
  sensorData.distFL1 = sensors[0].readRangeSingleMillimeters();
  sensorData.distFL2 = sensors[1].readRangeSingleMillimeters();
  sensorData.distFR1 = sensors[2].readRangeSingleMillimeters();
  sensorData.distFR2 = sensors[3].readRangeSingleMillimeters();
  sensorData.distBR1 = sensors[4].readRangeSingleMillimeters();
  sensorData.distBR2 = sensors[5].readRangeSingleMillimeters();
  sensorData.distBL1 = sensors[6].readRangeSingleMillimeters();
  sensorData.distBL2 = sensors[7].readRangeSingleMillimeters();
  
  // Leggi ultrasuoni
  sensorData.usFront = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  sensorData.usLeft = readUltrasonic(US_LEFT_TRIG, US_LEFT_ECHO);
  sensorData.usRight = readUltrasonic(US_RIGHT_TRIG, US_RIGHT_ECHO);
}

uint16_t readUltrasonic(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 8190; // Timeout
  
  return (duration * 0.343) / 2; // mm
}

void analyzeAlignment() {
  // Calcola distanze medie per ogni lato
  uint16_t frontLeft = min(sensorData.distFL1, sensorData.distFL2);
  uint16_t frontRight = min(sensorData.distFR1, sensorData.distFR2);
  uint16_t backLeft = min(sensorData.distBL1, sensorData.distBL2);
  uint16_t backRight = min(sensorData.distBR1, sensorData.distBR2);
  
  // Rileva presenza muri
  bool wallFront = sensorData.usFront < WALL_THRESHOLD;
  bool wallLeft = sensorData.usLeft < WALL_THRESHOLD;
  bool wallRight = sensorData.usRight < WALL_THRESHOLD;
  
  Serial.println("\n--- Stato Sensori ---");
  Serial.print("Front L/R: "); Serial.print(frontLeft); Serial.print(" / "); Serial.println(frontRight);
  Serial.print("Back L/R: "); Serial.print(backLeft); Serial.print(" / "); Serial.println(backRight);
  Serial.print("Muri - F:"); Serial.print(wallFront); Serial.print(" L:"); Serial.print(wallLeft); 
  Serial.print(" R:"); Serial.println(wallRight);
  
  // Calcola errori di allineamento
  int16_t lateralError = 0;
  int16_t angularError = 0;
  
  // Allineamento laterale (sinistra-destra)
  if (wallLeft && wallRight) {
    // Entrambi i muri presenti - centra tra i due
    lateralError = sensorData.usLeft - sensorData.usRight;
    
    // Calcola errore angolare
    int16_t leftDiff = sensorData.distFL1 - sensorData.distBL1;
    int16_t rightDiff = sensorData.distFR1 - sensorData.distBR1;
    angularError = (leftDiff + rightDiff) / 2;
    
    Serial.print("Centratura L/R - Errore: "); Serial.print(lateralError); Serial.println(" mm");
    
  } else if (wallLeft) {
    // Solo muro sinistro - mantieni distanza fissa
    int16_t targetDist = (TILE_SIZE - ROBOT_SIZE) / 2; // Distanza ideale dal muro
    lateralError = sensorData.usLeft - targetDist;
    
    // Errore angolare dal lato sinistro
    angularError = sensorData.distFL1 - sensorData.distBL1;
    
    Serial.print("Allineamento a SX - Errore: "); Serial.print(lateralError); Serial.println(" mm");
    
  } else if (wallRight) {
    // Solo muro destro - mantieni distanza fissa
    int16_t targetDist = (TILE_SIZE - ROBOT_SIZE) / 2;
    lateralError = targetDist - sensorData.usRight;
    
    // Errore angolare dal lato destro
    angularError = sensorData.distFR1 - sensorData.distBR1;
    
    Serial.print("Allineamento a DX - Errore: "); Serial.print(lateralError); Serial.println(" mm");
  }
  
  // Verifica allineamento
  bool isAligned = (abs(lateralError) < CENTER_TOLERANCE) && 
                   (abs(angularError) < ANGLE_TOLERANCE);
  
  Serial.print("Errore angolare: "); Serial.print(angularError); Serial.println(" mm");
  Serial.print("ALLINEATO: "); Serial.println(isAligned ? "SI" : "NO");
  
  if (!isAligned) {
    Serial.println("Correzione necessaria:");
    if (abs(lateralError) >= CENTER_TOLERANCE) {
      Serial.print("  - Spostamento laterale: ");
      Serial.print(lateralError > 0 ? "DESTRA " : "SINISTRA ");
      Serial.print(abs(lateralError)); Serial.println(" mm");
    }
    if (abs(angularError) >= ANGLE_TOLERANCE) {
      Serial.print("  - Rotazione: ");
      Serial.print(angularError > 0 ? "ORARIO " : "ANTIORARIO ");
      Serial.print(abs(angularError)); Serial.println(" mm");
    }
  }
}

// Funzioni di utilità per il controllo motori (da implementare)
void correctAlignment(int16_t lateralError, int16_t angularError) {
  // TODO: implementa controllo motori per correzione
  // lateralError: positivo = spostare a destra, negativo = spostare a sinistra
  // angularError: positivo = ruotare orario, negativo = ruotare antiorario
}

bool isWallPresent(uint16_t distance) {
  return distance < WALL_THRESHOLD && distance > 20;
}

float getDistanceFromWall(uint16_t sensor1, uint16_t sensor2) {
  // Usa il minimo tra i due sensori dell'angolo
  return min(sensor1, sensor2);
}
