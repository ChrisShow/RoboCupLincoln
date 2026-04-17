/*
 * File:  flag_test.ino
 * Scopo: Test rilevamento bandiere
 *
 *   Pressione BTN_START → invia <SCAN_FLAG> al Raspberry
 *   Raspberry scatta foto (cam destra) e risponde <FLAG:NOME>
 *   Risultato mostrato su LCD e Serial
 */

#include <LiquidCrystal_I2C.h>

#define BTN_START  43

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_START, INPUT_PULLUP);

  lcd.init(); lcd.backlight();
  lcd.clear(); lcd.print("FLAG TEST");
  lcd.setCursor(0, 1); lcd.print("Premi START");
  Serial.println("[D] Flag test pronto");
}

// ----------------------------------------------------------------
void loop() {
  // Ricezione risposta da Raspberry: <FLAG:NOME>
  if (Serial.available()) {
    String msg = Serial.readStringUntil('>');
    msg.trim();
    if (msg.startsWith("<FLAG:")) {
      String nome = msg.substring(6);  // tutto dopo "<FLAG:"
      Serial.println("[D] Bandiera: " + nome);
      lcd.clear();
      lcd.print("BANDIERA:");
      lcd.setCursor(0, 1);
      lcd.print(nome.substring(0, 16));
    }
  }

  // Pressione pulsante → richiedi scansione
  if (digitalRead(BTN_START) == LOW) {
    delay(50);
    if (digitalRead(BTN_START) == LOW) {
      lcd.clear(); lcd.print("Scansione...");
      lcd.setCursor(0, 1); lcd.print("attendi");
      Serial.println("<SCAN_FLAG>");
      while (digitalRead(BTN_START) == LOW);
      delay(100);
    }
  }
}
