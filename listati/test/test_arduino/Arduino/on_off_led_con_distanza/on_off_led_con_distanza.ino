const int trigPin = 7; // Pin collegato a TRIG
const int echoPin = 8; // Pin collegato a ECHO
const int ledPin = 13; // LED collegato al pin 13


long duration;
float distance;

void setup() {
   Serial.begin(9600); // Avvia la comunicazione seriale
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

void loop() {
  if (Serial.available()) {
    String comando = Serial.readStringUntil('\n'); // legge fino al carattere di nuova riga

    comando.trim(); // rimuove eventuali spazi o ritorni a capo

    if (comando == "ON") {
       digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Legge durata dell'eco
  duration = pulseIn(echoPin, HIGH);

  // Calcola distanza in centimetri
  distance = duration * 0.034 / 2;

  // Stampa solo se distanza < 20 cm
  if (distance > 0 && distance < 20) {
    Serial.print("Oggetto rilevato a: ");
    Serial.print(distance);
    Serial.println(" cm");
  }

  delay(200); // Piccola pausa
    } else if (comando == "OFF") {
      digitalWrite(ledPin, LOW);
      Serial.println("LED SPENTO");
    } else {
      Serial.println("Comando non riconosciuto");
    }
  }
}
