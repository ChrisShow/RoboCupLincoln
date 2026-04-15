/* 
 * Progetto: mENIAC 2026 - Test Colore Interattivo
 * Descrizione: Click START -> Leggi e Invia a RPi. Double Click -> Stop.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define TCS_S0 38
#define TCS_S1 39
#define TCS_S2 40
#define TCS_S3 41
#define TCS_OUT 42
#define BTN_START 43

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(115200);
  lcd.init(); lcd.backlight();
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT); pinMode(TCS_OUT, INPUT);
  pinMode(BTN_START, INPUT_PULLUP);
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW); // Frequenza al 20%
  lcd.print("COLOR TEST READY");
  lcd.setCursor(0,1); lcd.print("PRESS START");
}

void loop() {
  if (digitalRead(BTN_START) == LOW) {
    unsigned long firstClick = millis();
    delay(200); // Debounce
    while(digitalRead(BTN_START) == LOW); // Aspetta rilascio
    
    // Attendi un eventuale secondo click per 500ms
    bool doubleClick = false;
    while(millis() - firstClick < 500) {
      if (digitalRead(BTN_START) == LOW) {
        doubleClick = true;
        break;
      }
    }
    
    if (doubleClick) {
      lcd.clear(); lcd.print("STOPPING...");
      Serial.println("<STOP>");
      while(1); // Ferma il programma
    } else {
      // Click singolo: Leggi Colore
      digitalWrite(TCS_S2, LOW); digitalWrite(TCS_S3, LOW); int r = pulseIn(TCS_OUT, LOW);
      digitalWrite(TCS_S2, HIGH); digitalWrite(TCS_S3, HIGH); int g = pulseIn(TCS_OUT, LOW);
      digitalWrite(TCS_S2, LOW); digitalWrite(TCS_S3, HIGH); int b = pulseIn(TCS_OUT, LOW);
      
      lcd.clear();
      lcd.print("R:"); lcd.print(r); lcd.print(" G:"); lcd.print(g);
      lcd.setCursor(0,1);
      lcd.print("B:"); lcd.print(b);
      
      // Invia a Raspberry
      Serial.print("<COLOR:"); Serial.print(r); Serial.print(","); 
      Serial.print(g); Serial.print(","); Serial.print(b); Serial.println(">");
    }
  }
}
