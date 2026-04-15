/* 
 * Progetto: mENIAC 2026 - Test Motori
 * Descrizione: Avanti 3s, stop, indietro 3s. Trigger con tasto START.
 */

// --- PIN DEFINITIONS (da hw_links.md) ---
#define PWM_DX 2
#define DX_IN1 22
#define DX_IN2 23
#define PWM_SX 3
#define SX_IN1 24
#define SX_IN2 25
#define BTN_START 43

void setup() {
  // Setup Pin Motori
  pinMode(PWM_DX, OUTPUT);
  pinMode(DX_IN1, OUTPUT);
  pinMode(DX_IN2, OUTPUT);
  pinMode(PWM_SX, OUTPUT);
  pinMode(SX_IN1, OUTPUT);
  pinMode(SX_IN2, OUTPUT);
  
  // Setup Pulsante
  pinMode(BTN_START, INPUT_PULLUP);
  
  // Fermiamo i motori all'avvio
  stopMotors();
}

void loop() {
  // Aspetta che il pulsante venga premuto (segnale LOW)
  if (digitalRead(BTN_START) == LOW) {
    delay(500); // Piccolo debounce
    
    // 1. AVANTI (3 Secondi)
    setMotors(150, 150);
    delay(3000);
    
    // 2. STOP (1 Secondo)
    stopMotors();
    delay(1000);
    
    // 3. INDIETRO (3 Secondi)
    setMotors(-150, -150);
    delay(3000);
    
    // 4. STOP FINALE
    stopMotors();
    
    // Attendi rilascio pulsante per evitare loop infiniti
    while(digitalRead(BTN_START) == LOW);
  }
}

// Funzione helper per il controllo motori
void setMotors(int left, int right) {
  // Logica Invertita: left>=0 (Avanti) attiva LOW su SX_IN1 e HIGH su SX_IN2
  if (left >= 0) {
    digitalWrite(SX_IN1, LOW); digitalWrite(SX_IN2, HIGH);
  } else {
    digitalWrite(SX_IN1, HIGH); digitalWrite(SX_IN2, LOW);
  }
  analogWrite(PWM_SX, abs(left));

  if (right >= 0) {
    digitalWrite(DX_IN1, LOW); digitalWrite(DX_IN2, HIGH);
  } else {
    digitalWrite(DX_IN1, HIGH); digitalWrite(DX_IN2, LOW);
  }
  analogWrite(PWM_DX, abs(right));
}

void stopMotors() {
  digitalWrite(DX_IN1, HIGH);
  digitalWrite(DX_IN2, HIGH);
  digitalWrite(SX_IN1, HIGH);
  digitalWrite(SX_IN2, HIGH);
  analogWrite(PWM_DX, 0);
  analogWrite(PWM_SX, 0);
}
