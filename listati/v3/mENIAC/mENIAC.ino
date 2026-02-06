#include <Wire.h>
#include <VL53L0X.h>

// Pin pulsante START
#define START_BUTTON_PIN 2  // Usa pin con interrupt
#define LED_PIN 13          // LED di stato

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

// Stati del sistema
enum SystemState {
  STATE_IDLE,           // In attesa di START
  STATE_COUNTDOWN,      // Countdown 5 secondi
  STATE_RUNNING,        // In esecuzione
  STATE_STOPPED         // Fermo (premuto di nuovo)
};

SystemState currentState = STATE_IDLE;
unsigned long countdownStart = 0;
bool systemRunning = false;

// Array sensori VL53L0X
VL53L0X sensors[NUM_VL53L0X];
uint8_t sensorAddresses[NUM_VL53L0X] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

struct SensorData {
  uint16_t distFL1, distFL2;  // Front-Left corner
  uint16_t distFR1, distFR2;  // Front-Right corner
  uint16_t distBR1, distBR2;  // Back-Right corner
  uint16_t distBL1, distBL2;  // Back-Left corner
  uint16_t usFront, usLeft, usRight;
};

SensorData sensorData;

struct WallAnalysis {
  bool present;
  bool parallel;
  int16_t angleDiff;
  String alignment;
};

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);
  
  // Setup pulsante START
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Setup ultrasuoni
  pinMode(US_FRONT_TRIG, OUTPUT);
  pinMode(US_FRONT_ECHO, INPUT);
  pinMode(US_LEFT_TRIG, OUTPUT);
  pinMode(US_LEFT_ECHO, INPUT);
  pinMode(US_RIGHT_TRIG, OUTPUT);
  pinMode(US_RIGHT_ECHO, INPUT);
  
  // Inizializza VL53L0X
  Serial.println("Inizializzazione sensori...");
  initVL53L0X();
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║  ROBOCUP RESCUE MAZE - READY   ║");
  Serial.println("╚════════════════════════════════╝");
  Serial.println("\n>> Premi il pulsante START per iniziare <<\n");
  
  // LED lampeggia per indicare pronto
  blinkReady();
}

void loop() {
  // Gestione stato macchina
  handleStateMachine();
  
  // Se in esecuzione, esegui l'allineamento
  if (currentState == STATE_RUNNING) {
    readAllSensors();
    analyzeAllWalls();
    analyzeAlignment();
  }
  
  delay(100);
}

void handleStateMachine() {
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(START_BUTTON_PIN);
  
  // Rileva pressione pulsante (fronte di discesa)
  if (lastButtonState == HIGH && buttonState == LOW) {
    delay(50); // Debounce
    
    switch (currentState) {
      case STATE_IDLE:
        startCountdown();
        break;
        
      case STATE_COUNTDOWN:
        // Durante countdown, premere di nuovo annulla
        cancelCountdown();
        break;
        
      case STATE_RUNNING:
        // Durante esecuzione, STOP
        stopSystem();
        break;
        
      case STATE_STOPPED:
        // Da fermo, riavvia countdown
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
      // LED lampeggia durante countdown
      digitalWrite(LED_PIN, (millis() / 500) % 2);
      
      // Stampa countdown ogni secondo
      static int lastPrinted = -1;
      if (remaining != lastPrinted) {
        Serial.print(">>> Avvio tra ");
        Serial.print(remaining);
        Serial.println(" secondi...");
        lastPrinted = remaining;
      }
    } else {
      // Countdown terminato - AVVIA
      startSystem();
    }
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

void startSystem() {
  currentState = STATE_RUNNING;
  systemRunning = true;
  digitalWrite(LED_PIN, HIGH); // LED fisso ON
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║        SISTEMA ATTIVO!         ║");
  Serial.println("║   Allineamento in corso...     ║");
  Serial.println("╚════════════════════════════════╝\n");
  
  // TODO: Avvia motori / sistema di navigazione
}

void stopSystem() {
  currentState = STATE_STOPPED;
  systemRunning = false;
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║      SISTEMA FERMATO!          ║");
  Serial.println("╚════════════════════════════════╝\n");
  Serial.println(">> Premi START per riavviare <<\n");
  
  // TODO: Ferma motori
}

void blinkReady() {
  // Lampeggio veloce per 3 volte = PRONTO
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(150);
  }
  digitalWrite(LED_PIN, LOW);
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
      Serial.print("❌ Errore sensore ");
      Serial.println(i);
      while(1) {
        digitalWrite(LED_PIN, (millis() / 200) % 2); // Lampeggio errore
      }
    }
    
    sensors[i].setAddress(sensorAddresses[i]);
    sensors[i].setMeasurementTimingBudget(20000);
    
    Serial.print("✓ Sensore ");
    Serial.print(i);
    Serial.println(" OK");
  }
}

