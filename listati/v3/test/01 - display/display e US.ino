#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4); // se non va, prova 0x3F

// Pin ultrasuoni
#define US_FRONT_TRIG 30
#define US_FRONT_ECHO 31
#define US_RIGHT_TRIG 32
#define US_RIGHT_ECHO 33
#define US_LEFT_TRIG 34
#define US_LEFT_ECHO 35

String incoming = "";

void setup() {
  Serial.begin(115200);
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Test LCD + US");
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
  // 1) Lettura ultrasuoni e invio a Raspberry
  uint16_t usFront = readUltrasonic(US_FRONT_TRIG, US_FRONT_ECHO);
  uint16_t usRight = readUltrasonic(US_RIGHT_TRIG, US_RIGHT_ECHO);
  uint16_t usLeft  = readUltrasonic(US_LEFT_TRIG, US_LEFT_ECHO);

  // Mostra sul display
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Rilevazione US:");

  lcd.setCursor(0, 1);
  lcd.print("FRONTE: ");
  lcd.print(usFront);
  lcd.print(" cm");

  lcd.setCursor(0, 2);
  lcd.print("DESTRA: ");
  lcd.print(usRight);
  lcd.print(" cm");

  lcd.setCursor(0, 3);
  lcd.print("SINISTRA: ");
  lcd.print(usLeft);
  lcd.print(" cm");

  // Costruisci stringa per Raspberry
  String msg = "USFRONTE:" + String(usFront) +
               ",USDESTRA:" + String(usRight) +
               ",USSINISTRA:" + String(usLeft);

  Serial.println(msg);  // invia a Raspberry

  // 2) Ora attendi risposta da Raspberry
  unsigned long startWait = millis();
  incoming = "";

  while ((millis() - startWait) < 10000) { // timeout 10s
    if (Serial.available()) {
      incoming = Serial.readStringUntil('\n');
      incoming.trim();
      break;
    }
  }

  if (incoming.length() > 0) {
    // Visualizza stringa ricevuta con prefisso "RB PI dice:"
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RB PI dice:");

    // Se la stringa è più corta di 60, la riempi, altrimenti la tagli
    String s = incoming;
    if (s.length() < 60) {
      while (s.length() < 60) s += " ";
    } else if (s.length() > 60) {
      s = s.substring(0, 60);
    }

    // Riga 1
    lcd.setCursor(0, 1);
    lcd.print(s.substring(0, 20));
    // Riga 2
    lcd.setCursor(0, 2);
    lcd.print(s.substring(20, 40));
    // Riga 3
    lcd.setCursor(0, 3);
    lcd.print(s.substring(40, 60));

    delay(5000);
  }

  // Piccola pausa prima del prossimo ciclo
  delay(2000);
}

// -------- Funzione ultrasuoni --------
uint16_t readUltrasonic(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 0;

  float dist_cm = (duration * 0.0343) / 2.0;
  return (uint16_t)dist_cm;
}
