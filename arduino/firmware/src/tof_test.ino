/*
 * ToF Test — mENIAC RoboCup Junior 2026
 * Invia le rilevazioni di tutti e 6 i sensori VL53L0X ogni 3 secondi.
 * Formato pacchetto: <FRONTE: Xcm, DIETRO: Xcm, DX_ANT: Xcm, DX_POST: Xcm, SX_ANT: Xcm, SX_POST: Xcm>
 */

#include <Wire.h>
#include <VL53L0X.h>

#define XSHUT_FRONT   30
#define XSHUT_BACK    31
#define XSHUT_DX_ANT  32
#define XSHUT_DX_POST 33
#define XSHUT_SX_ANT  34
#define XSHUT_SX_POST 35

#define BAUD_RATE     115200
#define SEND_INTERVAL 3000  // ms

VL53L0X sensors[6];
const int xshutPins[6] = {
  XSHUT_FRONT, XSHUT_BACK,
  XSHUT_DX_ANT, XSHUT_DX_POST,
  XSHUT_SX_ANT, XSHUT_SX_POST
};
const char* labels[6] = {
  "FRONTE", "DIETRO",
  "DX_ANT", "DX_POST",
  "SX_ANT", "SX_POST"
};

bool sensorOk[6] = {false};
unsigned long lastSend = 0;

void setup() {
  Serial.begin(BAUD_RATE);
  Wire.begin();

  // Spegni tutti i sensori
  for (int i = 0; i < 6; i++) {
    pinMode(xshutPins[i], OUTPUT);
    digitalWrite(xshutPins[i], LOW);
  }

  // Accendi uno alla volta e assegna indirizzo univoco
  for (int i = 0; i < 6; i++) {
    digitalWrite(xshutPins[i], HIGH);
    delay(50);
    if (sensors[i].init()) {
      sensors[i].setAddress(0x30 + i);
      sensors[i].startContinuous();
      sensorOk[i] = true;
    } else {
      Serial.println("<ERRORE: sensore " + String(labels[i]) + " non risponde>");
    }
  }

  Serial.println("<SISTEMA PRONTO - invio ogni 3s>");
}

void loop() {
  if (millis() - lastSend < SEND_INTERVAL) return;
  lastSend = millis();

  String pacchetto = "<";
  for (int i = 0; i < 6; i++) {
    if (sensorOk[i]) {
      int cm = sensors[i].readRangeContinuousMillimeters() / 10;
      pacchetto += String(labels[i]) + ": " + String(cm) + "cm";
    } else {
      pacchetto += String(labels[i]) + ": ERR";
    }
    if (i < 5) pacchetto += ", ";
  }
  pacchetto += ">";

  Serial.println(pacchetto);
}
