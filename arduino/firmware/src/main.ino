/* 
 * Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
 * Descrizione: Codice Arduino Mega 2560 con Calibrazione EEPROM
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <MPU6050_light.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <EEPROM.h>

// --- PIN DEFINITIONS ---
#define XSHUT_FRONT 30
#define XSHUT_BACK  31
#define XSHUT_DX_ANT 32
#define XSHUT_DX_POST 33
#define XSHUT_SX_ANT 34
#define XSHUT_SX_POST 35

#define PWM_DX 2
#define DX_IN1 22
#define DX_IN2 23
#define PWM_SX 3
#define SX_IN1 24
#define SX_IN2 25

#define TCS_S0 38
#define TCS_S1 39
#define TCS_S2 40
#define TCS_S3 41
#define TCS_OUT 42

#define SERVO_PIN 4
#define BTN_START 43
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
  float kp_rot, ki_rot, kd_rot; // Valori PID tarabili
  long magic; // Identificativo per memoria valida
};

// --- COSTANTI E GLOBALI ---
const long MAGIC_VAL = 0xABCD1234;
RobotConfig config;

VL53L0X sensors[6];
int xshutPins[] = {XSHUT_FRONT, XSHUT_BACK, XSHUT_DX_ANT, XSHUT_DX_POST, XSHUT_SX_ANT, XSHUT_SX_POST};
MPU6050 mpu(Wire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo medikitServo;

bool isRunning = false;
int moveCounter = 0;
unsigned long lastMoveTime = 0;

// --- SOGLIE ALGORITMO (Ricalibrate per 22cm) ---
const float SOGLIA_MURO_FRONT = 25.0;  // Sopra i 25cm la cella davanti è considerata libera
const float SOGLIA_MURO_LAT = 18.0;    // Sopra i 18cm il muro laterale è considerato assente
const float TARGET_LAT_DIST = 10.0;    // Distanza ideale ToF-Muro per essere centrati (10cm)
const float TARGET_FRONT_DIST = 6.0;   // Stop a 6cm dal ToF (= 2cm dal sensore colore)
const float DIST_PERICOLO_RUOTA = 4.5; // Se il ToF legge meno di 4.5cm, la ruota sta per toccare!

// --- FUNZIONI SENSORE COLORE ---
void readRawColor(int &r, int &g, int &b) {
  digitalWrite(TCS_S2, LOW); digitalWrite(TCS_S3, LOW); r = pulseIn(TCS_OUT, LOW);
  digitalWrite(TCS_S2, HIGH); digitalWrite(TCS_S3, HIGH); g = pulseIn(TCS_OUT, LOW);
  digitalWrite(TCS_S2, LOW); digitalWrite(TCS_S3, HIGH); b = pulseIn(TCS_OUT, LOW);
}

void calibrateColors() {
  lcd.clear(); lcd.print("MODE: CALIBRA");
  delay(2000);
  
  const char* names[] = {"BIANCO/NORM", "GRIGIO/CHK", "ROSSO/DANGER", "BLU/DELAY", "NERO/TRAP"};
  ColorRGB* targets[] = {&config.normal, &config.grey, &config.red, &config.blue, &config.black};
  
  for(int i=0; i<5; i++) {
    lcd.clear(); lcd.print("POSIZIONA SU:");
    lcd.setCursor(0,1); lcd.print(names[i]);
    
    while(digitalRead(BTN_START) == HIGH); // Aspetta pressione
    delay(500);
    
    int r, g, b;
    readRawColor(r, g, b);
    targets[i]->r = r; targets[i]->g = g; targets[i]->b = b;
    
    lcd.clear(); lcd.print("OK! RILASCIA");
    while(digitalRead(BTN_START) == LOW); // Aspetta rilascio
    delay(500);
  }
  
  config.magic = MAGIC_VAL;
  // Impostiamo anche dei PID di default se è la prima volta
  config.kp_rot = 4.5; config.ki_rot = 0.1; config.kd_rot = 1.2;
  
  EEPROM.put(0, config);
  lcd.clear(); lcd.print("SALVATO!");
  delay(1000);
}

int getDetectedColor() {
  int r, g, b;
  readRawColor(r, g, b);
  // Castiamo a (long) per evitare l'overflow a 16-bit del Mega
  long d_norm = sq((long)r - config.normal.r) + sq((long)g - config.normal.g) + sq((long)b - config.normal.b);
  long d_grey = sq((long)r - config.grey.r)   + sq((long)g - config.grey.g)   + sq((long)b - config.grey.b);
  long d_red  = sq((long)r - config.red.r)    + sq((long)g - config.red.g)    + sq((long)b - config.red.b);
  long d_blue = sq((long)r - config.blue.r)   + sq((long)g - config.blue.g)   + sq((long)b - config.blue.b);
  long d_black= sq((long)r - config.black.r)  + sq((long)g - config.black.g)  + sq((long)b - config.black.b);

  long minDist = d_norm; int winner = 0;
  if(d_grey < minDist) { minDist = d_grey; winner = 1; }
  if(d_red < minDist)  { minDist = d_red;  winner = 2; }
  if(d_blue < minDist) { minDist = d_blue; winner = 3; }
  if(d_black < minDist){ minDist = d_black; winner = 4; }
  
  return winner; 
}

// --- SETUP E LOOP ---
void setup() {
  Serial.begin(115200);
  Wire.begin();
  lcd.init(); lcd.backlight();
  
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT);
  pinMode(DX_IN1, OUTPUT); pinMode(DX_IN2, OUTPUT);
  pinMode(SX_IN1, OUTPUT); pinMode(SX_IN2, OUTPUT);
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW); // 20% scaling

  EEPROM.get(0, config);
  
  // Se tieni premuto il tasto all'avvio o la memoria è vuota -> CALIBRAZIONE
  if(digitalRead(BTN_START) == LOW || config.magic != MAGIC_VAL) {
    calibrateColors();
  }

  // Inizializzazione ToF
  for(int i=0; i<6; i++) { pinMode(xshutPins[i], OUTPUT); digitalWrite(xshutPins[i], LOW); }
  for(int i=0; i<6; i++) {
    digitalWrite(xshutPins[i], HIGH); delay(50);
    if(sensors[i].init()) { sensors[i].setAddress(0x30 + i); sensors[i].startContinuous(); }
  }
  
  mpu.begin();
  lcd.clear(); lcd.print("CALIBRATING MPU...");
  mpu.calcOffsets();
  
  medikitServo.attach(SERVO_PIN);
  medikitServo.write(90);

  lcd.clear(); lcd.print("mENIAC 2026");
  lcd.setCursor(0,1); lcd.print("READY TO START");
}

void loop() {
  mpu.update();
  
  if (digitalRead(BTN_START) == LOW) {
    delay(200);
    isRunning = !isRunning;
    if(isRunning) {
      Serial.println("<INIT>");
      delay(100); 
      scanSensors(); 
    }
    else stopMotors();
    while(digitalRead(BTN_START) == LOW);
  }

  if (isRunning) {
    if (Serial.available()) {
      String cmd = Serial.readStringUntil('>');
      cmd.trim();
      if (cmd.startsWith("<")) handleCommand(cmd.substring(1));
    }
  }
}

// --- LOGICA MOVIMENTO E COMANDI ---
void handleCommand(String cmd) {
  if(cmd == "SCAN:FRONT") { rotate(-90); Serial.println("<SCAN:OK>"); }
  else if(cmd == "SCAN:DONE") rotate(90);
  
  // <-- AGGIUNGI QUESTA RIGA: Risponde subito a Python e bypassa i 5 secondi di delay!
  else if(cmd == "VIC:DONE") Serial.println("<VIC:DONE>"); 
  
  else if(cmd.startsWith("VIC:")) handleRescue(cmd.charAt(4), cmd.substring(6).toInt());
  else if(cmd.startsWith("GO:")) {
    int d = cmd.substring(3).toInt();
    if(d==1) rotate(90); else if(d==2) rotate(180); else if(d==3) rotate(-90);
    moveForward();
  }
}

void rotate(float rel_deg) {
  float target = mpu.getAngleZ() + rel_deg;
  float error, lastError = 0, integral = 0;
  // Usiamo i valori caricati dalla configurazione
  while(abs(error = target - mpu.getAngleZ()) > 1.5 && (millis() - start_time < 4000)) {
    mpu.update();
    float derivative = error - lastError;
    integral += error;
    float output = (error * config.kp_rot) + (integral * config.ki_rot) + (derivative * config.kd_rot);
    setMotors(constrain(-output, -150, 150), constrain(output, -150, 150));
    lastError = error; delay(10);
  }
  stopMotors();
}

void moveForward() {
  moveCounter = 0; lastMoveTime = millis();
  float baseSpeed = 130;
  bool rampMode = false;
  
  float dF_start = sensors[0].readRangeContinuousMillimeters()/10.0;
  float dB_start = sensors[1].readRangeContinuousMillimeters()/10.0;
  
  // Usiamo la nuova soglia di 25cm per capire se abbiamo un muro davanti
  bool wallFront = (dF_start < SOGLIA_MURO_FRONT); 
  bool wallBack = (dB_start < 15.0); // Leggermente alzata per sicurezza

  while(true) {
    mpu.update();
    float pitch = mpu.getAngleY();

    if (abs(pitch) > 15.0) {
      rampMode = true;
      int power = (pitch > 15.0) ? 230 : 80;
      setMotors(power, power);
    } else if (rampMode && abs(pitch) < 5.0) {
      stopMotors();
      Serial.println("<MOVE:RAMP>");
      scanSensors();
      return;
    } else {
      if (millis() - lastMoveTime >= 20) {
        lastMoveTime = millis(); moveCounter++;
        
        if(getDetectedColor() == 4) { // NERO
          setMotors(-150, -150); delay(moveCounter * 20);
          stopMotors(); Serial.println("<MOVE:BLACK>"); return;
        }

        // --- ANTI-DRIFT RICALIBRATO ---
        float dRA = sensors[2].readRangeContinuousMillimeters()/10.0;
        float dRP = sensors[3].readRangeContinuousMillimeters()/10.0;
        float dLA = sensors[4].readRangeContinuousMillimeters()/10.0;
        float dLP = sensors[5].readRangeContinuousMillimeters()/10.0;
        
        float drift = 0;
        // Caso 1: Corridoio stretto (muri da entrambi i lati) -> Resta al centro
        if (dRA < SOGLIA_MURO_LAT && dLA < SOGLIA_MURO_LAT) {
            drift = (dRA - dLA) * 8.0; 
        } 
        // Caso 2: Muro solo a destra -> Mantieni 10cm dal sensore (7cm dalle ruote)
        else if (dRA < SOGLIA_MURO_LAT) {
            drift = (dRA - TARGET_LAT_DIST) * 12.0;
        }
        // Caso 3: Muro solo a sinistra -> Mantieni 10cm dal sensore
        else if (dLA < SOGLIA_MURO_LAT) {
            drift = (TARGET_LAT_DIST - dLA) * 12.0;
        }

        // EMERGENZA RUOTE: Se i ToF leggono meno di 4.5cm, la ruota sta toccando!
        if (dRA < DIST_PERICOLO_RUOTA || dRP < DIST_PERICOLO_RUOTA) drift = -50; 
        if (dLA < DIST_PERICOLO_RUOTA || dLP < DIST_PERICOLO_RUOTA) drift = 50;

        setMotors(baseSpeed + drift, baseSpeed - drift);

        // --- CONDIZIONI DI FINE CELLA ---
        float dF = sensors[0].readRangeContinuousMillimeters()/10.0;
        float dB = sensors[1].readRangeContinuousMillimeters()/10.0;
        
        // 1. Muro frontale: fermati a 6cm (il sensore colore sarà a 2cm dal muro)
        if(wallFront && dF <= TARGET_FRONT_DIST) break;
        
        // 2. Muro posteriore: se ti sei allontanato di 28cm (cella da 30cm)
        if(wallBack && dB >= (dB_start + 28.0)) break; 
        
        // 3. Fail-safe: se non hai muri, fermati dopo circa 30cm di corsa
        if(!wallFront && !wallBack && moveCounter >= 85) break; 
      }
    }
  }
  stopMotors(); 
  Serial.println("<MOVE:OK>"); 
  scanSensors();
}

void scanSensors() {
  int c = getDetectedColor();
  int F = (sensors[0].readRangeContinuousMillimeters()/10.0 <= SOGLIA_MURO_FRONT);
  int R = ((sensors[2].readRangeContinuousMillimeters() + sensors[3].readRangeContinuousMillimeters())/20.0 <= SOGLIA_MURO_LAT);
  int L = ((sensors[4].readRangeContinuousMillimeters() + sensors[5].readRangeContinuousMillimeters())/20.0 <= SOGLIA_MURO_LAT);
  Serial.println("<DATA:" + String(c) + "," + String(F) + String(R) + String(L) + ">");
}

void handleRescue(char pos, int type) {
  digitalWrite(LED_STATUS, HIGH); delay(5000); digitalWrite(LED_STATUS, LOW);
  if(type > 1) {
    if(pos=='R') rotate(-90); else if(pos=='L') rotate(90); else if(pos=='F') rotate(180);
    for(int i=0; i<type; i++) { medikitServo.write(0); delay(1000); medikitServo.write(90); delay(500); }
    if(pos=='R') rotate(90); else if(pos=='L') rotate(-90); else if(pos=='F') rotate(180);
  }
  Serial.println("<VIC:DONE>");
}

void setMotors(int l, int r) {
  // Logica Invertita via Software: l>=0 (Avanti) attiva LOW su IN1 e HIGH su IN2
  digitalWrite(SX_IN1, l<0);  digitalWrite(SX_IN2, l>=0);
  digitalWrite(DX_IN1, r<0);  digitalWrite(DX_IN2, r>=0);
  analogWrite(PWM_SX, abs(l)); analogWrite(PWM_DX, abs(r));
}

void stopMotors() { setMotors(0, 0); }
