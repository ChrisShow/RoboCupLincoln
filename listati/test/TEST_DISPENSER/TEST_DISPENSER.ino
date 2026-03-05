//TEST SERVO DISPENSER: DA MONITOR SERIALE PREMERE 1 PER RILASCIARE 1 MEDIKIT E 2 PER RILASCIARNE 2

#include <Servo.h>

Servo dispenser;

const int SERVO_PIN = 11;
const unsigned long STEP_TIME = 90; 

int currentPos = 0;  // zero iniziale

void setup() {
  Serial.begin(9600);
  dispenser.attach(SERVO_PIN, 1000, 2000);
  dispenser.write(90);  // ferma
  Serial.println("Invia 1 o 2:");
}

void loop() {
  if (Serial.available() > 0) {
    int num = Serial.parseInt();
    Serial.read();

    if (num == 1 || num == 2) {
      dispenser.write(180);              // gira
      delay(num * STEP_TIME);            // esatto per num pale
      dispenser.write(90);               // ferma preciso
      currentPos = (currentPos + num * 40) % 360;
      Serial.print("Rilasciati "); Serial.print(num);
      Serial.print(" a "); Serial.println(currentPos);
    }
  }
}



