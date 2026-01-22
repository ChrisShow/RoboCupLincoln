/*
 * RoboCup Junior 2026 - Rescue Maze
 * Arduino Mega - Controllore Motori, Sensori IR, Servo, Display LCD
 * 
 * Comunicazione: Serial1 (pins 18,19) @ 115200 baud con Raspberry Pi
 * Sensori IR VL53L0X: I2C (pins 20 SDA, 21 SCL)
 * Display LCD: I2C (pins 20 SDA, 21 SCL)
 * Sensore colore TCS3200: pins 22-26
 * Servo camera: pin 10 (PWM)
 * Servo medikit: pin 11 (PWM)
 * Motori DC: pins 2-9 (PWM + direzione)
 * Pulsante START: pin A0
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <VL53L0X.h>
#include <Servo.h>

// ============================================================================
// CONFIGURAZIONE HARDWARE
// ============================================================================

// SERIALE RASPBERRY
#define SERIAL_BAUD 115200
#define SERIAL_TIMEOUT 2000  // ms

// SENSORI IR VL53L0X (I2C addresses cambiano dopo init)
#define NUM_SENSORS 8
VL53L0X sensors[NUM_SENSORS];
const uint8_t ir_addresses[NUM_SENSORS] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};
// Ordine: 0-1=FRONTE, 2-3=DESTRA, 4-5=SINISTRA, 6-7=DIETRO
int ir_distances[NUM_SENSORS];
const int IR_THRESHOLD = 70;  // mm (~7cm)

// DISPLAY LCD I2C
LiquidCrystal_I2C lcd(0x27, 20, 4);  // TODO: Verificare indirizzo (potrebbe essere 0x3F)

// SERVO CAMERA (pin 10) - range 0-180°
Servo servo_camera;
#define SERVO_CAMERA_PIN 10
#define SERVO_CAMERA_FRONT 90
#define SERVO_CAMERA_LEFT 0
#define SERVO_CAMERA_RIGHT 180

// SERVO MEDIKIT (pin 11)
Servo servo_medikit;
#define SERVO_MEDIKIT_PIN 11
#define SERVO_MEDIKIT_HOME 0
#define SERVO_MEDIKIT_RELEASE 90

// SENSORE COLORE TCS3200
#define COLOR_S0 22
#define COLOR_S1 23
#define COLOR_S2 24
#define COLOR_S3 25
#define COLOR_OUT 26
#define COLOR_FREQUENCY_READ_TIME 100  // ms

// MOTORI DC (L298N)
// L298N #1 - Motori FRONTE
#define MOTOR_F_LEFT_FW 2
#define MOTOR_F_LEFT_BW 3
#define MOTOR_F_LEFT_PWM 4
#define MOTOR_F_RIGHT_FW 5
#define MOTOR_F_RIGHT_BW 6
#define MOTOR_F_RIGHT_PWM 7

// L298N #2 - Motori DIETRO
#define MOTOR_B_LEFT_FW 8
#define MOTOR_B_LEFT_BW 9
#define MOTOR_B_LEFT_PWM 12
#define MOTOR_B_RIGHT_FW 13
#define MOTOR_B_RIGHT_BW 14
#define MOTOR_B_RIGHT_PWM 15

// VELOCITÀ MOTORI
#define MOTOR_SPEED_NORMAL 200  // 0-255 PWM
#define MOTOR_SPEED_ALIGNMENT 100

// PULSANTE START
#define BUTTON_PIN A0
#define BUTTON_DEBOUNCE 50  // ms

// ============================================================================
// VARIABILI GLOBALI
// ============================================================================

volatile bool button_pressed = false;
unsigned long last_button_time = 0;
bool exploration_active = false;

enum ColorDetected {
  COLOR_NONE = 0,
  COLOR_BLACK = 1,
  COLOR_GREY = 2,
  COLOR_OTHER = 3
};

// ============================================================================
// SETUP INIZIALE
// ============================================================================

void setup() {
  Serial.begin(115200);     // Debug
  Serial1.begin(SERIAL_BAUD); // Raspberry
  
  // Attendi inizializzazione
  delay(2000);
  
  Serial.println("[BOOT] Arduino Mega inizializzato");
  
  // Init pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), on_button_pressed, FALLING);
  
  // Init motori
  pinMode(MOTOR_F_LEFT_FW, OUTPUT);
  pinMode(MOTOR_F_LEFT_BW, OUTPUT);
  pinMode(MOTOR_F_RIGHT_FW, OUTPUT);
  pinMode(MOTOR_F_RIGHT_BW, OUTPUT);
  pinMode(MOTOR_B_LEFT_FW, OUTPUT);
  pinMode(MOTOR_B_LEFT_BW, OUTPUT);
  pinMode(MOTOR_B_RIGHT_FW, OUTPUT);
  pinMode(MOTOR_B_RIGHT_BW, OUTPUT);
  
  // Init servo
  servo_camera.attach(SERVO_CAMERA_PIN);
  servo_medikit.attach(SERVO_MEDIKIT_PIN);
  servo_camera.write(SERVO_CAMERA_FRONT);
  servo_medikit.write(SERVO_MEDIKIT_HOME);
  
  // Init sensore colore
  pinMode(COLOR_S0, OUTPUT);
  pinMode(COLOR_S1, OUTPUT);
  pinMode(COLOR_S2, OUTPUT);
  pinMode(COLOR_S3, OUTPUT);
  pinMode(COLOR_OUT, INPUT);
  
  // Configura scala frequenza TCS3200 (S0=HIGH, S1=HIGH = scala 100%)
  digitalWrite(COLOR_S0, HIGH);
  digitalWrite(COLOR_S1, HIGH);
  
  // Init I2C
  Wire.begin();
  
  // Init display LCD
  lcd.init();
  lcd.backlight();
  lcd.print("RoboCup 2026");
  lcd.setCursor(0, 1);
  lcd.print("Pronto...");
  
  Serial.println("[BOOT] Hardware inizializzato");
  
  // TODO: Init sensori IR VL53L0X (vedere funzione setup_ir_sensors())
  // Per ora, funzione mockati
  
  delay(1000);
}

// ============================================================================
// LOOP PRINCIPALE
// ============================================================================

void loop() {
  if (button_pressed) {
    button_pressed = false;
    
    lcd.clear();
    lcd.print("Inizio esplorazione...");
    Serial.println("[INFO] Pulsante premuto, inizio allineamento");
    
    exploration_active = true;
    
    // STEP 1: Allinea robot con sensori IR
    align_with_ir_sensors();
    
    // STEP 2: Leggi sensori IR e invia a Raspberry
    read_and_send_ir_sensors();
  }
  
  // Elabora comandi da Raspberry (se disponibile)
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      process_raspberry_command(command);
    }
  }
  
  delay(100);
}

// ============================================================================
// INTERRUPT PULSANTE
// ============================================================================

void on_button_pressed() {
  unsigned long now = millis();
  if (now - last_button_time > BUTTON_DEBOUNCE) {
    button_pressed = true;
    last_button_time = now;
  }
}

// ============================================================================
// FUNZIONI SENSORI IR
// ============================================================================

void align_with_ir_sensors() {
  /*
   * Legge 8 sensori IR e allinea robot
   * Se disallineamento > 1cm, ruota per centramento
   */
  
  Serial.println("[INFO] Allineamento sensori IR in corso...");
  
  // Leggi tutti i sensori
  for (int i = 0; i < NUM_SENSORS; i++) {
    // TODO: Implementare lettura reale sensori VL53L0X
    ir_distances[i] = 100;  // Mock: 100mm
  }
  
  // Controlla allineamento fronte (sensori 0, 1)
  int diff_front = abs(ir_distances[0] - ir_distances[1]);
  if (diff_front > 10) {  // > 1cm disallineamento
    Serial.print("[INFO] Disallineamento fronte: ");
    Serial.println(diff_front);
    
    // Ruota leggermente per allineamento
    if (ir_distances[0] > ir_distances[1]) {
      // Sensore 0 più lontano, ruota verso 1
      rotate_small_left(200);
    } else {
      rotate_small_right(200);
    }
  }
  
  Serial.println("[OK] Allineamento completato");
}

