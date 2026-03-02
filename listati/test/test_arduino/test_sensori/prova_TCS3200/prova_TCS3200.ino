#define S0 2
#define S1 3
#define S2 4
#define S3 5
#define SO 6

void setup() {
  Serial.begin(9600);
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(SO, INPUT);

  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW); // Frequenza massima
}

int readColor(int s2State, int s3State, int samples = 10) {
  digitalWrite(S2, s2State);
  digitalWrite(S3, s3State);
  delay(10); // Attendi stabilità segnale

  long sum = 0;
  for(int i = 0; i < samples; i++) {
    sum += pulseIn(SO, LOW);   // Lettura singola
  }
  return sum / samples;        // Media campioni
}

void loop() {
  int red = readColor(LOW, LOW);       // Lettura rosso
  int green = readColor(HIGH, HIGH);   // Lettura verde
  int blue = readColor(LOW, HIGH);     // Lettura blu

  // Normalizzazione per confronto più equo
  long total = red + green + blue;
  float rPerc = ((float)red/total)*100.0;
  float gPerc = ((float)green/total)*100.0;
  float bPerc = ((float)blue/total)*100.0;

  Serial.print("Rosso: ");
  Serial.print(red); Serial.print(" (");
  Serial.print(rPerc, 1); Serial.print("%)");

  Serial.print(" Verde: ");
  Serial.print(green); Serial.print(" (");
  Serial.print(gPerc, 1); Serial.print("%)");

  Serial.print(" Blu: ");
  Serial.print(blue); Serial.print(" (");
  Serial.print(bPerc, 1); Serial.print("%)");

  // Rilevamento colore dominante
  String detectedColor;
  if (rPerc < gPerc && rPerc < bPerc) detectedColor = "ROSSO";
  else if (gPerc < rPerc && gPerc < bPerc) detectedColor = "VERDE";
  else if (bPerc < rPerc && bPerc < gPerc) detectedColor = "BLU";
  else detectedColor = "INDISTINTO";

  Serial.print(" --> Colore rilevato: ");
  Serial.println(detectedColor);

  delay(400);
}

