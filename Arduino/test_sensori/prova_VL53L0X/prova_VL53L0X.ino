#include <Wire.h>         // Libreria per la comunicazione I2C
#include <VL53L0X.h>      // Libreria per il sensore di distanza VL53L0X

VL53L0X sensor;           // Crea l'oggetto 'sensor' per interagire con il VL53L0X

void setup() {
  Serial.begin(9600);     // Inizializza la comunicazione seriale per monitor seriale
  Wire.begin();           // Inizializza l'interfaccia I2C (su Mega: SDA=20, SCL=21)

  // Prova a inizializzare il sensore
  if (!sensor.init()) {
    Serial.println("Errore di inizializzazione sensore VL53L0X");
    while(1) {}           // Blocca il programma in caso di errore
  }

  sensor.setTimeout(500); // Timeout di lettura: attesa massima di 500 ms
  sensor.startContinuous(); // Avvia la lettura continua del sensore
  Serial.println("Sensore inizializzato correttamente");
}

void loop() {
  // Leggi la distanza in millimetri dal sensore
  uint16_t dist = sensor.readRangeContinuousMillimeters();

  // Se la lettura è 65535, significa errore o timeout
  if (dist == 65535) {
    Serial.println("Lettura non valida (timeout)");
  } else {
    Serial.print("Distanza: ");
    Serial.print(dist); 
    Serial.println(" mm"); // Stampa la distanza valida
  }

  // Controlla se si è verificato un timeout nella lettura
  if (sensor.timeoutOccurred()) {
    Serial.println("Timeout sensore"); // Stampa avviso per timeout
  }

  delay(100); // Attende 100 ms prima di una nuova lettura
}