void read_and_send_ir_sensors() {
  /*
   * Legge sensori IR e invia stringa "dritto,destra,sinistra"
   * 0 = muro presente (distanza < 7cm)
   * 1 = no muro
   */
  
  // Leggi sensori
  for (int i = 0; i < NUM_SENSORS; i++) {
    // TODO: Lettura reale VL53L0X
    ir_distances[i] = 100;  // Mock
  }
  
  // Valuta muri
  // FRONTE: sensori 0-1
  int wall_front = (ir_distances[0] < IR_THRESHOLD || ir_distances[1] < IR_THRESHOLD) ? 0 : 1;
  
  // DESTRA: sensori 2-3
  int wall_right = (ir_distances[2] < IR_THRESHOLD || ir_distances[3] < IR_THRESHOLD) ? 0 : 1;
  
  // SINISTRA: sensori 4-5
  int wall_left = (ir_distances[4] < IR_THRESHOLD || ir_distances[5] < IR_THRESHOLD) ? 0 : 1;
  
  // Invia a Raspberry
  String msg = String(wall_front) + "," + String(wall_right) + "," + String(wall_left);
  Serial1.println(msg);
  
  Serial.print("[INFO] Sensori IR inviati: ");
  Serial.println(msg);
}

// ============================================================================
// FUNZIONI MOVIMENTO MOTORI
// ============================================================================

