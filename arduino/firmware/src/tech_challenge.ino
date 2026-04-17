/*
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * File:     tech_challenge.ino
 * Scopo:    Technical Challenge — Zona A (tile cromatiche) + Zona B (bandiere)
 *
 *  Zona A: il sensore colore legge ogni tile e il Pi decide la direzione
 *          BLU → gira sinistra   ROSSA → gira destra   NORM → dritto
 *          GRIGIO → checkpoint, ingresso Zona B
 *
 *  Zona B: il Pi gestisce navigazione e visione; Arduino esegue comandi
 *          <BLINK:N>  → lampeggio N volte (500ms ON / 500ms OFF)
 *          <DROP:N>   → eroga N medikit con servo
 *
 *  Stesse costanti hardware di main_senza_mpu.ino (ROT_SPEED, ROT_90_MS, ecc.)
 */

#include <Wire.h>
#include <VL53L0X.h>
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

#define SERVO_PIN   4
#define BTN_START  43
#define LED_STATUS 45

// --- STRUTTURE ---
struct ColorRGB { int r, g, b; };

struct TechConfig {
  ColorRGB normal;   // beige/crema → codice 0
  ColorRGB grey;     // grigio checkpoint → codice 1
  ColorRGB red;      // rosso Zona A → codice 2
  ColorRGB blue;     // blu Zona A → codice 3
  ColorRGB black;    // nero (non usato in gara, ma utile per calibrazione)
  long magic;
};

struct ToFCache {
  float front, back, ra, rp, la, lp;
  unsigned long ts;
};

// --- COSTANTI ---
const long  MAGIC_TC       = 0xBEEF5678;
const int   TOF_REFRESH_MS = 15;

const float SOGLIA_MURO_FRONT   = 10.0;
const float SOGLIA_MURO_LAT     = 15.0;
const float TARGET_LAT_DIST     = 10.0;
const float TARGET_FRONT_DIST   =  4.0;
const float DIST_PERICOLO_RUOTA =  4.5;

// Rotazione — stesse di main_senza_mpu.ino
const int ROT_SPEED  = 185;
const int ROT_90_MS  = 550;
const int ROT_180_MS = 900;
const int ROT_SETTLE = 100;

const int BASE_SPEED = 60;

const float        SOGLIA_LENTA     = 12.0;
const int          SLOW_SPEED       =  55;
const unsigned long MOVE_TIMEOUT_MS = 3000UL;
const unsigned long BLACK_IGNORE_MS =  250UL;
const int          BLACK_CONFIRM    =    2;
const int          STOP_CONFIRM     =    2;
const int          BACKUP_MS        =  900;
const float        FRONT_STUCK_THR  =  8.0f;
const float        TOF_OPEN_SUBST   = 40.0f;

const int SERVO_STEP     = 40;
const int SERVO_SLOW_MS  = 12;
const int SERVO_RESET_MS =  4;

// Lampeggio Zona B
const int BLINK_ON_MS  = 500;
const int BLINK_OFF_MS = 500;

// --- GLOBALI ---
TechConfig config;
ToFCache   tof;
VL53L0X    sensors[6];
int xshutPins[] = {
  XSHUT_FRONT, XSHUT_BACK,
  XSHUT_DX_ANT, XSHUT_DX_POST,
  XSHUT_SX_ANT, XSHUT_SX_POST
};

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo             medikitServo;

int           servoAngle   = 0;
bool          isRunning    = false;
int           moveCounter  = 0;
unsigned long lastMoveTime = 0;

// ============================================================
//  SENSORE COLORE
//  Codici: 0=NORM  1=GRIGIO(checkpoint)  2=ROSSA(dx)  3=BLU(sx)
// ============================================================

void readRawColor(int &r, int &g, int &b) {
  digitalWrite(TCS_S2, LOW);  digitalWrite(TCS_S3, LOW);  r = pulseIn(TCS_OUT, LOW, 20000);
  digitalWrite(TCS_S2, HIGH); digitalWrite(TCS_S3, HIGH); g = pulseIn(TCS_OUT, LOW, 20000);
  digitalWrite(TCS_S2, LOW);  digitalWrite(TCS_S3, HIGH); b = pulseIn(TCS_OUT, LOW, 20000);
}

