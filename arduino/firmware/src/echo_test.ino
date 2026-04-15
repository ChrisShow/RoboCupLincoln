/*
 * Echo Test — mENIAC RoboCup Junior 2026
 * Riceve <msg:parola> da Raspberry Pi,
 * mostra la parola sul display LCD,
 * reinvia lo stesso pacchetto al Raspberry.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define BAUD_RATE 115200

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(BAUD_RATE);
  Wire.begin();
  lcd.init();
  lcd.backlight();

  lcd.print("Echo Test");
  lcd.setCursor(0, 1);
  lcd.print("In attesa...");
}

void loop() {
  if (!Serial.available()) return;

  // Leggi fino al delimitatore '>'
  String pacchetto = Serial.readStringUntil('>');
  pacchetto.trim();

  // Verifica formato: deve iniziare con "<msg:"
  if (!pacchetto.startsWith("<msg:")) return;

  // Estrai la parola (tutto dopo "<msg:")
  String parola = pacchetto.substring(5);

  // Mostra sul display LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ricevuto:");
  lcd.setCursor(0, 1);
  lcd.print(parola);

  // Reinvia il pacchetto originale al Raspberry (echo)
  Serial.println("<msg:" + parola + ">");
}
