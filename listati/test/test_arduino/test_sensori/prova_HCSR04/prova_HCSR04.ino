// Definizione dei pin
const int trigPin = 8;    // Pin TRIG collegato al pin digitale 8
const int echoPin = 9;    // Pin ECHO collegato al pin digitale 9

void setup() {
  Serial.begin(9600);         // Avvia la comunicazione seriale per il monitor
  pinMode(trigPin, OUTPUT);   // TRIG come uscita: invierà l'impulso
  pinMode(echoPin, INPUT);    // ECHO come ingresso: riceve il segnale riflesso
}

void loop() {
  long duration;  // Variabile per la durata dell'impulso
  int distance;   // Variabile per la distanza calcolata

  // Assicura TRIG basso per 2 microsecondi
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  // Invio impulso veloce di 10 microsecondi su TRIG
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Misura il tempo che ECHO rimane alto (microsecondi)
  duration = pulseIn(echoPin, HIGH);

  // Distanza in cm: durata/58 (guarda la velocità del suono nell'aria)
  distance = duration / 58;

  // Visualizza la distanza calcolata
  Serial.print("Distanza: ");
  Serial.print(distance);
  Serial.println(" cm");

  delay(200); // Attendi 200 ms prima del prossimo ciclo
}
