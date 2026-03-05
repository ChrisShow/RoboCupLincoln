
const int IN1 = 22; 
const int IN2 = 23;
const int IN3 = 24;
const int IN4 = 25;

const int IN5 = 26; 
const int IN6 = 27;
const int IN7 = 28;
const int IN8 = 29;

void setup() {
  
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(IN5, OUTPUT);
  pinMode(IN6, OUTPUT);
  pinMode(IN7, OUTPUT);
  pinMode(IN8, OUTPUT);
  
  
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);

  digitalWrite(IN5, LOW);
  digitalWrite(IN6, LOW);
  digitalWrite(IN7, LOW);
  digitalWrite(IN8, LOW);
  
  Serial.begin(9600);  
  Serial.println("Test motore DC");
}

void loop() {
  /*
  Serial.println("Avanti motore 1");
  digitalWrite(IN1, HIGH);  
  digitalWrite(IN2, LOW);
  delay(3000);

  Serial.println("Fermo motore 1");
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  delay(2000);

  Serial.println("Avanti motore 2");
  digitalWrite(IN3, HIGH);  
  digitalWrite(IN4, LOW);
  delay(3000);   

  Serial.println("Fermo motore 2");
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  delay(2000);

  Serial.println("Indietro motore 1");
  digitalWrite(IN1, LOW);   
  digitalWrite(IN2, HIGH);
  delay(3000);  

  Serial.println("Fermo motore 1");
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  delay(2000);  

  Serial.println("Indietro motore 2");
  digitalWrite(IN3, LOW);   
  digitalWrite(IN4, HIGH);
  delay(3000);

  Serial.println("Fermo motore 1");
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  delay(2000);  
  */

  Serial.println("Avanti motore 1 e 2");
  digitalWrite(IN1, HIGH);  
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  delay(3000);

  digitalWrite(IN5, LOW);  
  digitalWrite(IN6, HIGH);
  digitalWrite(IN7, LOW);
  digitalWrite(IN8, HIGH);
  delay(3000);


}
