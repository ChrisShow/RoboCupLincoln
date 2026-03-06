# Schema Elettrico Franco V6.0 - RoboCup Rescue Maze 2026

**AGGIORNAMENTO V6:** Aggiunti commenti divisore tensione per batterie 12V

---

## 🔌 **ALIMENTAZIONE**

### **Batterie**
- **2x Batterie LiPo 3S 11.1V** (in serie → 22.2V totale)
- **Capacità consigliata:** 2200-3000 mAh
- **Corrente max:** 30A continua

### **⚠️ DIVISORE TENSIONE PER BATTERIE 12V (Alternativa)**
Se usi batterie al piombo 12V invece di LiPo 22.2V:

**MODIFICHE NECESSARIE:**

1. **Per Arduino MEGA (Vin 7-12V):**
   - ✅ Collegamento diretto: Batteria 12V → Arduino Vin (OK)
   - NO divisore tensione necessario

2. **Per Driver Motori (6-12V raccomandato):**
   - ✅ Collegamento diretto: Batteria 12V → Driver VCC (OK)
   - NO divisore tensione necessario

3. **⚠️ ATTENZIONE: Raspberry Pi (5V richiesti)**
   - ❌ NON collegare 12V direttamente!
   - ✅ Usa **DC-DC Buck Converter 12V→5V 3A**
   - Esempio: LM2596 o XL4015
   - **Schema:**
     ```
     Batteria 12V (+) ──→ Buck IN+
     Batteria 12V (-) ──→ Buck IN-
     Buck OUT+ (5V)   ──→ Raspberry Pi GPIO 5V
     Buck OUT- (GND)  ──→ Raspberry Pi GND
     ```

**📌 NOTA:** Con batterie 12V hai **meno autonomia** e **potenza motori ridotta**. Consigliato LiPo 3S per prestazioni ottimali.

### **Regolatori di Tensione**
1. **DC-DC Buck Converter 22V → 12V 3A** (per Arduino + Driver)
   - IN+: 22.2V batteria
   - IN-: GND batteria
   - OUT+: 12V (Arduino Vin + Driver VCC)
   - OUT-: GND comune

2. **DC-DC Buck Converter 22V → 5V 3A** (per Raspberry Pi)
   - IN+: 22.2V batteria
   - IN-: GND batteria
   - OUT+: 5V (Raspberry Pi GPIO 5V)
   - OUT-: GND comune

3. **DC-DC Buck Converter 22V → 5V 2A** (per sensori)
   - IN+: 22.2V batteria
   - IN-: GND batteria
   - OUT+: 5V (VL53L0X, TCS3200, servo)
   - OUT-: GND comune

**⚠️ IMPORTANTE:** Tutti i GND devono essere comuni!

---

## 🤖 **ARDUINO MEGA 2560**

### **Alimentazione**
- **Vin:** 12V da regolatore DC-DC
- **GND:** GND comune

### **Pin Motori (PWM + Digitali)**

#### **Driver A - Ruote Anteriori**
| Motore | PWM | IN1 | IN2 |
|--------|-----|-----|-----|
| Front Left (FL)  | Pin 6  | Pin 22 | Pin 23 |
| Front Right (FR) | Pin 7  | Pin 24 | Pin 25 |

#### **Driver B - Ruote Posteriori**
| Motore | PWM | IN1 | IN2 |
|--------|-----|-----|-----|
| Back Left (BL)   | Pin 8  | Pin 26 | Pin 27 |
| Back Right (BR)  | Pin 9  | Pin 28 | Pin 29 |

### **Pin I2C (Sensori Distanza)**
- **SDA:** Pin 20 (I2C Data)
- **SCL:** Pin 21 (I2C Clock)
- **Dispositivi:**
  - VL53L0X Front (addr 0x30)
  - VL53L0X Right (addr 0x31)
  - VL53L0X Left (addr 0x32)
  - MPU6050 (addr 0x68)

### **Pin Sensore Colore TCS3200**
| Funzione | Pin Arduino |
|----------|-------------|
| S0       | Pin 30      |
| S1       | Pin 31      |
| S2       | Pin 32      |
| S3       | Pin 33      |
| OUT      | Pin 34      |
| VCC      | 5V sensori  |
| GND      | GND comune  |

### **Pin Servo**
| Servo | Pin Arduino |
|-------|-------------|
| Camera   | Pin 10 (PWM) |
| Medikit  | Pin 11 (PWM) |

**Alimentazione Servo:**
- **VCC:** 5V sensori
- **GND:** GND comune

### **Pin LED Status**
- **LED:** Pin 13 (built-in)

### **Pin UART Raspberry Pi**
- **TX1:** Pin 18 → RX Raspberry
- **RX1:** Pin 19 → TX Raspberry
- **Baud Rate:** 115200

---

## 🔴 **DRIVER MOTORI L298N (x2)**

