# Progetto RoboCup Junior Rescue Maze 2026

**Architettura:** Ibrida (Arduino Mega 2560 + Raspberry Pi 4B)
**Versione Documento:** 1.1

---

## DISTINTA COMPONENTI (BOM - Bill of Materials)

| Componente | Modello | Qtà | Descrizione Breve | Finalità |
| :--- | :--- | :---: | :--- | :--- |
| **Microcontrollore** | Arduino Mega 2560 | 1 | Scheda logica con numerosi I/O | Gestione real-time di sensori, motori, servo e logica di navigazione. |
| **SBC (Single Board Computer)** | Raspberry Pi 4B | 1 | Mini computer ad alte prestazioni | Elaborazione della visione artificiale (OpenCV) per il riconoscimento vittime/bersagli. |
| **Telecamera** | Innomaker U20CAM-1080P-1 | 2 | Videocamera USB 1080p | Acquisizione immagini dalle fiancate del robot per rilevare le lettere (Φ, Ψ, Ω) e i cognitive targets. |
| **Sensore ToF** | VL53L0X | 8 | Sensore di distanza laser | Mappatura dei muri laterali, frontali e posteriori. Utilizzati anche per stimare la distanza percorsa (odometria). |
| **Motore DC** | "TT Motor" (Giallo) | 4 | Motore a corrente continua con riduttore | Trazione e movimento del robot (configurazione skid-steer). |
| **Driver Motori** | TB6612FNG | 2 | Doppio ponte H a MOSFET | Comando di potenza e direzione dei motori DC (uno per la fiancata DX, uno per la SX). |
| **Sensore di Colore** | TCS3200 | 1 | Matrice di fotodiodi con filtri RGB | Riconoscimento del colore del pavimento (es. tessere nere, argento, checkpoint). |
| **Servomotore** | SG90 | 1 | Micro servo da 9g | Attuatore per il meccanismo fisico di rilascio dei cubetti medikit. |
| **Batteria** | LiPo/Li-ion 12V 4A | 1 | Pacco batterie ricaricabile | Alimentazione generale e primaria di tutto il sistema. |
| **Interruttore** | Interruttore a levetta 0-1 | 1 | Interruttore bipolare o unipolare | Accensione e spegnimento generale (stacco fisico della batteria). |
| **Convertitore Step-Down** | XLSEMI XL4015E1 | 2 | Regolatore di tensione DC-DC (max 5A) | Riduzione della tensione da 12V a 5.1V (per Raspberry) e a 6.0V (per Motori e Servo). |
| **Giroscopio (IMU)** | MPU6050 | 1 | Accelerometro e giroscopio a 6 assi | Mantenimento della traiettoria rettilinea e rotazioni precise di 90° o 180°. |
| **Display LCD** | LCD 20x4 con modulo I2C | 1 | Schermo a cristalli liquidi a 4 righe | Visualizzazione dati di debug, stato sensori e stato della gara. |
| **Ventola di Raffreddamento** | Ventolina 5V per Raspberry | 1 | Piccola ventola DC | Dissipazione del calore della CPU del Raspberry Pi durante l'elaborazione video. |
| **LED di Stato** | LED standard (es. 5mm) | 1 | Diodo a emissione luminosa | Segnalazione visiva per gli arbitri dell'avvenuto riconoscimento di una vittima (obbligo da regolamento). |
| **Pulsante** | Pulsante tattile N.A. | 2 | Interruttore a pressione | Utilizzati rispettivamente per lo START della run e per il RESET della logica di gara. |

---

# Schema dei Collegamenti

**Componenti Principali:** Arduino Mega 2560, Raspberry Pi 4B
**Versione:** 1.0

---

## ⚠️ REGOLA FONDAMENTALE: IL GND COMUNE
Tutti i pin GND (Massa / Negativo) di TUTTI i componenti (Arduino, Step-Down, Driver Motori, Sensori) **devono essere collegati insieme**. Se le masse non sono in comune, i segnali elettrici non avranno un riferimento e il robot non funzionerà.

---

## 1. SISTEMA DI ALIMENTAZIONE E POTENZA

L'energia parte dalla batteria e viene distribuita tramite un interruttore generale a due convertitori Step-Down.


* **Batteria LiPo/Li-ion 12V (4A):**
    * **Positivo (+):** Collegato a un polo dell'**Interruttore 0-1**.
    * **Negativo (- / GND):** Collegato alla linea del **GND Comune**.
* **Interruttore Generale (0-1):**
    * *Polo Ingresso:* Dal Positivo (+) della Batteria 12V.
    * *Polo Uscita:* Si sdoppia e va al pin **IN+** di *entrambi* gli Step-Down.
* **Step-Down 1 (Logica Raspberry) - XL4015E1:**
    * **IN+:** Dall'uscita dell'Interruttore Generale.
    * **IN-:** Al GND Comune.
    * **OUT+ (Regolato a 5.1V col multimetro!):** Alimenta il Raspberry Pi 4B (consigliato via cavo USB-C tagliato/saldato o pin 5V GPIO).
    * **OUT-:** Al GND Comune.