void calibrateColors() {
  lcd.clear(); lcd.print("TC CALIBRA");
  delay(2000);

  const char* names[]   = {"BIANCO/NORM", "GRIGIO/CP",  "ROSSA/DX",   "BLU/SX"};
  ColorRGB*   targets[] = {&config.normal, &config.grey, &config.red, &config.blue};

  for (int i = 0; i < 4; i++) {
    lcd.clear(); lcd.print("SU:"); lcd.setCursor(0, 1); lcd.print(names[i]);
    while (digitalRead(BTN_START) == HIGH);
    delay(500);
    int r, g, b;
    readRawColor(r, g, b);
    targets[i]->r = r; targets[i]->g = g; targets[i]->b = b;
    lcd.clear(); lcd.print("OK! RILASCIA");
    while (digitalRead(BTN_START) == LOW);
    delay(500);
  }

  config.magic = MAGIC_TC;
  EEPROM.put(0, config);
  lcd.clear(); lcd.print("SALVATO!");
  delay(1000);
}

int getDetectedColor() {
  int r, g, b;
  readRawColor(r, g, b);
  long d_norm  = sq((long)r-config.normal.r) + sq((long)g-config.normal.g) + sq((long)b-config.normal.b);
  long d_grey  = sq((long)r-config.grey.r)   + sq((long)g-config.grey.g)   + sq((long)b-config.grey.b);
  long d_red   = sq((long)r-config.red.r)    + sq((long)g-config.red.g)    + sq((long)b-config.red.b);
  long d_blue  = sq((long)r-config.blue.r)   + sq((long)g-config.blue.g)   + sq((long)b-config.blue.b);

  long minDist = d_norm; int winner = 0;
  if (d_grey < minDist) { minDist = d_grey; winner = 1; }
  if (d_red  < minDist) { minDist = d_red;  winner = 2; }
  if (d_blue < minDist) {                   winner = 3; }
  return winner;
}

// ============================================================
//  CACHE ToF
// ============================================================

float clampToF(float v) {
  if (v <= 0.5f || v > 200.0f) return TOF_OPEN_SUBST;
  return v;
}

void updateToF() {
  if (millis() - tof.ts < TOF_REFRESH_MS) return;
  tof.front = clampToF(sensors[0].readRangeContinuousMillimeters() / 10.0f);
  tof.back  = clampToF(sensors[1].readRangeContinuousMillimeters() / 10.0f);
  tof.ra    = clampToF(sensors[2].readRangeContinuousMillimeters() / 10.0f);
  tof.rp    = clampToF(sensors[3].readRangeContinuousMillimeters() / 10.0f);
  tof.la    = clampToF(sensors[4].readRangeContinuousMillimeters() / 10.0f);
  tof.lp    = clampToF(sensors[5].readRangeContinuousMillimeters() / 10.0f);
  tof.ts    = millis();
}

// ============================================================
//  MOTORI
// ============================================================

void setMotors(int l, int r) {
  digitalWrite(SX_IN1, l < 0); digitalWrite(SX_IN2, l >= 0);
  digitalWrite(DX_IN1, r < 0); digitalWrite(DX_IN2, r >= 0);
  analogWrite(PWM_SX, abs(l));
  analogWrite(PWM_DX, abs(r));
}

void stopMotors() { setMotors(0, 0); }

// ============================================================
//  LOGGING
// ============================================================

void lcdShow(String line1, String line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1); lcd.print(line2.substring(0, 16));
}

void dbg(String msg) { Serial.println("[D] " + msg); }

// ============================================================
//  ROTAZIONE OPEN-LOOP
// ============================================================

void rotate(float rel_deg) {
  if (rel_deg == 0) return;
  bool  destra   = (rel_deg < 0);
  float abs_deg  = abs(rel_deg);
  int   duration = (abs_deg > 170) ? ROT_180_MS : ROT_90_MS;

  String dirStr = destra ? ("-" + String((int)abs_deg) + "d DX")
                         : ("+" + String((int)abs_deg) + "d SX");
  lcdShow("ROTAZ", dirStr);
  dbg("Rotaz " + dirStr);

  if (destra) setMotors( ROT_SPEED,     -ROT_SPEED);
  else        setMotors(-ROT_SPEED,      ROT_SPEED);
  delay(duration * 4 / 5);

  if (destra) setMotors( ROT_SPEED / 2, -ROT_SPEED / 2);
  else        setMotors(-ROT_SPEED / 2,  ROT_SPEED / 2);
  delay(duration / 5);

  stopMotors();
  delay(ROT_SETTLE);
}

// ============================================================
//  SCAN SENSORI → <DATA:c,FRL>
// ============================================================