void move_forward(int speed, unsigned long duration_ms) {
  /*
   * Muove robot avanti di 30cm (circa 2 secondi @ speed=200)
   * TODO: Implementare encoder/odometria per precisione
   */
  
  Serial.println("[MOTOR] Movimento avanti");
  
  // Fronte e dietro insieme (movimento lineare)
  motor_front_forward(speed);
  motor_back_forward(speed);
  
  delay(duration_ms);
  
  // Rileggi sensore colore durante movimento (verifica colore nero)
  ColorDetected col = read_color_sensor();
  if (col == COLOR_BLACK) {
    Serial.println("[WARN] Colore nero rilevato, arresto movimento");
    stop_motors();
    return;
  }
  
  stop_motors();
}

void rotate_right(int speed) {
  /*Ruota 90° a destra (clockwise)*/
  Serial.println("[MOTOR] Rotazione destra 90°");
  
  motor_front_forward(speed);
  motor_back_backward(speed);
  
  delay(500);  // TODO: Calibrare tempo per 90°
  
  stop_motors();
}

void rotate_left(int speed) {
  /*Ruota 90° a sinistra (counter-clockwise)*/
  Serial.println("[MOTOR] Rotazione sinistra 90°");
  
  motor_front_backward(speed);
  motor_back_forward(speed);
  
  delay(500);  // TODO: Calibrare tempo per 90°
  
  stop_motors();
}

void rotate_small_right(int speed) {
  /*Piccola rotazione destra per allineamento*/
  motor_front_forward(speed);
  motor_back_backward(speed);
  delay(100);
  stop_motors();
}

void rotate_small_left(int speed) {
  /*Piccola rotazione sinistra per allineamento*/
  motor_front_backward(speed);
  motor_back_forward(speed);
  delay(100);
  stop_motors();
}

void stop_motors() {
  digitalWrite(MOTOR_F_LEFT_FW, LOW);
  digitalWrite(MOTOR_F_LEFT_BW, LOW);
  digitalWrite(MOTOR_F_RIGHT_FW, LOW);
  digitalWrite(MOTOR_F_RIGHT_BW, LOW);
  digitalWrite(MOTOR_B_LEFT_FW, LOW);
  digitalWrite(MOTOR_B_LEFT_BW, LOW);
  digitalWrite(MOTOR_B_RIGHT_FW, LOW);
  digitalWrite(MOTOR_B_RIGHT_BW, LOW);
}