void readAllSensors() {
  // Leggi solo se sistema attivo
  if (!systemRunning) return;
  
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
  if (duration == 0) return 8190;
  
  return (duration * 0.343) / 2;
}

WallAnalysis analyzeWall(uint16_t frontDist, uint16_t backDist, uint16_t usDistance) {
  WallAnalysis result;
  
  result.present = (usDistance < WALL_THRESHOLD && usDistance > 20);
  
  if (!result.present) {
    result.parallel = true;
    result.angleDiff = 0;
    result.alignment = "NO_WALL";
    return result;
  }
  
  result.angleDiff = frontDist - backDist;
  result.parallel = (abs(result.angleDiff) < ANGLE_TOLERANCE);
  
  if (result.parallel) {
    result.alignment = "PARALLEL";
  } else if (result.angleDiff > 0) {
    result.alignment = "DIVERGING";
  } else {
    result.alignment = "CONVERGING";
  }
  
  return result;
}

void analyzeAllWalls() {
  if (!systemRunning) return;
  
  Serial.println("\n=== ANALISI MURI ===");
  
  // Analizza lato SINISTRO
  WallAnalysis leftWall = analyzeWall(
    min(sensorData.distFL1, sensorData.distFL2),
    min(sensorData.distBL1, sensorData.distBL2),
    sensorData.usLeft
  );
  
  Serial.print("SINISTRA: ");
  Serial.print(leftWall.alignment);
  if (leftWall.present && !leftWall.parallel) {
    Serial.print(" (diff: ");
    Serial.print(leftWall.angleDiff);
    Serial.print("mm)");
  }
  Serial.println();
  
  // Analizza lato DESTRO
  WallAnalysis rightWall = analyzeWall(
    min(sensorData.distFR1, sensorData.distFR2),
    min(sensorData.distBR1, sensorData.distBR2),
    sensorData.usRight
  );
  
  Serial.print("DESTRA: ");
  Serial.print(rightWall.alignment);
  if (rightWall.present && !rightWall.parallel) {
    Serial.print(" (diff: ");
    Serial.print(rightWall.angleDiff);
    Serial.print("mm)");
  }
  Serial.println();
  
  // Analizza lato FRONTALE
  bool frontWall = (sensorData.usFront < WALL_THRESHOLD);
  int16_t frontLeftCorner = min(sensorData.distFL1, sensorData.distFL2);
  int16_t frontRightCorner = min(sensorData.distFR1, sensorData.distFR2);
  int16_t frontDiff = frontLeftCorner - frontRightCorner;
  
  Serial.print("FRONTE: ");
  if (frontWall) {
    if (abs(frontDiff) < ANGLE_TOLERANCE) {
      Serial.println("PARALLEL");
    } else {
      Serial.print(frontDiff > 0 ? "TILTED_LEFT" : "TILTED_RIGHT");
      Serial.print(" (diff: ");
      Serial.print(frontDiff);
      Serial.println("mm)");
    }
  } else {
    Serial.println("NO_WALL");
  }
  
  detectSpecialCases(leftWall, rightWall, frontWall);
}

