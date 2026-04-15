/*
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * Descrizione: Firmware Arduino Mega 2560
 *   - Calibrazione EEPROM interattiva
 *   - PID rotazione con anti-windup
 *   - Anti-drift con correzione angolare
 *   - scanSensors() dopo MOVE:BLACK
 *   - Cache letture ToF per ridurre latenza loop
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <MPU6050_light.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <EEPROM.h>

// --- PIN DEFINITIONS ---
#define XSHUT_FRONT   30
#define XSHUT_BACK    31
#define XSHUT_DX_ANT  32
#define XSHUT_DX_POST 33
#define XSHUT_SX_ANT  34
#define XSHUT_SX_POST 35

#define PWM_DX  2
#define DX_IN1  22
#define DX_IN2  23
#define PWM_SX  3
#define SX_IN1  24
#define SX_IN2  25

#define TCS_S0  38
#define TCS_S1  39
#define TCS_S2  40
#define TCS_S3  41
#define TCS_OUT 42

#define SERVO_PIN  4
#define BTN_START  43
#define LED_STATUS 45

// --- STRUTTURE DATI ---
struct ColorRGB {
  int r, g, b;
};

struct RobotConfig {
  ColorRGB normal;
  ColorRGB grey;
  ColorRGB red;
  ColorRGB blue;
  ColorRGB black;
  float kp_rot, ki_rot, kd_rot;
  long magic;
};

// Cache letture ToF: aggioranta ogni TOF_REFRESH_MS millisecondi
struct ToFCache {
  float front, back, ra, rp, la, lp;
  unsigned long ts;
};

// --- COSTANTI ---
const long  MAGIC_VAL        = 0xABCD1234;
const int   TOF_REFRESH_MS   = 15;   // Frequenza refresh cache sensori (ms)

// Soglie ricalibrate per robot 22 cm in cella 30x30 cm
const float SOGLIA_MURO_FRONT    = 25.0;  // cm — cella davanti occupata
const float SOGLIA_MURO_LAT      = 18.0;  // cm — muro laterale presente
const float TARGET_LAT_DIST      = 10.0;  // cm — distanza ideale da muro lat.
const float TARGET_FRONT_DIST    =  6.0;  // cm — stop al centro cella (fronte)
const float DIST_PERICOLO_RUOTA  =  4.5;  // cm — emergenza contatto ruota
const float RAMP_PITCH_ENTER     = 15.0;  // gradi — ingresso modalità rampa
const float RAMP_PITCH_EXIT      =  5.0;  // gradi — uscita modalità rampa

// Guadagni PID rotazione (sovrascrivibili via EEPROM)
const float KP_ROT_DEFAULT = 4.5;
const float KI_ROT_DEFAULT = 0.1;
const float KD_ROT_DEFAULT = 1.2;

// Anti-windup: limita l'accumulazione dell'integrale PID
const float INTEGRAL_CLAMP = 200.0;

// --- GLOBALI ---
RobotConfig config;
ToFCache    tof;

VL53L0X sensors[6];
int xshutPins[] = {
  XSHUT_FRONT, XSHUT_BACK,
  XSHUT_DX_ANT, XSHUT_DX_POST,
  XSHUT_SX_ANT, XSHUT_SX_POST
};

MPU6050          mpu(Wire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo            medikitServo;

bool         isRunning   = false;
int          moveCounter = 0;
unsigned long lastMoveTime = 0;

// ============================================================
//  SENSORE COLORE
// ============================================================

void readRawColor(int &r, int &g, int &b) {
  digitalWrite(TCS_S2, LOW);  digitalWrite(TCS_S3, LOW);  r = pulseIn(TCS_OUT, LOW, 20000);
  digitalWrite(TCS_S2, HIGH); digitalWrite(TCS_S3, HIGH); g = pulseIn(TCS_OUT, LOW, 20000);
  digitalWrite(TCS_S2, LOW);  digitalWrite(TCS_S3, HIGH); b = pulseIn(TCS_OUT, LOW, 20000);
}

void calibrateColors() {
  lcd.clear(); lcd.print("MODE: CALIBRA");
  delay(2000);

  const char* names[]    = {"BIANCO/NORM", "GRIGIO/CHK", "ROSSO/DANGER", "BLU/DELAY", "NERO/TRAP"};
  ColorRGB*   targets[]  = {&config.normal, &config.grey, &config.red, &config.blue, &config.black};

  for (int i = 0; i < 5; i++) {
    lcd.clear(); lcd.print("POSIZIONA SU:"); lcd.setCursor(0, 1); lcd.print(names[i]);
    while (digitalRead(BTN_START) == HIGH);
    delay(500);
    int r, g, b;
    readRawColor(r, g, b);
    targets[i]->r = r; targets[i]->g = g; targets[i]->b = b;
    lcd.clear(); lcd.print("OK! RILASCIA");
    while (digitalRead(BTN_START) == LOW);
    delay(500);
  }

  config.kp_rot = KP_ROT_DEFAULT;
  config.ki_rot = KI_ROT_DEFAULT;
  config.kd_rot = KD_ROT_DEFAULT;
  config.magic  = MAGIC_VAL;
  EEPROM.put(0, config);
  lcd.clear(); lcd.print("SALVATO!");
  delay(1000);
}

int getDetectedColor() {
  int r, g, b;
  readRawColor(r, g, b);
  // Distanza euclidea al quadrato (cast a long per evitare overflow 16-bit)
  long d_norm  = sq((long)r - config.normal.r) + sq((long)g - config.normal.g) + sq((long)b - config.normal.b);
  long d_grey  = sq((long)r - config.grey.r)   + sq((long)g - config.grey.g)   + sq((long)b - config.grey.b);
  long d_red   = sq((long)r - config.red.r)    + sq((long)g - config.red.g)    + sq((long)b - config.red.b);
  long d_blue  = sq((long)r - config.blue.r)   + sq((long)g - config.blue.g)   + sq((long)b - config.blue.b);
  long d_black = sq((long)r - config.black.r)  + sq((long)g - config.black.g)  + sq((long)b - config.black.b);

  long minDist = d_norm; int winner = 0;
  if (d_grey  < minDist) { minDist = d_grey;  winner = 1; }
  if (d_red   < minDist) { minDist = d_red;   winner = 2; }
  if (d_blue  < minDist) { minDist = d_blue;  winner = 3; }
  if (d_black < minDist) {                    winner = 4; }
  return winner;
}

// ============================================================
//  CACHE ToF — legge i sensori al massimo ogni TOF_REFRESH_MS
// ============================================================

void updateToF() {
  if (millis() - tof.ts < TOF_REFRESH_MS) return;
  tof.front = sensors[0].readRangeContinuousMillimeters() / 10.0f;
  tof.back  = sensors[1].readRangeContinuousMillimeters() / 10.0f;
  tof.ra    = sensors[2].readRangeContinuousMillimeters() / 10.0f;
  tof.rp    = sensors[3].readRangeContinuousMillimeters() / 10.0f;
  tof.la    = sensors[4].readRangeContinuousMillimeters() / 10.0f;
  tof.lp    = sensors[5].readRangeContinuousMillimeters() / 10.0f;
  tof.ts    = millis();
}

// ============================================================
//  MOTORI
// ============================================================

void setMotors(int l, int r) {
  // Logica invertita via software: l>=0 (Avanti) → IN1=LOW, IN2=HIGH
  digitalWrite(SX_IN1, l < 0); digitalWrite(SX_IN2, l >= 0);
  digitalWrite(DX_IN1, r < 0); digitalWrite(DX_IN2, r >= 0);
  analogWrite(PWM_SX, abs(l));
  analogWrite(PWM_DX, abs(r));
}

void stopMotors() { setMotors(0, 0); }

// ============================================================
//  ROTAZIONE PID con anti-windup
// ============================================================

void rotate(float rel_deg) {
  mpu.update();
  float target   = mpu.getAngleZ() + rel_deg;
  float lastError = 0;
  float integral  = 0;
  unsigned long startTime = millis();

  while (millis() - startTime < 4000) {
    mpu.update();
    float error      = target - mpu.getAngleZ();
    if (abs(error) <= 1.5) break;

    float derivative = error - lastError;
    // Anti-windup: limita l'integrale per evitare overshoot (fix migl. #3)
    integral = constrain(integral + error, -INTEGRAL_CLAMP, INTEGRAL_CLAMP);

    float output = (error * config.kp_rot)
                 + (integral * config.ki_rot)
                 + (derivative * config.kd_rot);

    setMotors(constrain(-output, -150.0f, 150.0f),
              constrain( output, -150.0f, 150.0f));
    lastError = error;
    delay(10);
  }
  stopMotors();
  delay(100); // Assestamento meccanico
}

// ============================================================
//  SCAN SENSORI → invia <DATA:> al Raspberry
// ============================================================

void scanSensors() {
  // Forza una lettura fresca (ignora la cache)
  tof.ts = 0;
  updateToF();

  int c = getDetectedColor();

  // Media dei due sensori laterali per decidere presenza muro
  int F = (tof.front <= SOGLIA_MURO_FRONT) ? 1 : 0;
  int R = (((tof.ra + tof.rp) / 2.0f) <= SOGLIA_MURO_LAT) ? 1 : 0;
  int L = (((tof.la + tof.lp) / 2.0f) <= SOGLIA_MURO_LAT) ? 1 : 0;

  Serial.println("<DATA:" + String(c) + "," + String(F) + String(R) + String(L) + ">");
}

// ============================================================
//  AVANZAMENTO con anti-drift angolare migliorato
// ============================================================

void moveForward() {
  moveCounter  = 0;
  lastMoveTime = millis();
  float baseSpeed = 130;
  bool  rampMode  = false;

  // Snapshot iniziale per stima distanza percorsa
  updateToF();
  float dF_start = tof.front;
  float dB_start = tof.back;
  bool  wallFront = (dF_start < SOGLIA_MURO_FRONT);
  bool  wallBack  = (dB_start < 15.0f);

  while (true) {
    mpu.update();
    float pitch = mpu.getAngleY();

    // ---- Modalità rampa ----
    if (abs(pitch) > RAMP_PITCH_ENTER) {
      rampMode = true;
      int power = (pitch > RAMP_PITCH_ENTER) ? 230 : 80; // Salita o discesa
      setMotors(power, power);
      continue;
    }

    if (rampMode && abs(pitch) < RAMP_PITCH_EXIT) {
      stopMotors();
      Serial.println("<MOVE:RAMP>");
      scanSensors();
      return;
    }

    // ---- Tick di controllo ogni 20ms ----
    if (millis() - lastMoveTime < 20) continue;
    lastMoveTime = millis();
    moveCounter++;

    // Controllo cella nera
    if (getDetectedColor() == 4) {
      // Arretra proporzionalmente al tempo percorso
      setMotors(-150, -150);
      delay(moveCounter * 20);
      stopMotors();
      Serial.println("<MOVE:BLACK>");
      // fix #1: scanSensors() dopo il ritorno per sbloccare il Raspberry
      scanSensors();
      return;
    }

    // ---- Lettura sensori laterali (via cache) ----
    updateToF();
    float ra = tof.ra, rp = tof.rp;
    float la = tof.la, lp = tof.lp;

    float drift = 0;

    // fix migl. #5: anti-drift con correzione sia centratura che angolazione
    bool hasMuroR = (ra < SOGLIA_MURO_LAT);
    bool hasMuroL = (la < SOGLIA_MURO_LAT);

    if (hasMuroR && hasMuroL) {
      // Corridoio stretto: centra + raddrizza
      float center_err = ra - la;                         // Errore di centratura
      float angle_err  = (ra - rp) - (la - lp);          // Errore di angolazione
      drift = center_err * 6.0f + angle_err * 4.0f;
    } else if (hasMuroR) {
      float angle_err = ra - rp;
      drift = (ra - TARGET_LAT_DIST) * 10.0f + angle_err * 4.0f;
    } else if (hasMuroL) {
      float angle_err = lp - la;
      drift = (TARGET_LAT_DIST - la) * 10.0f + angle_err * 4.0f;
    }

    // Emergenza contatto ruota
    if (ra < DIST_PERICOLO_RUOTA || rp < DIST_PERICOLO_RUOTA) drift = -60;
    if (la < DIST_PERICOLO_RUOTA || lp < DIST_PERICOLO_RUOTA) drift =  60;

    setMotors(baseSpeed + drift, baseSpeed - drift);

    // ---- Condizioni di fine cella ----
    float dF = tof.front;
    float dB = tof.back;

    if (wallFront && dF <= TARGET_FRONT_DIST)       break; // Muro frontale
    if (wallBack  && dB >= (dB_start + 28.0f))      break; // Muro posteriore +28cm
    if (!wallFront && !wallBack && moveCounter >= 85) break; // Fail-safe ~30cm
  }

  stopMotors();
  Serial.println("<MOVE:OK>");
  scanSensors();
}

// ============================================================
//  SOCCORSO FISICO
// ============================================================

void handleRescue(char pos, int tipo) {
  if (tipo < 1 || tipo > 3) {
    // Tipo non valido: ignora ma completa il handshake
    Serial.println("<VIC:DONE>");
    return;
  }

  digitalWrite(LED_STATUS, HIGH);

  // Ruota verso la parete vittima
  if      (pos == 'R') rotate(-90);
  else if (pos == 'L') rotate( 90);
  else if (pos == 'F') rotate(180);

  // Eroga kit (numero cicli = tipo)
  for (int i = 0; i < tipo; i++) {
    medikitServo.write(0);   delay(1000);
    medikitServo.write(90);  delay(500);
  }

  // Torna all'orientamento originale
  if      (pos == 'R') rotate( 90);
  else if (pos == 'L') rotate(-90);
  else if (pos == 'F') rotate(180);

  digitalWrite(LED_STATUS, LOW);
  Serial.println("<VIC:DONE>");
}

// ============================================================
//  GESTIONE COMANDI SERIALI
// ============================================================

void handleCommand(String cmd) {
  if (cmd == "SCAN:FRONT") {
    rotate(-90);
    Serial.println("<SCAN:OK>");
  }
  else if (cmd == "SCAN:DONE") {
    rotate(90);
  }
  else if (cmd == "VIC:DONE") {
    // Raspberry ha terminato la lista vittime, conferma ricezione
    Serial.println("<VIC:DONE>");
  }
  else if (cmd.startsWith("VIC:")) {
    char pos  = cmd.charAt(4);
    int  tipo = cmd.substring(6).toInt();
    handleRescue(pos, tipo);
  }
  else if (cmd.startsWith("GO:")) {
    int d = cmd.substring(3).toInt();
    if      (d == 1) rotate( 90);
    else if (d == 2) rotate(180);
    else if (d == 3) rotate(-90);
    moveForward();
  }
  else if (cmd.startsWith("WAIT:")) {
    int secs = cmd.substring(5).toInt();
    lcd.clear(); lcd.print("ATTESA: "); lcd.print(secs); lcd.print("s");
    delay((long)secs * 1000);
    lcd.clear(); lcd.print("mENIAC 2026");
  }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Wire.begin();
  lcd.init(); lcd.backlight();

  pinMode(BTN_START,  INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT);
  pinMode(DX_IN1, OUTPUT); pinMode(DX_IN2, OUTPUT);
  pinMode(SX_IN1, OUTPUT); pinMode(SX_IN2, OUTPUT);
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  // Scaling TCS3200 al 20%
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW);

  // Carica configurazione da EEPROM
  EEPROM.get(0, config);
  if (digitalRead(BTN_START) == LOW || config.magic != MAGIC_VAL) {
    calibrateColors();
  }

  // Inizializzazione sensori ToF (uno alla volta via XSHUT)
  for (int i = 0; i < 6; i++) { pinMode(xshutPins[i], OUTPUT); digitalWrite(xshutPins[i], LOW); }
  for (int i = 0; i < 6; i++) {
    digitalWrite(xshutPins[i], HIGH); delay(50);
    if (sensors[i].init()) {
      sensors[i].setAddress(0x30 + i);
      sensors[i].startContinuous();
    } else {
      lcd.clear(); lcd.print("ToF ERR: "); lcd.print(i);
      delay(2000);
    }
  }

  // Calibrazione MPU6050
  mpu.begin();
  lcd.clear(); lcd.print("Calibrating MPU");
  mpu.calcOffsets();

  medikitServo.attach(SERVO_PIN);
  medikitServo.write(90);

  // Inizializza cache ToF
  tof.ts = 0;

  lcd.clear(); lcd.print("mENIAC 2026");
  lcd.setCursor(0, 1); lcd.print("READY TO START");
}

// ============================================================
//  LOOP PRINCIPALE
// ============================================================

void loop() {
  mpu.update();
  updateToF(); // Aggiorna cache sensori in background

  // Tasto START: toggle isRunning
  if (digitalRead(BTN_START) == LOW) {
    delay(200);
    isRunning = !isRunning;
    if (isRunning) {
      Serial.println("<INIT>");
      delay(100);
      scanSensors(); // Invia subito i dati della cella iniziale
    } else {
      stopMotors();
      lcd.clear(); lcd.print("STOPPED");
    }
    while (digitalRead(BTN_START) == LOW);
  }

  // Gestione comandi seriali
  if (isRunning && Serial.available()) {
    String cmd = Serial.readStringUntil('>');
    cmd.trim();
    if (cmd.startsWith("<")) {
      handleCommand(cmd.substring(1));
    }
  }

  // Tasto LOP (Lack of Progress): se premuto mentre è in esecuzione
  // Nota: il regolamento prevede che sia il giudice a segnalarlo
  // Arduino notifica il Raspberry che rimuoverà dall'interruzione
  // (implementazione opzionale con secondo pulsante su pin 44)
}
