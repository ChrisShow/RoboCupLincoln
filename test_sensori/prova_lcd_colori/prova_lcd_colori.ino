#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Indirizzo LCD I2C comunemente 0x27
#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

// Pin sensore TCS3200
#define S0 2
#define S1 3
#define S2 4
#define S3 5
#define SO 6

void setup() {
  // LCD init
  lcd.init();
  lcd.backlight();

  // Sensore colore
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(SO, INPUT);

  digitalWrite(S0, HIGH);   // Frequenza massima
  digitalWrite(S1, LOW);

  lcd.setCursor(0,0);
  lcd.print("   COLOR SENSOR    ");
  lcd.setCursor(0,1);
  lcd.print("Rilevando colore...");
}

int readColor(int s2State, int s3State, int samples = 10) {
  digitalWrite(S2, s2State);
  digitalWrite(S3, s3State);
  delay(10);

  long sum = 0;
  for(int i = 0; i < samples; i++) {
    sum += pulseIn(SO, LOW);
  }
  return sum / samples;
}

void loop() {
  int red = readColor(LOW, LOW);
  int green = readColor(HIGH, HIGH);
  int blue = readColor(LOW, HIGH);

  // Normalizzazione
  long total = red + green + blue;
  float rPerc = ((float)red/total)*100.0;
  float gPerc = ((float)green/total)*100.0;
  float bPerc = ((float)blue/total)*100.0;

  // Rileva colore dominante
  String detectedColor;
  if (rPerc < gPerc && rPerc < bPerc) detectedColor = "ROSSO";
  else if (gPerc < rPerc && gPerc < bPerc) detectedColor = "VERDE";
  else if (bPerc < rPerc && bPerc < gPerc) detectedColor = "BLU";
  else detectedColor = "INDISTINTO";

  // Visualizza su LCD
  lcd.setCursor(0,2);
  lcd.print("Colore:           ");
  lcd.setCursor(8,2);
  lcd.print(detectedColor);

  lcd.setCursor(0,3);
  lcd.print("R:"); lcd.print(red);
  lcd.print(" G:"); lcd.print(green);
  lcd.print(" B:"); lcd.print(blue);

  delay(500);
}