* **Step-Down 2 (Potenza Motori e Servo) - XL4015E1:**
    * **IN+:** Dall'uscita dell'Interruttore Generale.
    * **IN-:** Al GND Comune.
    * **OUT+ (Regolato a 6.0V col multimetro!):** Va alla linea di alimentazione di potenza (collegato al pin `VMOT` dei due driver motori e al filo Rosso del Servo SG90).
    * **OUT-:** Al GND Comune.

---

## 2. COMUNICAZIONE PRINCIPALE E RAFFREDDAMENTO

* **Raspberry Pi 4B <-> Arduino Mega 2560:**
    * Cavo dati USB standard (es. USB-A da Raspberry a USB-B per Arduino Mega).
    * *Nota:* Questo cavo fornirà anche i 5V per alimentare la logica della scheda Arduino.
* **Telecamere (x2 Innomaker U20CAM-1080P-1):**
    * Collegate direttamente alle porte USB 3.0 (quelle blu) del Raspberry Pi.
* **Ventolina di Raffreddamento Raspberry:**
    * **Filo Rosso:** Pin 5V del Raspberry (Pin 2 o 4 della GPIO).
    * **Filo Nero:** GND del Raspberry (Pin 6 della GPIO).

---

## 3. ARDUINO MEGA: BUS I2C (Logica 5V)
*Tutti questi moduli condividono gli stessi pin I2C. Prenderanno l'alimentazione dai pin 5V e GND dell'Arduino.*

* **Giroscopio MPU6050:** VCC -> 5V Arduino | GND -> GND Comune | SDA -> **Pin 20** | SCL -> **Pin 21**
* **Display LCD 20x4 (Modulo I2C):** VCC -> 5V Arduino | GND -> GND Comune | SDA -> **Pin 20** | SCL -> **Pin 21**
* **Sensori ToF VL53L0X (Tutti e 8):** VCC -> 5V Arduino | GND -> GND Comune | SDA -> **Pin 20** | SCL -> **Pin 21**

---

## 4. ARDUINO MEGA: INDIRIZZAMENTO SENSORI ToF (XSHUT)
*Per evitare conflitti sull'I2C, ogni sensore ha il pin XSHUT collegato a un pin digitale separato.*

* **Sensore ToF 1:** XSHUT -> **Pin 30**
* **Sensore ToF 2:** XSHUT -> **Pin 31**
* **Sensore ToF 3:** XSHUT -> **Pin 32**
* **Sensore ToF 4:** XSHUT -> **Pin 33**
* **Sensore ToF 5:** XSHUT -> **Pin 34**
* **Sensore ToF 6:** XSHUT -> **Pin 35**
* **Sensore ToF 7:** XSHUT -> **Pin 36**
* **Sensore ToF 8:** XSHUT -> **Pin 37**

---

## 5. ARDUINO MEGA: DRIVER MOTORI TB6612FNG


**Alimentazione e Logica Comune (per entrambi i driver):**
* **VMOT:** Uscita +6V dello Step-Down 2 (NON ALIMENTARE A 12V!)
* **VCC:** 5V da Arduino
* **GND:** GND Comune
* **STBY:** 5V da Arduino (Sempre ALTO per abilitare il driver)

**Driver 1 (Controllo Fiancata Destra - Motori DX):**
* `PWMA` e `PWMB` uniti insieme -> **Pin 2** (PWM)
* `AIN1` e `BIN1` uniti insieme -> **Pin 22** (Digitale)
* `AIN2` e `BIN2` uniti insieme -> **Pin 23** (Digitale)
* `AO1/AO2` -> Motore Anteriore Destro
* `BO1/BO2` -> Motore Posteriore Destro

**Driver 2 (Controllo Fiancata Sinistra - Motori SX):**
* `PWMA` e `PWMB` uniti insieme -> **Pin 3** (PWM)
* `AIN1` e `BIN1` uniti insieme -> **Pin 24** (Digitale)
* `AIN2` e `BIN2` uniti insieme -> **Pin 25** (Digitale)
* `AO1/AO2` -> Motore Anteriore Sinistro
* `BO1/BO2` -> Motore Posteriore Sinistro

---

## 6. ARDUINO MEGA: SENSORE DI COLORE TCS3200

* **VCC:** 5V da Arduino
* **GND:** GND Comune
* **S0:** -> **Pin 38**
* **S1:** -> **Pin 39**
* **S2:** -> **Pin 40**
* **S3:** -> **Pin 41**
* **OUT:** -> **Pin 42**

---

## 7. ARDUINO MEGA: SERVOMOTORE, PULSANTI E LED

* **Servomotore SG90 (Rilascio Medikit):**
    * Filo Rosso (VCC) -> Uscita +6.0V dello Step-Down 2
    * Filo Marrone/Nero (GND) -> GND Comune
    * Filo Arancione/Giallo (Segnale) -> **Pin 4** (PWM)
* **Pulsante START:**
    * Polo 1 -> GND Comune
    * Polo 2 -> **Pin 43** (Usare `INPUT_PULLUP` nel codice)
* **Pulsante RESET:**
    * Polo 1 -> GND Comune
    * Polo 2 -> **Pin 44** (Usare `INPUT_PULLUP` nel codice)
* **LED di Stato (Segnalazione Vittima):**
    * Catodo (Gamba corta) -> GND Comune
    * Anodo (Gamba lunga) -> Resistenza da 220/330 ohm -> **Pin 45**