### **Driver A - Ruote Anteriori**

#### **Alimentazione**
- **+12V:** 12V da regolatore DC-DC
- **GND:** GND comune
- **+5V:** NON collegato (usa 12V)

#### **Segnali Controllo**
| Segnale | Pin Arduino | Funzione |
|---------|-------------|----------|
| ENA     | Pin 6       | PWM velocità FL+FR |
| IN1     | Pin 22      | Direzione FL |
| IN2     | Pin 23      | Direzione FL |
| IN3     | Pin 24      | Direzione FR |
| IN4     | Pin 25      | Direzione FR |
| ENB     | Pin 7       | PWM velocità FR (collegare jumper se non usato) |

#### **Uscite Motori**
- **OUT1, OUT2:** Motore FL (Front Left)
- **OUT3, OUT4:** Motore FR (Front Right)

### **Driver B - Ruote Posteriori**

#### **Alimentazione**
- **+12V:** 12V da regolatore DC-DC
- **GND:** GND comune
- **+5V:** NON collegato

#### **Segnali Controllo**
| Segnale | Pin Arduino | Funzione |
|---------|-------------|----------|
| ENA     | Pin 8       | PWM velocità BL+BR |
| IN1     | Pin 26      | Direzione BL |
| IN2     | Pin 27      | Direzione BL |
| IN3     | Pin 28      | Direzione BR |
| IN4     | Pin 29      | Direzione BR |
| ENB     | Pin 9       | PWM velocità BR |

#### **Uscite Motori**
- **OUT1, OUT2:** Motore BL (Back Left)
- **OUT3, OUT4:** Motore BR (Back Right)

---

## 📏 **SENSORI DISTANZA VL53L0X (x3)**

### **Configurazione I2C**
Tutti i sensori condividono bus I2C, ma con indirizzi univoci:

| Sensore | Indirizzo I2C | Posizione |
|---------|---------------|-----------|
| Front   | 0x30          | Anteriore centrale |
| Right   | 0x31          | Laterale destra |
| Left    | 0x32          | Laterale sinistra |

### **Connessioni per OGNI sensore**
| Pin VL53L0X | Collegamento |
|-------------|--------------|
| VCC         | 5V sensori   |
| GND         | GND comune   |
| SDA         | Pin 20 Arduino (I2C Data) |
| SCL         | Pin 21 Arduino (I2C Clock) |
| XSHUT       | (opzionale per cambio indirizzo) |

**⚙️ Programmazione Indirizzo:**
1. Collega XSHUT di Front e Right a GND (disabilitati)
2. Assegna 0x32 a Left via software
3. Collega XSHUT Front a VCC, assegna 0x30
4. Collega XSHUT Right a VCC, assegna 0x31

---

## 🎨 **SENSORE COLORE TCS3200**

### **Connessioni**
| Pin TCS3200 | Pin Arduino | Funzione |
|-------------|-------------|----------|
| VCC         | 5V sensori  | Alimentazione |
| GND         | GND comune  | Ground |
| S0          | Pin 30      | Frequency scaling (HIGH) |
| S1          | Pin 31      | Frequency scaling (LOW) |
| S2          | Pin 32      | Color filter select |
| S3          | Pin 33      | Color filter select |
| OUT         | Pin 34      | Frequency output |
| OE          | GND         | Output enable (sempre attivo) |

**📍 Posizionamento:** Sotto il robot, centrato, distanza 5-10mm dal pavimento

---

## 📐 **GIROSCOPIO MPU6050**

### **Connessioni**
| Pin MPU6050 | Collegamento |
|-------------|--------------|
| VCC         | 5V sensori   |
| GND         | GND comune   |
| SDA         | Pin 20 Arduino (I2C Data) |
| SCL         | Pin 21 Arduino (I2C Clock) |
| INT         | (non usato)  |

**Indirizzo I2C:** 0x68 (default)

---

## 📹 **RASPBERRY PI 4 MODEL B**

### **Alimentazione**
- **5V:** Da regolatore DC-DC 5V 3A
- **GND:** GND comune

### **Camera Raspberry Pi Camera Module V2**
- **Connessione:** Porta CSI-2 (15-pin ribbon cable)
- **Risoluzione:** 8MP, 1080p30

### **UART Comunicazione Arduino**
| Pin Raspberry GPIO | Pin Arduino | Funzione |
|-------------------|-------------|----------|
| GPIO 14 (TXD)     | Pin 19 (RX1) | Trasmissione → Arduino |
| GPIO 15 (RXD)     | Pin 18 (TX1) | Ricezione ← Arduino |
| GND               | GND comune   | Ground comune |

**⚠️ NOTA:** Livelli logici 3.3V (Raspberry) compatibili con 5V (Arduino). NO level shifter necessario.

---

## 🔧 **SERVO MOTORI (x2)**