void scanSensors() {
  delay(40);
  float sumF = 0, sumR = 0, sumL = 0;
  for (int i = 0; i < 3; i++) {
    tof.ts = 0; updateToF();
    sumF += tof.front;
    sumR += (tof.ra + tof.rp) / 2.0f;
    sumL += (tof.la + tof.lp) / 2.0f;
    delay(8);
  }
  tof.ts = 0; updateToF();
  int c = getDetectedColor();

  int F = (sumF/3.0f <= SOGLIA_MURO_FRONT) ? 1 : 0;
  int R = (sumR/3.0f <= SOGLIA_MURO_LAT)   ? 1 : 0;
  int L = (sumL/3.0f <= SOGLIA_MURO_LAT)   ? 1 : 0;

  const char* colori[] = {"NORM","GREY","ROSS","BLU "};
  String muri = "F:"; muri+=F; muri+=" R:"; muri+=R; muri+=" L:"; muri+=L;
  lcdShow("SCAN " + muri, String(colori[c]));
  dbg("SCAN F:"+String(sumF/3.0f,1)+" R:"+String(sumR/3.0f,1)+" L:"+String(sumL/3.0f,1)+" C:"+String(c));

  Serial.println("<DATA:" + String(c) + "," + String(F) + String(R) + String(L) + ">");
}

// ============================================================
//  AVANZAMENTO
// ============================================================

void moveForward() {
  lcdShow("AVANZA...", "");
  dbg("Avanzamento iniziato");

  tof.ts = 0; updateToF();
  if (tof.front <= FRONT_STUCK_THR) {
    lcdShow("STUCK pre", "F:"+String(tof.front,1));
    setMotors(-BASE_SPEED, -BASE_SPEED); delay(BACKUP_MS); stopMotors();
    Serial.println("<MOVE:STUCK>");
    scanSensors(); return;
  }

  moveCounter  = 0;
  lastMoveTime = millis();
  float dF_start  = tof.front;
  float dB_start  = tof.back;
  bool  wallFront = (dF_start < SOGLIA_MURO_FRONT);
  bool  wallBack  = (dB_start < 15.0f);
  int   stopCount = 0;
  bool  stuck     = false;

  setMotors(BASE_SPEED, BASE_SPEED);
  unsigned long moveStart = millis();

  while (true) {
    if (millis() - lastMoveTime < 20) continue;
    lastMoveTime = millis();
    moveCounter++;
    updateToF();

    float ra=tof.ra, rp=tof.rp, la=tof.la, lp=tof.lp;
    float dF=tof.front, dB=tof.back;
    if (!wallFront && dF < SOGLIA_MURO_FRONT) wallFront = true;

    float drift = 0;
    bool hasMuroR = (ra < SOGLIA_MURO_LAT);
    bool hasMuroL = (la < SOGLIA_MURO_LAT);
    if (hasMuroR && hasMuroL) {
      drift = (ra - la) * 6.0f + ((ra-rp)-(la-lp)) * 4.0f;
    } else if (hasMuroR) {
      drift = (ra - TARGET_LAT_DIST) * 10.0f + (ra-rp) * 4.0f;
    } else if (hasMuroL) {
      drift = (TARGET_LAT_DIST - la) * 10.0f + (lp-la) * 4.0f;
    }

    int spd = (wallFront && dF < SOGLIA_LENTA) ? SLOW_SPEED : BASE_SPEED;
    if      (ra < DIST_PERICOLO_RUOTA || rp < DIST_PERICOLO_RUOTA) drift = -(spd-20);
    else if (la < DIST_PERICOLO_RUOTA || lp < DIST_PERICOLO_RUOTA) drift =  (spd-20);
    else    drift = constrain(drift, -(spd-20), (spd-20));
    setMotors(spd+(int)drift, spd-(int)drift);

    if (wallFront && dF <= TARGET_FRONT_DIST) {
      if (++stopCount >= STOP_CONFIRM) break;
    } else stopCount = 0;

    if (wallBack && dB >= (dB_start + 29.5f)) break;

    if (millis() - moveStart >= MOVE_TIMEOUT_MS) {
      dbg("TIMEOUT — STUCK"); stuck = true; break;
    }
  }

  stopMotors();
  if (stuck) {
    lcdShow("STUCK!", "Backup...");
    setMotors(-BASE_SPEED, -BASE_SPEED); delay(BACKUP_MS); stopMotors();
    Serial.println("<MOVE:STUCK>");
  } else {
    lcdShow("AVANZ.OK", "F:"+String(tof.front,1));
    Serial.println("<MOVE:OK>");
  }
  scanSensors();
}

// ============================================================
//  SERVO MEDIKIT
// ============================================================

void servoSlowMove(int target, int msPerDeg = SERVO_SLOW_MS) {
  target = constrain(target, 0, 180);
  while (servoAngle != target) {
    servoAngle += (target > servoAngle) ? 1 : -1;
    medikitServo.write(servoAngle);
    delay(msPerDeg);
  }
}

