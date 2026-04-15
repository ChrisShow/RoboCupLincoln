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

// ---------------------------------------------------------------
// Soglie geometricamente derivate — robot 22cm, cella 30x30cm
//
//  Scocca 17×10cm. Centro rotazione: 8.5cm ant/post, 5.0cm lat.
//  Front ToF  : 8.5cm avanti  → lettura muro frontale centrato = 15-8.5 = 6.5cm
//  Rear ToF   : 8.5cm dietro  → lettura muro posteriore centrato = 6.5cm
//  Side ToF   : 5.0cm laterale→ lettura muro laterale centrato  = 15-5.0 = 10.0cm
//  Ruote      : 8.0cm laterale→ clearance muro centrato = 15-8.0 = 7.0cm
//  Sens.colore: 12.5cm avanti → clearance muro frontale = 15-12.5 = 2.5cm
// ---------------------------------------------------------------
const float SOGLIA_MURO_FRONT    = 25.0;  // cm — discrimina muro(6.5) da apertura(36.5+)
const float SOGLIA_MURO_LAT      = 18.0;  // cm — discrimina muro(10) da apertura(40+)
const float TARGET_LAT_DIST      = 10.0;  // cm — distanza ideale ToF laterale ↔ muro
const float TARGET_FRONT_DIST    =  6.5;  // cm — stop al centro cella (15-8.5=6.5)
const float DIST_PERICOLO_RUOTA  =  4.5;  // cm — ruota a 1.5cm dal muro (contatto a 3cm)
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
//  LOGGING — LCD + Seriale debug
// ============================================================

// Mostra due righe sul display LCD (tronca a 16 char per riga)
void lcdShow(String line1, String line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1); lcd.print(line2.substring(0, 16));
}

// Invia messaggio di debug al Raspberry (prefisso [D] per distinguerlo dai pacchetti)
void dbg(String msg) {
  Serial.println("[D] " + msg);
}

// ============================================================
//  ROTAZIONE PID con anti-windup
// ============================================================

void rotate(float rel_deg) {
  String dirStr = (rel_deg > 0) ? "+" + String((int)rel_deg) + "deg DX"
                                : String((int)rel_deg) + "deg SX";
  lcdShow("ROTAZIONE", dirStr);
  dbg("Rotazione " + dirStr + " target=" + String(mpu.getAngleZ() + rel_deg, 1));

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
  delay(100);
  dbg("Rotazione OK  yaw=" + String(mpu.getAngleZ(), 1));
}

// ============================================================
//  SCAN SENSORI → invia <DATA:> al Raspberry
// ============================================================

void scanSensors() {
  tof.ts = 0;
  updateToF();

  int c = getDetectedColor();
  int F = (tof.front <= SOGLIA_MURO_FRONT) ? 1 : 0;
  int R = (((tof.ra + tof.rp) / 2.0f) <= SOGLIA_MURO_LAT) ? 1 : 0;
  int L = (((tof.la + tof.lp) / 2.0f) <= SOGLIA_MURO_LAT) ? 1 : 0;

  // Log su LCD: muri F/R/L e colore pavimento
  const char* colori[] = {"NORM","GREY","REDD","BLUE","BLCK"};
  String muri = "F:"; muri += F; muri += " R:"; muri += R; muri += " L:"; muri += L;
  String pavimento = "Col:"; pavimento += colori[c];
  lcdShow("SCAN " + muri, pavimento);

  // Log debug su seriale
  dbg("SCAN F:" + String(tof.front,1) + "cm R:" + String((tof.ra+tof.rp)/2,1)
      + "cm L:" + String((tof.la+tof.lp)/2,1) + "cm Col:" + String(c));

  Serial.println("<DATA:" + String(c) + "," + String(F) + String(R) + String(L) + ">");
}

// ============================================================
//  AVANZAMENTO con anti-drift angolare migliorato
// ============================================================

