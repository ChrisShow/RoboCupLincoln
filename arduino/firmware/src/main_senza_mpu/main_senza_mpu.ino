/*
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * Descrizione: Firmware Arduino Mega 2560 — BACKUP SENZA MPU6050
 *
 *   Identico a main.ino tranne:
 *   - Rotazione: open-loop velocità × tempo (ROT_90_MS / ROT_180_MS)
 *   - Rampa: rilevata per contatore cicli (no pitch sensor)
 *   - RobotConfig: senza campi PID (solo colori EEPROM)
 *
 *   Tuning necessario: ROT_90_MS, ROT_180_MS empiricamente
 *   su superficie di gara con batteria carica.
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
  long magic;
};

// Cache letture ToF: aggiornata ogni TOF_REFRESH_MS millisecondi
struct ToFCache {
  float front, back, ra, rp, la, lp;
  unsigned long ts;
};

// --- COSTANTI GENERALI ---
const long  MAGIC_VAL      = 0xABCD1234;
const int   TOF_REFRESH_MS = 15;

// ---------------------------------------------------------------
// Soglie geometricamente derivate — robot 22cm, cella 30x30cm
// ---------------------------------------------------------------
const float SOGLIA_MURO_FRONT   = 10.0;
const float SOGLIA_MURO_LAT     = 15.0;
const float TARGET_LAT_DIST     = 10.0;
const float TARGET_FRONT_DIST   =  4.0;
const float DIST_PERICOLO_RUOTA =  4.5;

// ---------------------------------------------------------------
// Rotazione open-loop — TUNING EMPIRICO richiesto
//   Misurare su superficie di gara con batteria carica.
//   ROT_SPEED   : PWM applicato durante la rotazione
//   ROT_90_MS   : ms per 90°  (tipico: 400–550ms)
//   ROT_180_MS  : ms per 180° (tipico: ~2 × ROT_90_MS)
//   ROT_SETTLE  : pausa post-rotazione per stabilizzarsi
// ---------------------------------------------------------------
const int ROT_SPEED  = 203;
const int ROT_90_MS  = 500;
const int ROT_180_MS = 1000;
const int ROT_SETTLE = 100;

// Velocità di crociera costante
const int BASE_SPEED = 60;

// ---------------------------------------------------------------
// Parametri avanzamento
//   SOGLIA_LENTA     : distanza frontale sotto cui si rallenta
//   SLOW_SPEED       : PWM in zona rallentamento (±20 dal target)
//   MOVE_TIMEOUT_MS  : timeout assoluto avanzamento (evita blocchi)
//   BLACK_IGNORE_MS  : ms iniziali in cui ignora cella nera
//                      (il sensore colore è ancora sulla cella precedente)
//   BLACK_CONFIRM    : letture consecutive nere prima di confermare
//   STOP_CONFIRM     : tick consecutivi con dF≤TARGET prima di fermarsi
// ---------------------------------------------------------------
const float        SOGLIA_LENTA      = 12.0;
const int          SLOW_SPEED        =  55;
const unsigned long MOVE_TIMEOUT_MS  = 3000UL;
const unsigned long BLACK_IGNORE_MS  =  250UL;
const int          BLACK_CONFIRM     =    2;
const int          STOP_CONFIRM      =    2;
// Backup di sicurezza quando robot è bloccato (angolo/timeout)
const int          BACKUP_MS         =  900;   // ~10cm a BASE_SPEED=90
// Soglia "già a muro" all'avvio moveForward — non avanzare
const float        FRONT_STUCK_THR   =  8.0f;  // cm (TARGET_FRONT_DIST + 1.5cm margine)
// Valore sostitutivo per letture ToF fuori scala (0 o >200cm = errore sensore)
const float        TOF_OPEN_SUBST    = 40.0f;  // cm — trattato come spazio aperto

// ---------------------------------------------------------------
// Meccanismo medikit: ruota a 9 pale, 360°/9 = 40° per pala.
//   SERVO_STEP       : gradi per ogni medikit (fisso)
//   SERVO_SLOW_MS    : ms per grado in modalità erogazione lenta
//   SERVO_RESET_MS   : ms per grado in modalità ripristino a 0
//                      (direzione contraria, pale non rilasciano)
// ---------------------------------------------------------------
const int SERVO_STEP     = 40;
const int SERVO_SLOW_MS  = 12;   // → 40° = 480ms per kit
const int SERVO_RESET_MS =  4;   // → reset 160°→0 ≈ 960ms

// --- GLOBALI ---
RobotConfig config;
ToFCache    tof;

VL53L0X sensors[6];
int xshutPins[] = {
  XSHUT_FRONT, XSHUT_BACK,
  XSHUT_DX_ANT, XSHUT_DX_POST,
  XSHUT_SX_ANT, XSHUT_SX_POST
};

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo             medikitServo;

int           servoAngle  = 0;
bool          isRunning   = false;
int           moveCounter = 0;
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

  // Grigio e rosso disabilitati — calibrare solo 3 colori attivi
  const char* names[]   = {"BIANCO/NORM", "BLU/DELAY", "NERO/TRAP"};
  ColorRGB*   targets[] = {&config.normal, &config.blue, &config.black};

  for (int i = 0; i < 3; i++) {
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

  config.magic = MAGIC_VAL;
  EEPROM.put(0, config);
  lcd.clear(); lcd.print("SALVATO!");
  delay(1000);
}

int getDetectedColor() {
  int r, g, b;
  readRawColor(r, g, b);
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
  // Grigio e rosso disabilitati (sensore inaffidabile) — trattati come normale
  if (winner == 1 || winner == 2) winner = 0;
  return winner;
}

// ============================================================
//  CACHE ToF
// ============================================================

// Clamp lettura ToF: valori ≤0 (timeout/errore) o >200cm (fuori scala VL53L0X)
// vengono sostituiti con TOF_OPEN_SUBST (40cm = spazio aperto).
// CRITICO: senza clamp, un valore 0 triggera l'emergenza ruota (DIST_PERICOLO_RUOTA=4.5cm)
// e fa impazzire il robot in spazi aperti.
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
//  LOGGING — LCD + Seriale debug
// ============================================================

void lcdShow(String line1, String line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1); lcd.print(line2.substring(0, 16));
}

void dbg(String msg) {
  Serial.println("[D] " + msg);
}

// ============================================================
//  ROTAZIONE OPEN-LOOP (velocità × tempo)
//  TUNING: misurare ROT_90_MS su superficie di gara con
//  batteria carica. Ruota con entrambe le fiancate opposte.
// ============================================================

void rotate(float rel_deg) {
  if (rel_deg == 0) return;

  bool  destra   = (rel_deg < 0);
  float abs_deg  = abs(rel_deg);
  int   duration = (abs_deg > 170) ? ROT_180_MS : ROT_90_MS;

  String dirStr = destra ? ("-" + String((int)abs_deg) + "deg DX")
                         : ("+" + String((int)abs_deg) + "deg SX");
  lcdShow("ROTAZIONE", dirStr);
  dbg("Rotazione " + dirStr + " durata=" + String(duration) + "ms");

  // Fase 1 (80%): velocità piena
  if (destra) setMotors( ROT_SPEED,     -ROT_SPEED);
  else        setMotors(-ROT_SPEED,      ROT_SPEED);
  delay(duration * 4 / 5);

  // Fase 2 (20%): velocità dimezzata — smaltisce l'inerzia di uscita
  if (destra) setMotors( ROT_SPEED / 2, -ROT_SPEED / 2);
  else        setMotors(-ROT_SPEED / 2,  ROT_SPEED / 2);
  delay(duration / 5);

  stopMotors();
  delay(ROT_SETTLE);
  dbg("Rotazione OK");
}

// ============================================================
//  SCAN SENSORI → invia <DATA:> al Raspberry
// ============================================================

void scanSensors() {
  // Pausa stabilizzazione post-stop, poi media su 3 letture per robustezza
  delay(40);
  float sumF = 0, sumR = 0, sumL = 0;
  for (int i = 0; i < 3; i++) {
    tof.ts = 0; updateToF();
    sumF += tof.front;
    sumR += (tof.ra + tof.rp) / 2.0f;
    sumL += (tof.la + tof.lp) / 2.0f;
    delay(8);
  }
  float avgF = sumF / 3.0f;
  float avgR = sumR / 3.0f;
  float avgL = sumL / 3.0f;

  tof.ts = 0; updateToF();   // cache fresca
  int c = getDetectedColor();

  int F = (avgF <= SOGLIA_MURO_FRONT) ? 1 : 0;
  int R = (avgR <= SOGLIA_MURO_LAT)   ? 1 : 0;
  int L = (avgL <= SOGLIA_MURO_LAT)   ? 1 : 0;

  const char* colori[] = {"NORM","----","----","BLUE","BLCK"};
  String muri = "F:"; muri += F; muri += " R:"; muri += R; muri += " L:"; muri += L;
  lcdShow("SCAN " + muri, "Col:" + String(colori[c]));
  dbg("SCAN F:" + String(avgF,1) + "cm R:" + String(avgR,1)
      + "cm L:" + String(avgL,1) + "cm Col:" + String(c));

  Serial.println("<DATA:" + String(c) + "," + String(F) + String(R) + String(L) + ">");
}

// ============================================================
//  AVANZAMENTO con anti-drift angolare
// ============================================================

void moveForward() {
  lcdShow("AVANZAMENTO...", "");
  dbg("Avanzamento iniziato");

  // ----------------------------------------------------------------
  //  PRE-CHECK: se siamo già contro un muro (angolo, rotazione errata)
  //  non avanzare — fai backup immediato e segnala STUCK al Pi.
  // ----------------------------------------------------------------
  tof.ts = 0; updateToF();
  if (tof.front <= FRONT_STUCK_THR) {
    lcdShow("STUCK pre-move", "F:" + String(tof.front,1) + "cm");
    dbg("STUCK pre-move: front=" + String(tof.front,1) + "cm <= " + String(FRONT_STUCK_THR));
    delay(BACKUP_MS);
    stopMotors();
    Serial.println("<MOVE:STUCK>");
    scanSensors();
    return;
  }

  moveCounter  = 0;
  lastMoveTime = millis();

  float dF_start   = tof.front;
  float dB_start   = tof.back;
  bool  wallFront  = (dF_start < SOGLIA_MURO_FRONT);
  bool  wallBack   = (dB_start < 15.0f);
  int   blackCount = 0;   // conferme consecutive cella nera
  int   stopCount  = 0;   // conferme consecutive fine-cella
  bool  stuck      = false;

  // Motori partono subito; drift verrà applicato ogni tick
  setMotors(BASE_SPEED, BASE_SPEED);
  unsigned long moveStart = millis();

  while (true) {

    // ================================================================
    //  RILEVAMENTO CELLA NERA — continuo, non vincolato al tick 20ms
    //
    //  Finestra iniziale BLACK_IGNORE_MS: il sensore colore è 12.5cm
    //  avanti e all'avvio si trova ancora sulla cella corrente.
    //  Richiediamo BLACK_CONFIRM letture consecutive per evitare
    //  falsi positivi da riflessioni o bordi di transizione.
    // ================================================================
    if (millis() - moveStart > BLACK_IGNORE_MS) {
      if (getDetectedColor() == 4) {
        blackCount++;
        if (blackCount >= BLACK_CONFIRM) {
          stopMotors();
          unsigned long elapsed = millis() - moveStart;
          lcdShow("CELLA NERA!", "Ritorno...");
          dbg("Cella NERA (x" + String(blackCount)
              + ") retromarcia " + String(elapsed) + "ms");
          setMotors(-BASE_SPEED, -BASE_SPEED);
          delay(elapsed);
          stopMotors();
          lcdShow("CELLA NERA", "Rientrato OK");
          dbg("Rientro completato");
          Serial.println("<MOVE:BLACK>");
          scanSensors();
          return;
        }
      } else {
        blackCount = 0;   // reset se la lettura non conferma
      }
    }

    // ================================================================
    //  TICK DI CONTROLLO 20ms — anti-drift + fine cella
    // ================================================================
    if (millis() - lastMoveTime < 20) continue;
    lastMoveTime = millis();
    moveCounter++;

    updateToF();
    float ra = tof.ra, rp = tof.rp;
    float la = tof.la, lp = tof.lp;
    float dF = tof.front;
    float dB = tof.back;

    // Aggiorna rilevazione dinamica muro frontale
    if (!wallFront && dF < SOGLIA_MURO_FRONT) wallFront = true;

    // ---- Anti-drift con clamping dinamico ----
    float drift = 0;
    bool hasMuroR = (ra < SOGLIA_MURO_LAT);
    bool hasMuroL = (la < SOGLIA_MURO_LAT);

    if (hasMuroR && hasMuroL) {
      float center_err = ra - la;
      float angle_err  = (ra - rp) - (la - lp);
      drift = center_err * 6.0f + angle_err * 4.0f;
    } else if (hasMuroR) {
      float angle_err = ra - rp;
      drift = (ra - TARGET_LAT_DIST) * 10.0f + angle_err * 4.0f;
    } else if (hasMuroL) {
      float angle_err = lp - la;
      drift = (TARGET_LAT_DIST - la) * 10.0f + angle_err * 4.0f;
    }

    // Velocità corrente: riduce nell'area di avvicinamento al muro
    int spd = (wallFront && dF < SOGLIA_LENTA) ? SLOW_SPEED : BASE_SPEED;

    // Emergenze contatto ruota: override immediato; altrimenti clamp drift
    if      (ra < DIST_PERICOLO_RUOTA || rp < DIST_PERICOLO_RUOTA) drift = -(spd - 20);
    else if (la < DIST_PERICOLO_RUOTA || lp < DIST_PERICOLO_RUOTA) drift =  (spd - 20);
    else    drift = constrain(drift, -(spd - 20), (spd - 20));

    setMotors(spd + (int)drift, spd - (int)drift);

    // ---- Fine cella: STOP_CONFIRM tick consecutivi con dF ≤ TARGET ----
    if (wallFront && dF <= TARGET_FRONT_DIST) {
      if (++stopCount >= STOP_CONFIRM) break;
    } else {
      stopCount = 0;
    }

    // ---- Fine cella: muro posteriore percorso ~30cm ----
    if (wallBack && dB >= (dB_start + 29.5f)) break;

    // ---- Timeout assoluto → robot bloccato ----
    if (millis() - moveStart >= MOVE_TIMEOUT_MS) {
      dbg("TIMEOUT avanzamento — robot STUCK");
      stuck = true;
      break;
    }
  }

  stopMotors();

  if (stuck) {
    // ----------------------------------------------------------------
    //  STUCK: robot non ha raggiunto la cella target.
    //  Backup di BACKUP_MS ms per liberarsi dall'angolo,
    //  poi segnala al Pi che la direzione è impraticabile.
    // ----------------------------------------------------------------
    lcdShow("STUCK!", "Backup " + String(BACKUP_MS) + "ms");
    dbg("STUCK: backup " + String(BACKUP_MS) + "ms poi MOVE:STUCK");
    setMotors(-BASE_SPEED, +BASE_SPEED*2);
    delay(BACKUP_MS);
    stopMotors();
    Serial.println("<MOVE:STUCK>");
  } else {
    dbg("Avanzamento OK  F:" + String(tof.front,1)
        + "cm cnt=" + String(moveCounter)
        + " t=" + String(millis() - moveStart) + "ms");
    lcdShow("AVANZ. OK", "F:" + String(tof.front,1) + "cm");
    Serial.println("<MOVE:OK>");
  }
  scanSensors();
}

// ============================================================
//  SEGNALAZIONE VITTIMA — lampeggio LED + retroilluminazione LCD
// ============================================================

void blinkVictim(int times = 5, int onMs = 250, int offMs = 200) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_STATUS, HIGH);
    lcd.backlight();
    delay(onMs);
    digitalWrite(LED_STATUS, LOW);
    lcd.noBacklight();
    delay(offMs);
  }
  digitalWrite(LED_STATUS, HIGH);
  lcd.backlight();
}

// ============================================================
//  MOVIMENTO LENTO SERVO
//  Avanza di 1° alla volta con msPerDeg di pausa tra ogni step.
//  Direzione automatica: sale se target > servoAngle, scende altrimenti.
// ============================================================

void servoSlowMove(int target, int msPerDeg = SERVO_SLOW_MS) {
  target = constrain(target, 0, 180);
  while (servoAngle != target) {
    servoAngle += (target > servoAngle) ? 1 : -1;
    medikitServo.write(servoAngle);
    delay(msPerDeg);
  }
}

// ============================================================
//  EROGAZIONE SINGOLO MEDIKIT
//  Avanza sempre nella stessa direzione (angolo crescente).
//  Quando supererebbe 170°, ripristina lentamente a 0
//  (le pale della ruota non rilasciano kit in direzione contraria)
//  poi riprende il passo successivo.
// ============================================================

void dispenseKit() {
  medikitServo.attach(SERVO_PIN);
  delay(50);  // stabilizzazione breve post-attach

  int nextAngle = servoAngle + SERVO_STEP;
  if (nextAngle > 170) {
    lcdShow("SERVO RESET", "0deg...");
    dbg("Servo reset: " + String(servoAngle) + "deg -> 0deg");
    servoSlowMove(0, SERVO_RESET_MS);
    nextAngle = SERVO_STEP;
  }
  lcdShow("EROGAZ. KIT", String(servoAngle) + ">" + String(nextAngle) + "deg");
  dbg("Servo: " + String(servoAngle) + "deg -> " + String(nextAngle) + "deg");
  servoSlowMove(nextAngle);

  delay(200);              // mantieni posizione raggiunta
  medikitServo.detach();   // stop PWM — no jitter tra un'erogazione e l'altra
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

  // tipo=1 → OMEGA (0 kit), tipo=2 → PHI (1 kit), tipo=3 → PSI (2 kit)
  const char* nomi[] = {"", "OMEGA",  "PHI",  "PSI"};
  const int   kits[] = {  0,      0,      1,      2};
  int numKit = kits[tipo];

  lcdShow("VITTIMA " + String(nomi[tipo]), String(numKit) + " kit  pos:" + String(pos));
  dbg("Vittima " + String(nomi[tipo]) + " pos=" + String(pos)
      + " kit=" + String(numKit));

  blinkVictim();

  if (numKit > 0) {
    if      (pos == 'R') rotate(-90);
    else if (pos == 'L') rotate( 90);
    else if (pos == 'F') rotate(180);

    for (int i = 0; i < numKit; i++) {
      lcdShow("EROGAZ. KIT", String(i+1) + "/" + String(numKit));
      dbg("Erogo kit " + String(i+1) + "/" + String(numKit)
          + " → servo " + String(servoAngle) + "° → "
          + String(servoAngle + SERVO_STEP) + "°");
      dispenseKit();
    }

    lcdShow("RIENTRO ORI.", "");
    if      (pos == 'R') rotate( 90);
    else if (pos == 'L') rotate(-90);
    else if (pos == 'F') rotate(180);
  }

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
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW); // TCS3200 scaling 20%

  EEPROM.get(0, config);
  if (digitalRead(BTN_START) == LOW || config.magic != MAGIC_VAL) {
    calibrateColors();
  }

  // Inizializzazione ToF (uno alla volta via XSHUT)
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

  // Nessuna calibrazione MPU — avvio immediato
  lcdShow("NO MPU MODE", "BACKUP FW");
  delay(1000);

  medikitServo.attach(SERVO_PIN);
  medikitServo.write(0);   // posiziona direttamente a 0° — nessuno sweep
  servoAngle = 0;
  delay(600);              // lascia raggiungere fisicamente 0°
  medikitServo.detach();   // stop PWM: nessun jitter, nessuna rotazione continua
  lcdShow("SERVO INIT", "pronto a 0deg");

  tof.ts = 0;

  lcd.clear(); lcd.print("mENIAC 2026");
  lcd.setCursor(0, 1); lcd.print("READY TO START");
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
      lcdShow("mENIAC 2026", "GARA INIZIATA!");
      dbg("=== GARA INIZIATA (NO MPU) ===");
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

  if (isRunning && Serial.available()) {
    String cmd = Serial.readStringUntil('>');
    cmd.trim();
    if (cmd.startsWith("<")) {
      handleCommand(cmd.substring(1));
    }
  }
}
