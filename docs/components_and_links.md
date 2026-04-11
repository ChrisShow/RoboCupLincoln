# Progetto RoboCup Junior Rescue Maze 2026 - Master Wiring Guide

**Architettura:** Ibrida (Arduino Mega 2560 + Raspberry Pi 4B)
**Versione Documento:** 1.4 (Livello di Dettaglio: Assoluto)

---

## SPECIFICHE DIMENSIONALI E MECCANICHE

- **Dimensioni Scocca Nuda:** 17 cm (lunghezza) x 10 cm (larghezza).
- **Ingombro Totale (incluse ruote):** 17 cm (lunghezza) x **13 cm** (larghezza).
- **Posizionamento ToF Laterali:** Montati a filo scocca (10 cm), i sensori distano 5 cm dal baricentro centrale. Le ruote sporgono di ulteriori 1.5 cm per lato rispetto al piano dei sensori.

---

## DISTINTA COMPONENTI (BOM - Bill of Materials)

| Componente           | Modello                | Qtà | Descrizione Breve                 | Finalità                                                        |
| :------------------- | :--------------------- | :-: | :-------------------------------- | :-------------------------------------------------------------- |
| **Microcontrollore** | Arduino Mega 2560      |  1  | Scheda logica con numerosi I/O    | Gestione real-time di sensori, motori, servo e logica.          |
| **SBC** | Raspberry Pi 4B        |  1  | Mini computer ad alte prestazioni | Elaborazione della visione artificiale (OpenCV).                |
| **Telecamera** | Innomaker U20CAM       |  2  | Videocamera USB 1080p             | Acquisizione immagini dalle fiancate per rilevare vittime.      |
| **Sensore ToF** | VL53L0X                |  6  | Sensore di distanza laser         | Mappatura dei muri e odometria (1 Fronte, 1 Retro, 2 DX, 2 SX). |
| **Motore DC** | "TT Motor" (Giallo)    |  4  | Motore a corrente continua        | Trazione e movimento del robot (skid-steer).                    |
| **Driver Motori** | TB6612FNG              |  2  | Doppio ponte H a MOSFET           | Comando di potenza e direzione dei motori DC.                   |
| **Sensore Colore** | TCS3200                |  1  | Matrice di fotodiodi RGB          | Riconoscimento del colore del pavimento (es. tessere nere).     |
| **Servomotore** | SG90                   |  1  | Micro servo da 9g                 | Attuatore per il meccanismo fisico di rilascio medikit.         |
| **Batteria** | LiPo/Li-ion 12V 4A     |  1  | Pacco batterie ricaricabile       | Alimentazione generale e primaria di tutto il sistema.          |
| **Interruttore** | Interruttore a levetta |  1  | Interruttore bipolare o unipolare | Accensione e spegnimento generale.                              |
| **Step-Down** | XLSEMI XL4015E1        |  2  | Regolatore di tensione DC-DC      | Riduzione tensione a 5.1V (Raspberry) e 6.0V (Motori/Servo).    |
| **Giroscopio** | MPU6050                |  1  | Accelerometro e giroscopio        | Mantenimento traiettoria rettilinea e rotazioni a 90°/180°.     |
| **Display LCD** | LCD 16x2 con I2C       |  1  | Schermo a cristalli liquidi       | Visualizzazione dati di debug e stato della gara.               |
| **Ventola** | Ventolina 5V           |  1  | Piccola ventola DC                | Dissipazione del calore della CPU del Raspberry Pi.              |
| **Pulsante** | Pulsante tattile N.A.  |  1  | Interruttore a pressione          | Gestione START (pressione breve) e RESET (pressione lunga).     |
| **Maniglia** | Stampa 3D / Plastica   |  1  | Maniglia fissa superiore          | Permette di sollevare il robot (regolamento 4.2.7).             |

---

## LIBRERIE ARDUINO RICHIESTE

- **`Wire.h`** (Preinstallata): Gestione nativa del bus I2C.
- **`VL53L0X` di Pololu:** Estremamente leggera e perfetta per gestire indirizzi multipli e l'uso del pin XSHUT.
- **`MPU6050_light` di rfetick:** Calcola autonomamente gli angoli di Eulero (Yaw, Pitch, Roll) e gestisce internamente la calibrazione del drift.
- **`LiquidCrystal_I2C` di Frank de Brabander:** Lo standard per pilotare i display 16x2 dotati di "zainetto" PCF8574 sul retro.
- **`Servo.h`** (Preinstallata): Gestione del segnale PWM per il servo SG90.
- **Nessuna libreria esterna** è strettamente necessaria per i driver motore TB6612FNG e per il sensore TCS3200.

---

# SCHEMA CABLAGGIO PIN-TO-PIN

## ⚠️ NODO CRITICO: IL "GND COMUNE"
Tutti i riferimenti a "GND COMUNE" in questo documento implicano che i pin indicati devono essere fisicamente collegati tra loro (solitamente tramite una basetta millefori, una breadboard o un cavo sdoppiato e saldato). 

---

## 1. ALIMENTAZIONE E DISTRIBUZIONE ENERGIA