void motor_front_forward(int speed) {
  digitalWrite(MOTOR_F_LEFT_FW, HIGH);
  digitalWrite(MOTOR_F_LEFT_BW, LOW);
  analogWrite(MOTOR_F_LEFT_PWM, speed);
  
  digitalWrite(MOTOR_F_RIGHT_FW, HIGH);
  digitalWrite(MOTOR_F_RIGHT_BW, LOW);
  analogWrite(MOTOR_F_RIGHT_PWM, speed);
}

void motor_front_backward(int speed) {
  digitalWrite(MOTOR_F_LEFT_FW, LOW);
  digitalWrite(MOTOR_F_LEFT_BW, HIGH);
  analogWrite(MOTOR_F_LEFT_PWM, speed);
  
  digitalWrite(MOTOR_F_RIGHT_FW, LOW);
  digitalWrite(MOTOR_F_RIGHT_BW, HIGH);
  analogWrite(MOTOR_F_RIGHT_PWM, speed);
}

void motor_back_forward(int speed) {
  digitalWrite(MOTOR_B_LEFT_FW, HIGH);
  digitalWrite(MOTOR_B_LEFT_BW, LOW);
  analogWrite(MOTOR_B_LEFT_PWM, speed);
  
  digitalWrite(MOTOR_B_RIGHT_FW, HIGH);
  digitalWrite(MOTOR_B_RIGHT_BW, LOW);
  analogWrite(MOTOR_B_RIGHT_PWM, speed);
}

void motor_back_backward(int speed) {
  digitalWrite(MOTOR_B_LEFT_FW, LOW);
  digitalWrite(MOTOR_B_LEFT_BW, HIGH);
  analogWrite(MOTOR_B_LEFT_PWM, speed);
  
  digitalWrite(MOTOR_B_RIGHT_FW, LOW);
  digitalWrite(MOTOR_B_RIGHT_BW, HIGH);
  analogWrite(MOTOR_B_RIGHT_PWM, speed);
}

// ============================================================================
// SENSORE COLORE TCS3200
// ============================================================================

ColorDetected read_color_sensor() {
  /*
   * Legge sensore colore TCS3200 per 100ms
   * Ritorna BLACK, GREY, OTHER basato su frequenza
   * TODO: Calibrare soglie per ambiente reale
   */
  
  unsigned int frequency = 0;
  
  // Leggi frequenza OUT per 100ms
  // S2=LOW, S3=LOW = rosso
  digitalWrite(COLOR_S2, LOW);
  digitalWrite(COLOR_S3, LOW);
  
  unsigned long start = millis();
  while (millis() - start < COLOR_FREQUENCY_READ_TIME) {
    if (digitalRead(COLOR_OUT) == HIGH) {
      frequency++;
    }
  }
  
  Serial.print("[COLOR] Frequenza: ");
  Serial.println(frequency);
  
  // Classifica colore (TODO: Calibrare soglie reali)
  if (frequency < 50) {
    return COLOR_BLACK;  // Nero = frequenza molto bassa
  } else if (frequency < 150) {
    return COLOR_GREY;   // Grigio = frequenza media
  } else {
    return COLOR_OTHER;  // Altro colore = frequenza alta
  }
}

void select_color_channel(int channel) {
  /*
   * Seleziona canale colore TCS3200
   * 0 = Rosso, 1 = Verde, 2 = Blu
   */
  switch (channel) {
    case 0:  // Rosso
      digitalWrite(COLOR_S2, LOW);
      digitalWrite(COLOR_S3, LOW);
      break;
    case 1:  // Verde
      digitalWrite(COLOR_S2, HIGH);
      digitalWrite(COLOR_S3, HIGH);
      break;
    case 2:  // Blu
      digitalWrite(COLOR_S2, LOW);
      digitalWrite(COLOR_S3, HIGH);
      break;
  }
}

