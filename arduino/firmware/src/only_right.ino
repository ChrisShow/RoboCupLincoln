/* * Progetto: RoboCup Junior Rescue Maze - Logic Test "Right-Hand Rule"
 * Descrizione: Il robot segue costantemente il muro alla sua destra.
 */

#include <Wire.h>
#include <VL53L0X.h>
#include <MPU6050_light.h>

// --- PIN MOTORI (Presi dal vostro main.py) ---
#define PWM_DX 2
#define DX_IN1 22
#define DX_IN2 23
#define PWM_SX 3
#define SX_IN1 24
#define SX_IN2 25

// --- SOGLIE DISTANZA (cm) ---
const float SOGLIA_MURO_FRONT = 12.0;
const float SOGLIA_MURO_LAT = 15.0;

// --- SENSORI ---
VL53L0X sensors[6];
const uint8_t xshutPins[6] = {30, 31, 32, 33, 34, 35}; // Front, Back, DX_Ant, DX_Post, SX_Ant, SX_Post
MPU6050 mpu(Wire);

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Inizializzazione Motori
  pinMode(PWM_DX, OUTPUT); pinMode(DX_IN1, OUTPUT); pinMode(DX_IN2, OUTPUT);
  pinMode(PWM_SX, OUTPUT); pinMode(SX_IN1, OUTPUT); pinMode(SX_IN2, OUTPUT);

  // Inizializzazione Sensori ToF (Sequenziale come nel vostro codice)
  for(int i=0; i<6; i++) {
    pinMode(xshutPins[i], OUTPUT);
    digitalWrite(xshutPins[i], LOW);
  }
  for(int i=0; i<6; i++) {
    digitalWrite(xshutPins[i], HIGH);
    delay(10);
    if(!sensors[i].init()) {
      Serial.print("Errore sensore "); Serial.println(i);
    }
    sensors[i].setAddress(0x21 + i);
    sensors[i].startContinuous();
  }

  // Calibrazione Giroscopio
  Serial.println("Calibrazione MPU... NON MUOVERE IL ROBOT");
  mpu.begin();
  delay(1000);
  mpu.calcOffsets();
  Serial.println("Pronto!");
}

void loop() {
  mpu.update();

  // 1. Lettura Sensori
  float distFront = sensors[0].readRangeContinuousMillimeters() / 10.0;
  float distDX = (sensors[2].readRangeContinuousMillimeters() + sensors[3].readRangeContinuousMillimeters()) / 20.0;

  // 2. Logica decisionale (Priorità a DESTRA)
  if (distDX > SOGLIA_MURO_LAT) {
    // Caso A: La destra è libera -> Gira a destra e avanza
    Serial.println("Muro DX assente: Gira a destra");
    delay(200);      // Breve pausa per stabilità
    rotate(-90);     // Gira a destra
    delay(200);
    moveForward();   // Avanza di una cella
  } 
  else if (distFront > SOGLIA_MURO_FRONT) {
    // Caso B: Muro a DX presente, ma fronte libero -> Vai dritto
    Serial.println("Muro DX presente, fronte libero: Vai dritto");
    moveForward();
  } 
  else {
    // Caso C: Muro a DX e muro Fronte -> Gira a sinistra
    Serial.println("Vicolo cieco o angolo: Gira a sinistra");
    rotate(90);
  }
  
  stopMotors();
  delay(500); // Pausa tra una cella e l'altra per evitare oscillazioni
}

// --- FUNZIONI DI MOVIMENTO (Corrette con i suggerimenti precedenti) ---

void rotate(float angle) {
  float target = mpu.getAngleZ() + angle;
  unsigned long start_time = millis();
  
  while(abs(target - mpu.getAngleZ()) > 2.0 && (millis() - start_time < 3000)) {
    mpu.update();
    float error = target - mpu.getAngleZ();
    int speed = 100 + abs(error) * 2; // Semplice proporzionale
    speed = constrain(speed, 110, 180);

    if(error > 0) motori(speed, -speed); // Sinistra
    else motori(-speed, speed);          // Destra
  }
  stopMotors();
}

void moveForward() {
  unsigned long start_time = millis();
  float start_angle = mpu.getAngleZ();
  
  // Avanza per circa 1.5 secondi (equivalente alla vostra cella da 75 cicli)
  while(millis() - start_time < 1500) {
    mpu.update();
    float gyro_error = start_angle - mpu.getAngleZ();
    
    // Correzione base con giroscopio per andare dritti
    int base_speed = 150;
    int corr = gyro_error * 5; 
    
    motori(base_speed + corr, base_speed - corr);

    // Se un muro appare improvvisamente davanti, fermati per sicurezza
    if(sensors[0].readRangeContinuousMillimeters() / 10.0 < 5.0) break;
  }
  stopMotors();
}

void motori(int sx, int dx) {
  if(dx >= 0) {
    digitalWrite(DX_IN1, HIGH); digitalWrite(DX_IN2, LOW);
  } else {
    digitalWrite(DX_IN1, LOW); digitalWrite(DX_IN2, HIGH);
  }
  if(sx >= 0) {
    digitalWrite(SX_IN1, HIGH); digitalWrite(SX_IN2, LOW);
  } else {
    digitalWrite(SX_IN1, LOW); digitalWrite(SX_IN2, HIGH);
  }
  analogWrite(PWM_DX, abs(dx));
  analogWrite(PWM_SX, abs(sx));
}

void stopMotors() {
  analogWrite(PWM_DX, 0); analogWrite(PWM_SX, 0);
}