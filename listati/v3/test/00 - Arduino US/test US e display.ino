#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD 20x4 I2C
LiquidCrystal_I2C lcd(0x27, 20, 4); // se non va, prova 0x3F

// Pin ultrasuoni
#define US_FRONT_TRIG 30
#define US_FRONT_ECHO 31
#define US_RIGHT_TRIG 32
#define US_RIGHT_ECHO 33
#define US_LEFT_TRIG 34
#define US_LEFT_ECHO 35

// Funzione per leggere distanza in cm da un sensore HC-SR04
uint16_t readUltrasonic(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // timeout 30ms (~5m)
  if (duration == 0) {
    return 0; // fuori range
  }

  float dist_cm = (duration * 0.0343) / 2.0; // velocità suono ~343 m/s
  return (uint16_t)dist_cm;
}

void setup() {
  Serial.begin(115200);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Test US + LCD");
  delay(1000);
  lcd.clear();

  pinMode(US_FRONT_TRIG, OUTPUT);
  pinMode(US_FRONT_ECHO, INPUT);
  pinMode(US_RIGHT_TRIG, OUTPUT);
  pinMode(US_RIGHT_ECHO, INPUT);
  pinMode(US_LEFT_TRIG, OUTPUT);
  pinMode(US_LEFT_ECHO, INPUT);
}

void loop() {
  // Leggi i tre sensori
  uint16_t usFront = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  uint16_t usRight = readUltrasonic(US_RIGHT_TRIG, US_RIGHT_ECHO);
  uint16_t usLeft  = readUltrasonic(US_LEFT_TRIG, US_LEFT_ECHO);

  // Stampa su Serial (opzionale, utile per debug da PC)
  Serial.print("FRONTE: ");
  Serial.print(usFront);
  Serial.print(" cm, DESTRA: ");
  Serial.print(usRight);
  Serial.print(" cm, SINISTRA: ");
  Serial.print(usLeft);
  Serial.println(" cm");

  // Stampa sul display LCD
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Rilevazione US:");

  lcd.setCursor(0, 1);
  lcd.print("FRONTE: ");
  lcd.print(usFront);
  lcd.print(" cm   ");

  lcd.setCursor(0, 2);
  lcd.print("DESTRA: ");
  lcd.print(usRight);
  lcd.print(" cm   ");

  lcd.setCursor(0, 3);
  lcd.print("SINISTRA: ");
  lcd.print(usLeft);
  lcd.print(" cm   ");

  delay(500); // aggiorna 2 volte al secondo
}