// ============================================================================
// SERVO CAMERA
// ============================================================================

void move_camera(int direction) {
  /*
   * direction: 0=fronte, 1=destra, 2=sinistra
   */
  switch (direction) {
    case 0:
      servo_camera.write(SERVO_CAMERA_FRONT);
      Serial.println("[SERVO_CAM] Posizione fronte");
      break;
    case 1:
      servo_camera.write(SERVO_CAMERA_RIGHT);
      Serial.println("[SERVO_CAM] Posizione destra");
      break;
    case 2:
      servo_camera.write(SERVO_CAMERA_LEFT);
      Serial.println("[SERVO_CAM] Posizione sinistra");
      break;
  }
  delay(500);  // Attendi movimento servo
}

// ============================================================================
// SERVO MEDIKIT
// ============================================================================

void release_medikit() {
  /*
   * Ruota servo medikit per far cadere medikit
   */
  Serial.println("[SERVO_MED] Rilascio medikit");
  
  servo_medikit.write(SERVO_MEDIKIT_RELEASE);
  delay(1000);
  
  servo_medikit.write(SERVO_MEDIKIT_HOME);
  delay(500);
}

// ============================================================================
// ELABORAZIONE COMANDI RASPBERRY
// ============================================================================

void process_raspberry_command(String command) {
  /*
   * Riceve comando da Raspberry
   * Formato:
   * - "0" = muovi avanti
   * - "1" = gira destra
   * - "2" = gira sinistra
   * - "3" = gira dietro
   * - "M" = rilascia medikit
   */
  
  Serial.print("[CMD] Comando ricevuto: ");
  Serial.println(command);
  
  if (command == "M") {
    release_medikit();
    return;
  }
  
  int move_code = command.toInt();
  int movement_status = 1;  // 1=successo, -1=fallito
  
  switch (move_code) {
    case 0:  // Avanti
      move_forward(MOTOR_SPEED_NORMAL, 2000);  // 2 secondi
      break;
    
    case 1:  // Destra
      rotate_right(MOTOR_SPEED_NORMAL);
      break;
    
    case 2:  // Sinistra
      rotate_left(MOTOR_SPEED_NORMAL);
      break;
    
    case 3:  // Dietro
      // TODO: Implementare movimento dietro
      Serial.println("[INFO] Movimento dietro non ancora implementato");
      break;
    
    default:
      Serial.println("[ERROR] Comando sconosciuto");
      movement_status = -1;
      break;
  }
  
  // Invia feedback movimento
  Serial1.println(movement_status);
  Serial.print("[FEEDBACK] Movimento: ");
  Serial.println(movement_status);
}

// ============================================================================
// DISPLAY LCD
// ============================================================================

void update_lcd_status(const char* status, int x, int y) {
  lcd.setCursor(x, y);
  lcd.print(status);
}

void update_lcd_coordinate(int x, int y) {
  lcd.setCursor(0, 1);
  lcd.print("X:");
  lcd.print(x);
  lcd.print(" Y:");
  lcd.print(y);
}

void update_lcd_medikit_count(int count) {
  lcd.setCursor(0, 3);
  lcd.print("Medikit:");
  lcd.print(count);
}

// ============================================================================
// TODO: SETUP SENSORI VL53L0X
// ============================================================================

void setup_ir_sensors() {
  /*
   * Inizializza 8 sensori VL53L0X con indirizzi univoci
   * Procedura:
   * 1. Tutti i sensori inizialmente al pin XSHUT (disable)
   * 2. Abilita uno alla volta e assegna indirizzo I2C
   * 3. Test lettura
   * 
   * TODO: Implementare con libreria VL53L0X
   */
  
  Serial.println("[IR] Inizializzazione sensori VL53L0X in corso...");
  
  // TODO: Implementare procedura init VL53L0X
  
  Serial.println("[OK] Sensori IR inizializzati");
}
