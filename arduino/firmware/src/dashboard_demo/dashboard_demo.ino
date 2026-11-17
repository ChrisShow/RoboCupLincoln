/*
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * Descrizione: Firmware DIMOSTRATIVO per la dashboard web (raspberry/src/dashboard)
 *
 *   - Legge TUTTI i sensori e li invia al Raspberry come righe <TEL:...> (10 Hz)
 *   - I MOTORI RESTANO SEMPRE FERMI: i pin PWM sono a 0 e non esiste
 *     nessuna funzione che li accenda. Il servo non viene mai collegato.
 *   - Non modifica la calibrazione colori salvata in EEPROM (la legge soltanto).
 *
 *   Per tornare in gara basta ricaricare il firmware di gara (es. fine_gara.ino).
 *
 * Formato riga (un pacchetto ogni TEL_PERIOD_MS):
 * <TEL:ms,F,B,DXant,DXpost,SXant,SXpost,R,G,B,colore,pwmSX,pwmDX,servo,gara,pulsante,led,yaw,pitch>
 *   ToF in mm grezzi (65535 = timeout, >= 8190 = fuori portata, 0 = sensore non inizializzato)
 *   R,G,B = durata impulso TCS3200 in µs (più basso = più luce)
 *   colore = 0 normale, 3 blu, 4 nero (-1 se la calibrazione EEPROM non è valida)
 *   yaw/pitch = "-" se l'MPU6050 non risponde
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <MPU6050_light.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// --- PIN (identici a fine_gara.ino) ---
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

#define BTN_START  43
#define LED_STATUS 45

// --- STRUTTURE (stesso layout EEPROM di fine_gara.ino, solo lettura) ---
struct ColorRGB { int r, g, b; };
struct RobotConfig {
  ColorRGB normal, grey, red, blue, black;
  long magic;
};
const long MAGIC_VAL = 0xABCD1234;

const unsigned long TEL_PERIOD_MS = 100;

VL53L0X           sensors[6];
bool              tofOk[6] = {false, false, false, false, false, false};
int               xshutPins[] = {XSHUT_FRONT, XSHUT_BACK, XSHUT_DX_ANT, XSHUT_DX_POST, XSHUT_SX_ANT, XSHUT_SX_POST};
MPU6050           mpu(Wire);
bool              mpuOk = false;
LiquidCrystal_I2C lcd(0x27, 16, 2);
RobotConfig       config;
bool              configOk = false;
unsigned long     telLast = 0;

// ============================================================
//  MOTORI: SEMPRE FERMI
// ============================================================

void motorsOff() {
  analogWrite(PWM_SX, 0);
  analogWrite(PWM_DX, 0);
  digitalWrite(SX_IN1, LOW); digitalWrite(SX_IN2, LOW);
  digitalWrite(DX_IN1, LOW); digitalWrite(DX_IN2, LOW);
}

// ============================================================
//  SENSORE COLORE
// ============================================================

void readRawColor(int &r, int &g, int &b) {
  digitalWrite(TCS_S2, LOW);  digitalWrite(TCS_S3, LOW);  r = pulseIn(TCS_OUT, LOW, 20000);
  digitalWrite(TCS_S2, HIGH); digitalWrite(TCS_S3, HIGH); g = pulseIn(TCS_OUT, LOW, 20000);
  digitalWrite(TCS_S2, LOW);  digitalWrite(TCS_S3, HIGH); b = pulseIn(TCS_OUT, LOW, 20000);
}

long dist2(int r, int g, int b, const ColorRGB &c) {
  return sq((long)r - c.r) + sq((long)g - c.g) + sq((long)b - c.b);
}

// Stessa classificazione di fine_gara.ino (grigio e rosso trattati come normale)
int classifyColor(int r, int g, int b) {
  if (!configOk) return -1;
  long d[5] = {dist2(r, g, b, config.normal), dist2(r, g, b, config.grey), dist2(r, g, b, config.red),
               dist2(r, g, b, config.blue),   dist2(r, g, b, config.black)};
  int winner = 0;
  for (int i = 1; i < 5; i++) if (d[i] < d[winner]) winner = i;
  if (winner == 1 || winner == 2) winner = 0;
  return winner;
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  // Motori bloccati PRIMA di qualsiasi altra cosa
  pinMode(PWM_SX, OUTPUT); pinMode(PWM_DX, OUTPUT);
  pinMode(SX_IN1, OUTPUT); pinMode(SX_IN2, OUTPUT);
  pinMode(DX_IN1, OUTPUT); pinMode(DX_IN2, OUTPUT);
  motorsOff();

  Serial.begin(115200);
  Wire.begin();
  Wire.setWireTimeout(3000, true);   // un sensore guasto non blocca il bus I2C

  lcd.init(); lcd.backlight();
  lcd.clear(); lcd.print("DEMO DASHBOARD"); lcd.setCursor(0, 1); lcd.print("motori OFF");

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT);
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW);   // scaling 20%

  EEPROM.get(0, config);
  configOk = (config.magic == MAGIC_VAL);

  // ToF uno alla volta via XSHUT (indirizzi 0x30..0x35 come in gara)
  for (int i = 0; i < 6; i++) { pinMode(xshutPins[i], OUTPUT); digitalWrite(xshutPins[i], LOW); }
  delay(50);
  for (int i = 0; i < 6; i++) {
    digitalWrite(xshutPins[i], HIGH); delay(50);
    sensors[i].setTimeout(60);
    if (sensors[i].init()) {
      sensors[i].setAddress(0x30 + i);
      sensors[i].startContinuous();
      tofOk[i] = true;
    }
  }

  // MPU6050 opzionale: se non risponde si invia "-"
  if (mpu.begin() == 0) {
    lcd.setCursor(0, 1); lcd.print("calibro MPU...  ");
    mpu.calcOffsets();
    mpuOk = true;
  }

  int nTof = 0; for (int i = 0; i < 6; i++) nTof += tofOk[i];
  lcd.clear(); lcd.print("DEMO  ToF:"); lcd.print(nTof); lcd.print("/6");
  lcd.setCursor(0, 1); lcd.print(mpuOk ? "MPU OK " : "MPU -- "); lcd.print(configOk ? "COL OK" : "COL --");
  digitalWrite(LED_STATUS, HIGH);
}

// ============================================================
//  LOOP: solo letture + telemetria
// ============================================================

void loop() {
  motorsOff();                 // ribadito ad ogni giro, per sicurezza
  if (mpuOk) mpu.update();

  if (millis() - telLast < TEL_PERIOD_MS) return;
  telLast = millis();

  int r, g, b;
  readRawColor(r, g, b);

  Serial.print(F("<TEL:"));
  Serial.print(millis());
  for (int i = 0; i < 6; i++) {
    Serial.print(',');
    Serial.print(tofOk[i] ? sensors[i].readRangeContinuousMillimeters() : 0);
  }
  Serial.print(','); Serial.print(r);
  Serial.print(','); Serial.print(g);
  Serial.print(','); Serial.print(b);
  Serial.print(','); Serial.print(classifyColor(r, g, b));
  Serial.print(F(",0,0,-"));                       // PWM SX, PWM DX sempre 0; servo non usato
  Serial.print(F(",0"));                           // gara: mai in esecuzione
  Serial.print(','); Serial.print(digitalRead(BTN_START) == LOW ? 1 : 0);
  Serial.print(','); Serial.print(digitalRead(LED_STATUS));
  if (mpuOk) {
    Serial.print(','); Serial.print(mpu.getAngleZ(), 1);
    Serial.print(','); Serial.print(mpu.getAngleY(), 1);
  } else {
    Serial.print(F(",-,-"));
  }
  Serial.println('>');
}