| Componente | Pin Fisico | Destinazione / Collegamento | Note |
| :--- | :--- | :--- | :--- |
| **Batteria 12V** | Polo Positivo (Rosso) | Ingresso Interruttore 0-1 | Fonte primaria. |
| **Batteria 12V** | Polo Negativo (Nero) | **GND COMUNE** | Chiude il circuito. |
| **Interruttore** | Ingresso | Polo Positivo Batteria | - |
| **Interruttore** | Uscita | Pin `IN+` di *entrambi* gli Step-Down | Distribuisce i 12V. |
| **Step-Down 1 (5.1V)** | `IN+` | Uscita Interruttore | - |
| **Step-Down 1 (5.1V)** | `IN-` | **GND COMUNE** | - |
| **Step-Down 1 (5.1V)** | `OUT+` (Regolato a 5.1V) | Raspberry Pi 4B (Pin 2 / 4 della GPIO o via cavo USB-C) | Alimenta il Pi. |
| **Step-Down 1 (5.1V)** | `OUT-` | **GND COMUNE** | - |
| **Step-Down 2 (6.0V)** | `IN+` | Uscita Interruttore | - |
| **Step-Down 2 (6.0V)** | `IN-` | **GND COMUNE** | - |
| **Step-Down 2 (6.0V)** | `OUT+` (Regolato a 6.0V) | Linea di Potenza Motori (`VMOT` dei due TB6612FNG) + Cavo Rosso Servo SG90 | Linea di forza bruta. |
| **Step-Down 2 (6.0V)** | `OUT-` | **GND COMUNE** | - |

---

## 2. RASPBERRY PI 4B E PERIFERICHE USB

| Componente | Porta / Pin | Destinazione / Collegamento | Note |
| :--- | :--- | :--- | :--- |
| **Raspberry Pi 4B** | Pin GPIO 2 o 4 (5V) | Cavo Rosso Ventolina | Alimentazione ventola. |
| **Raspberry Pi 4B** | Pin GPIO 6 (GND) | Cavo Nero Ventolina | **GND COMUNE**. |
| **Raspberry Pi 4B** | Porta USB 3.0 (Blu) | Cavo USB Innomaker Cam 1 (Destra) | Dati e alimentazione cam. |
| **Raspberry Pi 4B** | Porta USB 3.0 (Blu) | Cavo USB Innomaker Cam 2 (Sinistra) | Dati e alimentazione cam. |
| **Raspberry Pi 4B** | Porta USB 2.0 (Nera) | Cavo USB-B collegato ad Arduino Mega | Comunicazione Seriale e alimentazione Arduino a 5V. |

---

## 3. COMPONENTI SUL BUS I2C (ARDUINO MEGA)
Tutti questi componenti lavorano a 5V e comunicano sui pin 20 e 21 di Arduino.

| Componente | Pin Fisico | Destinazione (Arduino Mega o Altro) | Note |
| :--- | :--- | :--- | :--- |
| **Giroscopio MPU6050** | `VCC` | Pin `5V` Arduino | |
| **Giroscopio MPU6050** | `GND` | **GND COMUNE** | |
| **Giroscopio MPU6050** | `SCL` | Pin `21` Arduino | Linea Clock I2C. |
| **Giroscopio MPU6050** | `SDA` | Pin `20` Arduino | Linea Dati I2C. |
| **Giroscopio MPU6050** | `XDA`, `XCL`, `INT` | *NON COLLEGATI* | Pin non necessari. |
| **Giroscopio MPU6050** | `AD0` | **GND COMUNE** | Fissa l'indirizzo I2C a 0x68. |

### 3.1 Sensori ToF VL53L0X (Tutti e 6 condividono VCC, GND, SCL, SDA)

| Pin sul ToF | Destinazione Globale | Note |
| :--- | :--- | :--- |
| `VIN` (o VCC) | Pin `5V` Arduino | Alimenta il regolatore interno a 2.8V. |
| `GND` | **GND COMUNE** | - |
| `SCL` | Pin `21` Arduino | Bus I2C. |
| `SDA` | Pin `20` Arduino | Bus I2C. |
| `GPIO1` | *NON COLLEGATO* | Pin di interrupt non usato in questo progetto. |

### 3.2 Indirizzamento XSHUT dei singoli ToF VL53L0X

| Componente | Pin `XSHUT` | Destinazione su Arduino Mega |
| :--- | :--- | :--- |
| **ToF 1 (Fronte Centrale)** | `XSHUT` | Pin Digitale **30** |
| **ToF 2 (Retro Centrale)** | `XSHUT` | Pin Digitale **31** |
| **ToF 3 (Destra Anteriore)** | `XSHUT` | Pin Digitale **32** |
| **ToF 4 (Destra Posteriore)**| `XSHUT` | Pin Digitale **33** |
| **ToF 5 (Sinistra Anteriore)**| `XSHUT` | Pin Digitale **34** |
| **ToF 6 (Sinistra Posteriore)**| `XSHUT` | Pin Digitale **35** |

---

## 4. DRIVER MOTORI TB6612FNG (Dettaglio Maniacale)

