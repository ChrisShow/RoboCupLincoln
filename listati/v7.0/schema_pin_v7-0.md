# 🔌 Schema Pin Franco V7.0 - Tabella Connessioni Complete

**AGGIORNAMENTO V7:** 2 LED separati (ROSSO status + VERDE power)

---

## 📋 **TABELLA COMPLETA PIN ARDUINO MEGA 2560**

| **COMPONENTE** | **Pin/Segnale** | **Arduino MEGA** | **Alimentazione** | **Note** |
|----------------|-----------------|------------------|-------------------|----------|
| **MOTORI (Driver A - Anteriori)** | | | | |
| Front Left (FL) | PWM | Pin 6 | 12V (Driver) | Velocità |
| Front Left (FL) | IN1 | Pin 22 | - | Direzione |
| Front Left (FL) | IN2 | Pin 23 | - | Direzione |
| Front Right (FR) | PWM | Pin 7 | 12V (Driver) | Velocità |
| Front Right (FR) | IN3 | Pin 24 | - | Direzione |
| Front Right (FR) | IN4 | Pin 25 | - | Direzione |
| **MOTORI (Driver B - Posteriori)** | | | | |
| Back Left (BL) | PWM | Pin 8 | 12V (Driver) | Velocità |
| Back Left (BL) | IN1 | Pin 26 | - | Direzione |
| Back Left (BL) | IN2 | Pin 27 | - | Direzione |
| Back Right (BR) | PWM | Pin 9 | 12V (Driver) | Velocità |
| Back Right (BR) | IN3 | Pin 28 | - | Direzione |
| Back Right (BR) | IN4 | Pin 29 | - | Direzione |
| **SENSORI DISTANZA VL53L0X (I2C)** | | | | |
| VL53L0X Front | SDA | Pin 20 | 5V sensori | Indirizzo 0x30 |
| VL53L0X Front | SCL | Pin 21 | 5V sensori | Bus I2C |
| VL53L0X Right | SDA | Pin 20 | 5V sensori | Indirizzo 0x31 |
| VL53L0X Right | SCL | Pin 21 | 5V sensori | Bus I2C |
| VL53L0X Left | SDA | Pin 20 | 5V sensori | Indirizzo 0x32 |
| VL53L0X Left | SCL | Pin 21 | 5V sensori | Bus I2C |
| **SENSORE COLORE TCS3200** | | | | |
| TCS3200 | S0 | Pin 30 | 5V sensori | Freq scaling HIGH |
| TCS3200 | S1 | Pin 31 | 5V sensori | Freq scaling LOW |
| TCS3200 | S2 | Pin 32 | 5V sensori | Color filter |
| TCS3200 | S3 | Pin 33 | 5V sensori | Color filter |
| TCS3200 | OUT | Pin 34 | - | Frequency output |
| TCS3200 | OE | GND | - | Output enable |
| **GIROSCOPIO MPU6050 (I2C)** | | | | |
| MPU6050 | SDA | Pin 20 | 5V sensori | Indirizzo 0x68 |
| MPU6050 | SCL | Pin 21 | 5V sensori | Bus I2C |
| **SERVO MOTORI** | | | | |
| Servo Camera | Segnale | Pin 10 (PWM) | 5V sensori | Pan 0-180° |
| Servo Medikit | Segnale | Pin 11 (PWM) | 5V sensori | Dispenser 0-90° |
| **LED** | | | | |
| 🔴 LED ROSSO | Anodo (+) | **Pin 13** | - | **Lampeggio 5s vittime** |
| 🟢 LED VERDE | Anodo (+) | **Pin 12** | - | **Acceso fisso power ON** |
| **COMUNICAZIONE UART** | | | | |
| Raspberry Pi | TX Arduino | Pin 18 (TX1) | - | Trasmissione → Raspberry |
| Raspberry Pi | RX Arduino | Pin 19 (RX1) | - | Ricezione ← Raspberry |
| **ALIMENTAZIONE ARDUINO** | | | | |
| Arduino MEGA | Vin | 12V Buck | - | Alimentazione principale |
| Arduino MEGA | GND | GND comune | - | Ground comune |

---

## 🔴🟢 **SCHEMA COLLEGAMENTO LED**

### **LED ROSSO (Pin 13) - Status Vittime**
```
Arduino Pin 13 ──→ Resistenza 220Ω ──→ LED ROSSO (+) ──→ GND
```
**Funzione:** Lampeggia 5 secondi (500ms ON/OFF) quando rileva vittima