void moveForward() {
  lcdShow("AVANZAMENTO...", "");
  dbg("Avanzamento iniziato");

  moveCounter  = 0;
  lastMoveTime = millis();
  float baseSpeed = 130;
  bool  rampMode  = false;

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
      if (!rampMode) {
        bool salita = (pitch > 0);
        lcdShow("RAMPA " + String(salita ? "SALITA" : "DISCESA"),
                "Pitch:" + String(pitch, 1));
        dbg("Rampa rilevata pitch=" + String(pitch, 1) + (salita ? " SALITA" : " DISCESA"));
      }
      rampMode = true;
      int power = (pitch > RAMP_PITCH_ENTER) ? 230 : 80;
      setMotors(power, power);
      continue;
    }

    if (rampMode && abs(pitch) < RAMP_PITCH_EXIT) {
      stopMotors();
      lcdShow("RAMPA OK", "2 celle avanzate");
      dbg("Rampa completata pitch=" + String(pitch, 1));
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
      lcdShow("CELLA NERA!", "Ritorno...");
      dbg("Cella NERA rilevata! Retromarcia per " + String(moveCounter * 20) + "ms");
      setMotors(-150, -150);
      delay(moveCounter * 20);
      stopMotors();
      lcdShow("CELLA NERA!", "Rientrato OK");
      dbg("Rientro completato");
      Serial.println("<MOVE:BLACK>");
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

    // Rilevazione dinamica muro frontale:
    // All'avvio dF_start = 36.5cm (muro in fondo alla cella successiva) → wallFront=false.
    // Durante il movimento dF scende; quando entra nel range lo iniziamo a tracciare.
    if (!wallFront && dF < SOGLIA_MURO_FRONT) {
      wallFront = true;  // Muro entrato nel range → ora tracciamo la distanza
    }

    if (wallFront && dF <= TARGET_FRONT_DIST)          break; // Centro cella: ToF=6.5cm
    if (wallBack  && dB >= (dB_start + 29.5f))         break; // Muro post: percorsi ~29.5cm
    if (!wallFront && !wallBack && moveCounter >= 90)  break; // Fail-safe ~30cm
  }

  stopMotors();
  dbg("Avanzamento OK  F:" + String(tof.front,1) + "cm cnt=" + String(moveCounter));
  lcdShow("AVANZ. OK", "F:" + String(tof.front,1) + "cm");
  Serial.println("<MOVE:OK>");
  scanSensors();
}

// ============================================================
//  SOCCORSO FISICO
// ============================================================

void handleRescue(char pos, int tipo) {
  if (tipo < 1 || tipo > 3) {
    dbg("Rescue IGNORATO tipo=" + String(tipo));
    Serial.println("<VIC:DONE>");
    return;
  }

  const char* gravita[] = {"", "ILLESA", "STABILE", "GRAVE"};
  lcdShow("SOCCORSO " + String(pos), String(gravita[tipo]) + " " + String(tipo) + " kit");
  dbg("Soccorso pos=" + String(pos) + " tipo=" + String(tipo)
      + " (" + String(gravita[tipo]) + ")");

  digitalWrite(LED_STATUS, HIGH);

  if      (pos == 'R') rotate(-90);
  else if (pos == 'L') rotate( 90);
  else if (pos == 'F') rotate(180);

  for (int i = 0; i < tipo; i++) {
    lcdShow("EROGAZ. KIT", String(i+1) + "/" + String(tipo));
    dbg("Erogo kit " + String(i+1) + "/" + String(tipo));
    medikitServo.write(0);   delay(1000);
    medikitServo.write(90);  delay(500);
  }

  lcdShow("RIENTRO ORI.", "");
  if      (pos == 'R') rotate( 90);
  else if (pos == 'L') rotate(-90);
  else if (pos == 'F') rotate(180);

  digitalWrite(LED_STATUS, LOW);
  dbg("Soccorso completato");
  Serial.println("<VIC:DONE>");
}

// ============================================================
//  GESTIONE COMANDI SERIALI
// ============================================================

void handleCommand(String cmd) {
  dbg("CMD ricevuto: " + cmd);

  if (cmd == "SCAN:FRONT") {
    lcdShow("SCAN FRONT", "Rotaz. -90deg");
    dbg("Rotazione per scansione frontale");
    rotate(-90);
    Serial.println("<SCAN:OK>");
  }
  else if (cmd == "SCAN:DONE") {
    lcdShow("SCAN DONE", "Rotaz. +90deg");
    dbg("Ripristino orientamento post-scan");
    rotate(90);
  }
  else if (cmd == "VIC:DONE") {
    dbg("Lista vittime ricevuta - nessuna vittima");
    Serial.println("<VIC:DONE>");
  }
  else if (cmd.startsWith("VIC:")) {
    char pos  = cmd.charAt(4);
    int  tipo = cmd.substring(6).toInt();
    handleRescue(pos, tipo);
  }
  else if (cmd.startsWith("GO:")) {
    int d = cmd.substring(3).toInt();
    const char* dirs[] = {"AVANTI","DESTRA","DIETRO","SINISTRA"};
    lcdShow("GO: " + String(dirs[d]), "");
    dbg("Movimento verso: " + String(dirs[d]));
    if      (d == 1) rotate( 90);
    else if (d == 2) rotate(180);
    else if (d == 3) rotate(-90);
    moveForward();
  }
  else if (cmd.startsWith("WAIT:")) {
    int secs = cmd.substring(5).toInt();
    lcdShow("CELLA BLU", "Attesa " + String(secs) + "s...");
    dbg("Attesa cella BLU: " + String(secs) + "s");
    delay((long)secs * 1000);
    lcdShow("mENIAC 2026", "RUNNING");
  }
  else {
    dbg("CMD sconosciuto: " + cmd);
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
      lcdShow("mENIAC 2026", "GARA INIZIATA!");
      dbg("=== GARA INIZIATA ===");
      Serial.println("<INIT>");
      delay(100);
      scanSensors();
    } else {
      stopMotors();
      lcdShow("mENIAC 2026", "STOPPED");
      dbg("=== GARA FERMATA ===");
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