void detectSpecialCases(WallAnalysis left, WallAnalysis right, bool front) {
  Serial.println("\n--- Situazione rilevata ---");
  
  if (!left.present && !right.present && front) {
    Serial.println("⚠ POSSIBILE OSTACOLO CENTRALE");
  }
  
  if (left.present && right.present && left.parallel && right.parallel) {
    int16_t robotRotation = (left.angleDiff + right.angleDiff) / 2;
    if (abs(robotRotation) > ANGLE_TOLERANCE) {
      Serial.print("⚠ ROBOT RUOTATO: ");
      Serial.print(robotRotation > 0 ? "senso orario" : "senso antiorario");
      Serial.print(" (");
      Serial.print(abs(robotRotation));
      Serial.println("mm)");
    } else {
      Serial.println("✓ Robot allineato correttamente");
    }
  }
  
  if ((left.present && !left.parallel) || (right.present && !right.parallel)) {
    Serial.println("⚠ MURO STORTO O OSTACOLO INCLINATO");
    
    if (left.present && !left.parallel) {
      Serial.print("  Lato SX: ");
      Serial.println(left.alignment);
    }
    if (right.present && !right.parallel) {
      Serial.print("  Lato DX: ");
      Serial.println(right.alignment);
    }
  }
  
  if (left.present && right.present) {
    if (!left.parallel && !right.parallel) {
      bool narrowing = (left.angleDiff < 0 && right.angleDiff > 0);
      bool widening = (left.angleDiff > 0 && right.angleDiff < 0);
      
      if (narrowing) {
        Serial.println("⚠ CORRIDOIO CHE SI RESTRINGE");
      } else if (widening) {
        Serial.println("⚠ CORRIDOIO CHE SI ALLARGA");
      }
    }
  }
}

void analyzeAlignment() {
  if (!systemRunning) return;
  
  uint16_t frontLeft = min(sensorData.distFL1, sensorData.distFL2);
  uint16_t frontRight = min(sensorData.distFR1, sensorData.distFR2);
  uint16_t backLeft = min(sensorData.distBL1, sensorData.distBL2);
  uint16_t backRight = min(sensorData.distBR1, sensorData.distBR2);
  
  bool wallFront = sensorData.usFront < WALL_THRESHOLD;
  bool wallLeft = sensorData.usLeft < WALL_THRESHOLD;
  bool wallRight = sensorData.usRight < WALL_THRESHOLD;
  
  Serial.println("\n--- Stato Sensori ---");
  Serial.print("Front L/R: "); Serial.print(frontLeft); Serial.print(" / "); Serial.println(frontRight);
  Serial.print("Back L/R: "); Serial.print(backLeft); Serial.print(" / "); Serial.println(backRight);
  Serial.print("Muri - F:"); Serial.print(wallFront); Serial.print(" L:"); Serial.print(wallLeft); 
  Serial.print(" R:"); Serial.println(wallRight);
  
  int16_t lateralError = 0;
  int16_t angularError = 0;
  
  if (wallLeft && wallRight) {
    lateralError = sensorData.usLeft - sensorData.usRight;
    int16_t leftDiff = sensorData.distFL1 - sensorData.distBL1;
    int16_t rightDiff = sensorData.distFR1 - sensorData.distBR1;
    angularError = (leftDiff + rightDiff) / 2;
    
    Serial.print("Centratura L/R - Errore: "); Serial.print(lateralError); Serial.println(" mm");
    
  } else if (wallLeft) {
    int16_t targetDist = (TILE_SIZE - ROBOT_SIZE) / 2;
    lateralError = sensorData.usLeft - targetDist;
    angularError = sensorData.distFL1 - sensorData.distBL1;
    
    Serial.print("Allineamento a SX - Errore: "); Serial.print(lateralError); Serial.println(" mm");
    
  } else if (wallRight) {
    int16_t targetDist = (TILE_SIZE - ROBOT_SIZE) / 2;
    lateralError = targetDist - sensorData.usRight;
    angularError = sensorData.distFR1 - sensorData.distBR1;
    
    Serial.print("Allineamento a DX - Errore: "); Serial.print(lateralError); Serial.println(" mm");
  }
  
  bool isAligned = (abs(lateralError) < CENTER_TOLERANCE) && 
                   (abs(angularError) < ANGLE_TOLERANCE);
  
  Serial.print("Errore angolare: "); Serial.print(angularError); Serial.println(" mm");
  Serial.print("ALLINEATO: "); Serial.println(isAligned ? "✓ SI" : "✗ NO");
  
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