### **Servo Camera (Pan)**
| Pin Servo | Collegamento |
|-----------|--------------|
| Segnale (Arancio) | Pin 10 Arduino (PWM) |
| VCC (Rosso)       | 5V sensori |
| GND (Marrone)     | GND comune |

**Range:** 0-180° (0=sinistra, 90=centro, 180=destra)

### **Servo Medikit Dispenser**
| Pin Servo | Collegamento |
|-----------|--------------|
| Segnale (Arancio) | Pin 11 Arduino (PWM) |
| VCC (Rosso)       | 5V sensori |
| GND (Marrone)     | GND comune |

**Range:** 0-90° (0=chiuso, 90=aperto)

---

## ⚡ **SCHEMA BLOCCHI ALIMENTAZIONE**

```
Batterie 22.2V (LiPo 3S x2)
    │
    ├──→ Buck 22V→12V 3A ──→ Arduino MEGA Vin (12V)
    │                    └──→ Driver A VCC (12V)
    │                    └──→ Driver B VCC (12V)
    │
    ├──→ Buck 22V→5V 3A ──→ Raspberry Pi GPIO 5V
    │
    └──→ Buck 22V→5V 2A ──→ VL53L0X x3 (5V)
                        └──→ TCS3200 (5V)
                        └──→ MPU6050 (5V)
                        └──→ Servo Camera (5V)
                        └──→ Servo Medikit (5V)

GND COMUNE: batteria, regolatori, Arduino, Raspberry, driver, sensori, servo
```

**⚠️ ALTERNATIVA CON BATTERIE 12V:**
```
Batterie 12V (Piombo)
    │
    ├──→ Arduino MEGA Vin (12V diretto, NO regolatore)
    │
    ├──→ Driver A VCC (12V diretto)
    │
    ├──→ Driver B VCC (12V diretto)
    │
    ├──→ Buck 12V→5V 3A ──→ Raspberry Pi GPIO 5V
    │
    └──→ Buck 12V→5V 2A ──→ Sensori + Servo (5V)
```

---

## 📋 **CHECKLIST CONNESSIONI**

### **Prima di accendere:**
- [ ] Batterie cariche (>11V per cella LiPo)
- [ ] Tutti i GND comuni collegati
- [ ] Nessun corto circuito visibile
- [ ] Regolatori DC-DC settati su tensioni corrette (multimetro)
- [ ] Polarità corretta su tutti i connettori

### **Test Step-by-Step:**
1. Collega batterie → verifica tensioni con multimetro
2. Accendi Arduino → verifica LED alimentazione
3. Accendi Raspberry Pi → verifica boot (LED attività)
4. Carica sketch Arduino → verifica Serial Monitor
5. Test motori singoli (funzione `testSingleMotors()`)
6. Test sensori I2C (verifica indirizzi `Wire.scan()`)
7. Test sensore colore (verifica valori RGB su Serial)
8. Test servo (verifica angoli 0-180°)
9. Test comunicazione UART Arduino↔Raspberry

---

## 🔥 **SICUREZZA**

### **Protezioni**
- **Interruttore generale** sulla batteria
- **Fusibile 30A** sul + batteria
- **Diodi flyback** sui motori (protezione reverse current)
- **Condensatori ceramici 100nF** vicino a ogni IC

### **Dissipazione Termica**
- **Driver L298N:** dissipatori obbligatori (si scaldano molto!)
- **Regolatori DC-DC:** ventilazione passiva/attiva se >2A

### **Ground Loop Prevention**
- **GND comune unico:** stella (tutte le masse a un punto centrale)
- **NO loop di massa:** evita cavi GND paralleli lunghi

---

## 📦 **LISTA COMPONENTI**

| Componente | Quantità | Spec |
|------------|----------|------|
| Arduino MEGA 2560 | 1 | |
| Raspberry Pi 4 Model B | 1 | 2GB+ RAM |
| Raspberry Pi Camera V2 | 1 | 8MP |
| Driver L298N | 2 | Dual H-Bridge |
| Motore DC | 4 | 12V, ruote Mecanum |
| VL53L0X | 3 | Sensore distanza laser |
| TCS3200 | 1 | Sensore colore RGB |
| MPU6050 | 1 | Giroscopio + accelerometro |
| Servo SG90 | 2 | 0-180° |
| Batteria LiPo 3S | 2 | 11.1V, 2200mAh+ |
| Buck Converter | 3 | 22V→12V, 22V→5V x2 |
| Cavi Dupont | ~50 | M-M, M-F |
| Breadboard / PCB | 1 | Per sensori |

---

**🏆 FRANCO V6.0 - SCHEMA COMPLETO PER ROBOCUP 2026**

**Autore:** Team Franco  
**Data:** Marzo 2026  
**Versione:** 6.0 (con note divisore tensione batterie 12V)