Questi driver richiedono sia alimentazione logica (5V) che di potenza (6V). Hanno tre pin GND che vanno uniti. I pin di comando A e B vengono ponticellati per comandare due motori in parallelo sullo stesso lato.

### 4.1 Driver 1: Controllo Fiancata Destra (Motori DX)

| Pin sul Driver DX | Destinazione / Collegamento | Note |
| :--- | :--- | :--- |
| `VMOT` | Uscita `OUT+` Step-Down 2 (6.0V) | **NON ALIMENTARE A 12V!** |
| `VCC` | Pin `5V` Arduino | Alimentazione logica chip. |
| `GND` (Tutti e 3 i pin) | **GND COMUNE** | Vanno saldati o connessi tutti a massa. |
| `STBY` | Pin `5V` Arduino | Tiene il driver sempre sveglio (ALTO). |
| `PWMA` e `PWMB` | Uniti insieme -> Pin PWM **2** Arduino | Regola la velocità di tutta la fiancata DX. |
| `AIN1` e `BIN1` | Uniti insieme -> Pin Digitale **22** Arduino | Senso di marcia 1. |
| `AIN2` e `BIN2` | Uniti insieme -> Pin Digitale **23** Arduino | Senso di marcia 2. |
| `AO1` e `AO2` | Poli del Motore Anteriore Destro | Se la ruota gira al contrario, inverti i cavi tra loro. |
| `BO1` e `BO2` | Poli del Motore Posteriore Destro | Se la ruota gira al contrario, inverti i cavi tra loro. |

### 4.2 Driver 2: Controllo Fiancata Sinistra (Motori SX)

| Pin sul Driver SX | Destinazione / Collegamento | Note |
| :--- | :--- | :--- |
| `VMOT` | Uscita `OUT+` Step-Down 2 (6.0V) | **NON ALIMENTARE A 12V!** |
| `VCC` | Pin `5V` Arduino | Alimentazione logica chip. |
| `GND` (Tutti e 3 i pin) | **GND COMUNE** | Vanno saldati o connessi tutti a massa. |
| `STBY` | Pin `5V` Arduino | Tiene il driver sempre sveglio (ALTO). |
| `PWMA` e `PWMB` | Uniti insieme -> Pin PWM **3** Arduino | Regola la velocità di tutta la fiancata SX. |
| `AIN1` e `BIN1` | Uniti insieme -> Pin Digitale **24** Arduino | Senso di marcia 1. |
| `AIN2` e `BIN2` | Uniti insieme -> Pin Digitale **25** Arduino | Senso di marcia 2. |
| `AO1` e `AO2` | Poli del Motore Anteriore Sinistro | Se la ruota gira al contrario, inverti i cavi tra loro. |
| `BO1` e `BO2` | Poli del Motore Posteriore Sinistro | Se la ruota gira al contrario, inverti i cavi tra loro. |

---

## 5. SENSORE DI COLORE TCS3200

Questo sensore ha pin specifici per l'abilitazione e il calcolo della frequenza.

| Pin sul TCS3200 | Destinazione su Arduino Mega / Altro | Note |
| :--- | :--- | :--- |
| `VCC` | Pin `5V` Arduino | Alimentazione. |
| `GND` | **GND COMUNE** | Massa. |
| `OE` (Output Enable) | **GND COMUNE** | Va forzato basso (LOW) per tenere attivo il sensore. Spesso omesso, causa malfunzionamenti se non collegato. |
| `S0` | Pin Digitale **38** | Scaling frequenza (solitamente accoppiato a HIGH). |
| `S1` | Pin Digitale **39** | Scaling frequenza (solitamente accoppiato a LOW). |
| `S2` | Pin Digitale **40** | Selezione filtro fotodiodi (Rosso/Verde/Blu/Clear). |
| `S3` | Pin Digitale **41** | Selezione filtro fotodiodi (Rosso/Verde/Blu/Clear). |
| `OUT` | Pin Digitale **42** | Trasmette l'onda quadra letta da `pulseIn()`. |

---

## 6. SERVOMOTORE E INPUT/OUTPUT GENERALI

| Componente | Pin Fisico | Destinazione / Collegamento | Note |
| :--- | :--- | :--- | :--- |
| **Servo SG90** | Filo Rosso (VCC) | Uscita `OUT+` Step-Down 2 (6.0V) | Dà la forza meccanica. Non attaccare al 5V di Arduino! |
| **Servo SG90** | Filo Marrone/Nero (GND)| **GND COMUNE** | Riferimento essenziale per il PWM. |
| **Servo SG90** | Filo Arancione/Giallo | Pin PWM **4** Arduino | Segnale di controllo angolare. |
| **Pulsante START** | Terminale 1 | **GND COMUNE** | Nessuna resistenza necessaria (usa `INPUT_PULLUP` nel codice). |
| **Pulsante START** | Terminale 2 | Pin Digitale **43** Arduino | |
| **LED di Stato** | Anodo (Gamba lunga) | Ad una resistenza (es. 220/330 ohm), l'altro capo al Pin **45** Arduino | |
| **LED di Stato** | Catodo (Gamba corta) | **GND COMUNE** | - |