### **LED VERDE (Pin 12) - Power Indicator**
```
Arduino Pin 12 ──→ Resistenza 220Ω ──→ LED VERDE (+) ──→ GND
```
**Funzione:** Acceso fisso durante funzionamento normale  
**Emergenza:** Lampeggia se Raspberry non risponde

---

## 🔋 **SCHEMA ALIMENTAZIONE SEMPLIFICATO**

```
Batterie 22.2V (LiPo 3S x2)
    │
    ├──→ Buck 22V→12V ──→ Arduino Vin (12V)
    │                └──→ Driver Motori (12V)
    │
    ├──→ Buck 22V→5V ──→ Raspberry Pi (5V)
    │
    └──→ Buck 22V→5V ──→ Sensori (VL53L0X, TCS3200, MPU6050, Servo) (5V)

⚠️ GND COMUNE: batteria, regolatori, Arduino, Raspberry, driver, sensori, LED
```

**Alternativa batterie 12V:**
- Arduino Vin: **12V diretto** (NO Buck)
- Driver Motori: **12V diretto** (NO Buck)
- Raspberry Pi: **Buck 12V→5V obbligatorio**
- Sensori: **Buck 12V→5V**

---

## 📍 **PIN I2C CONDIVISI (Bus Unico)**

**SDA (Pin 20) e SCL (Pin 21) condivisi tra:**
1. VL53L0X Front (0x30)
2. VL53L0X Right (0x31)
3. VL53L0X Left (0x32)
4. MPU6050 (0x68)

**⚠️ IMPORTANTE:** Ogni dispositivo I2C deve avere indirizzo univoco!

---

## 🔧 **RESISTENZE LED**

| LED | Tensione LED | Resistenza | Corrente |
|-----|--------------|------------|----------|
| 🔴 Rosso | ~2V | **220Ω** | ~14mA |
| 🟢 Verde | ~2.2V | **220Ω** | ~13mA |

**Calcolo:** R = (5V - V_led) / 0.015A ≈ 220Ω

---

## 🚦 **COMPORTAMENTO LED**

| Stato | LED VERDE (Pin 12) | LED ROSSO (Pin 13) |
|-------|--------------------|--------------------|
| **Startup** | Acceso fisso | Spento |
| **Funzionamento normale** | Acceso fisso | Spento |
| **Vittima HARMED** | Acceso fisso | Lampeggio 5s → Spento |
| **Vittima STABLE** | Acceso fisso | Lampeggio 5s → Spento |
| **Vittima UNHARMED** | Acceso fisso | Lampeggio 5s → Spento |
| **Emergenza Raspberry** | Lampeggio lento | Spento |
| **Sistema spento** | Spento | Spento |

---

## 📦 **LISTA MATERIALI AGGIUNTIVI LED**

| Componente | Quantità | Spec |
|------------|----------|------|
| LED Rosso 5mm | 1 | 2V, 20mA |
| LED Verde 5mm | 1 | 2.2V, 20mA |
| Resistenza 220Ω | 2 | 1/4W |
| Cavi jumper | 4 | M-M per collegamenti |

---

## ✅ **CHECKLIST CONNESSIONI V7**

### **LED (NUOVO)**
- [ ] LED ROSSO: Pin 13 → Resistenza 220Ω → LED (+) → GND
- [ ] LED VERDE: Pin 12 → Resistenza 220Ω → LED (+) → GND
- [ ] Polarità LED corretta (gamba lunga = anodo +)

### **Motori**
- [ ] Driver A: Pin 6, 7, 22-25 collegati
- [ ] Driver B: Pin 8, 9, 26-29 collegati
- [ ] Driver alimentati a 12V

### **Sensori I2C**
- [ ] VL53L0X x3: SDA → Pin 20, SCL → Pin 21
- [ ] MPU6050: SDA → Pin 20, SCL → Pin 21
- [ ] Tutti sensori alimentati a 5V

### **Sensore Colore**
- [ ] TCS3200: Pin 30-34 collegati
- [ ] Alimentato a 5V

### **Servo**
- [ ] Servo Camera: Pin 10
- [ ] Servo Medikit: Pin 11
- [ ] Alimentati a 5V

### **Comunicazione**
- [ ] UART Raspberry: TX1 (18) ↔ RX, RX1 (19) ↔ TX
- [ ] GND comune Arduino-Raspberry

### **Alimentazione**
- [ ] Arduino Vin: 12V da Buck
- [ ] GND comune verificato con multimetro

---

**🏆 FRANCO V7.0 - SCHEMA PIN COMPLETO**

**Autore:** Team Franco  
**Data:** Marzo 2026  
**Versione:** 7.0 (2 LED separati: ROSSO status + VERDE power)
