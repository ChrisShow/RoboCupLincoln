#include <Servo.h>      // Libreria standard per servomotori

Servo mioServo;         // Crea l'oggetto servo

const int servoPin = 10; // Pin PWM per il segnale del servo

void setup() {
  mioServo.attach(servoPin); // Collega il servo al pin specificato
  Serial.begin(9600);        // Avvia monitor seriale
}

void loop() {
  // I servo a 360° NON hanno posizionamento angolare, ma velocità/rotazione!
  // 0 = RUOTA VELOCEMENTE IN UN SENSO
  // 90 ≈ STOP (alcuni servomotori si fermano intorno a 90-95)
  // 180 = RUOTA VELOCEMENTE NELL'ALTRO SENSO

  mioServo.write(0);         // Ruota un senso
  Serial.println("Rotazione oraria");
  delay(2000);               // Ruota per 2 secondi

  mioServo.write(90);        // Prova a fermare (dipende dal tuo SG90)
  Serial.println("Stop");
  delay(2000);               // Ferma per 2 secondi

  mioServo.write(180);       // Ruota nell'altro senso
  Serial.println("Rotazione antioraria");
  delay(2000);               // Ruota per 2 secondi

  mioServo.write(90);        // Torna allo stop
  delay(2000);               // Ferma 2 secondi
}