void dispenseKit() {
  medikitServo.attach(SERVO_PIN);
  delay(50);
  int nextAngle = servoAngle + SERVO_STEP;
  if (nextAngle > 170) {
    lcdShow("SERVO RST","0deg");
    servoSlowMove(0, SERVO_RESET_MS);
    nextAngle = SERVO_STEP;
  }
  lcdShow("DROP KIT", String(servoAngle)+"->"+String(nextAngle));
  servoSlowMove(nextAngle);
  delay(200);
  medikitServo.detach();
}

// ============================================================
//  LAMPEGGIO N VOLTE — Zona B checkpoint
// ============================================================

void blinkN(int n) {
  lcdShow("BLINK x" + String(n), "Bandiere target");
  dbg("Lampeggio x" + String(n));
  for (int i = 0; i < n; i++) {
    digitalWrite(LED_STATUS, HIGH); lcd.backlight();
    delay(BLINK_ON_MS);
    digitalWrite(LED_STATUS, LOW);  lcd.noBacklight();
    delay(BLINK_OFF_MS);
  }
  digitalWrite(LED_STATUS, HIGH); lcd.backlight();
  lcdShow("BLINK OK", String(n) + " lampeggi");
  Serial.println("<BLINK:DONE>");
}

// ============================================================
//  GESTIONE COMANDI SERIALI
// ============================================================

void handleCommand(String cmd) {
  dbg("CMD: " + cmd);

  if (cmd.startsWith("GO:")) {
    int d = cmd.substring(3).toInt();
    const char* dirs[] = {"AVANTI","DESTRA","DIETRO","SINISTRA"};
    lcdShow("GO:", String(dirs[d]));
    if      (d == 1) rotate( 90);
    else if (d == 2) rotate(180);
    else if (d == 3) rotate(-90);
    moveForward();
  }
  else if (cmd.startsWith("BLINK:")) {
    int n = cmd.substring(6).toInt();
    blinkN(n);
  }
  else if (cmd.startsWith("DROP:")) {
    int n = cmd.substring(5).toInt();
    lcdShow("DROP " + String(n) + " kit", "");
    dbg("Drop " + String(n) + " kit");
    for (int i = 0; i < n; i++) {
      lcdShow("DROP KIT", String(i+1)+"/"+String(n));
      dispenseKit();
      delay(300);
    }
    Serial.println("<DROP:DONE>");
  }
  else if (cmd == "SCAN:FRONT") {
    lcdShow("SCAN FRONT", "-90deg");
    rotate(-90);
    Serial.println("<SCAN:OK>");
  }
  else if (cmd == "SCAN:DONE") {
    lcdShow("SCAN DONE", "+90deg");
    rotate(90);
  }
  else if (cmd.startsWith("WAIT:")) {
    int secs = cmd.substring(5).toInt();
    lcdShow("ATTESA", String(secs) + "s");
    delay((long)secs * 1000);
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
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW);

  // Calibrazione colori specifica per tech challenge
  EEPROM.get(0, config);
  if (digitalRead(BTN_START) == LOW || config.magic != MAGIC_TC) {
    calibrateColors();
  }

  // Inizializzazione ToF
  for (int i = 0; i < 6; i++) { pinMode(xshutPins[i], OUTPUT); digitalWrite(xshutPins[i], LOW); }
  for (int i = 0; i < 6; i++) {
    digitalWrite(xshutPins[i], HIGH); delay(50);
    if (sensors[i].init()) {
      sensors[i].setAddress(0x30 + i);
      sensors[i].startContinuous();
    } else {
      lcdShow("ToF ERR:" + String(i), "");
      delay(2000);
    }
  }

  lcdShow("TECH CHALLENGE", "NO MPU");
  delay(1000);

  medikitServo.attach(SERVO_PIN);
  medikitServo.write(0); servoAngle = 0;
  delay(600);
  medikitServo.detach();
  lcdShow("SERVO OK", "0deg pronto");
  delay(500);

  tof.ts = 0;
  lcdShow("mENIAC 2026", "TECH READY");
}

// ============================================================
//  LOOP PRINCIPALE
// ============================================================

void loop() {
  updateToF();

  if (digitalRead(BTN_START) == LOW) {
    delay(200);
    isRunning = !isRunning;
    if (isRunning) {
      lcdShow("TC INIZIATA!", "");
      dbg("=== TECH CHALLENGE INIZIATA ===");
      Serial.println("<INIT>");
      delay(100);
      scanSensors();
    } else {
      stopMotors();
      lcdShow("STOPPED", "");
      dbg("=== FERMATO ===");
    }
    while (digitalRead(BTN_START) == LOW);
  }

  if (isRunning && Serial.available()) {
    String cmd = Serial.readStringUntil('>');
    cmd.trim();
    if (cmd.startsWith("<")) {
      handleCommand(cmd.substring(1));
    }
  }
}
