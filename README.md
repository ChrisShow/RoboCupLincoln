# 🤖 RoboCup Rescue Maze 2026 - Documentazione Completa

## 

## 📋 Indice

1. [Introduzione](#introduzione)
2. [Funzionamento Generale](#funzionamento-generale)
3. [Componenti Hardware](#componenti-hardware)
4. [Architettura Software](#architettura-software)
5. [Versione V1 - Base](#versione-v1---base)
6. [Versione V2 - Avanzata](#versione-v2---avanzata)
7. [Versione V3 - C](#versione-v2---avanzata)ognitive Targets
8. [Installazione e Configurazione](#installazione-e-configurazione)



---



## 🎯 Introduzione

Questo progetto implementa un robot autonomo per la competizione **RoboCup Rescue Maze 2026**. Il robot è capace di:

* ✅ **Esplorare autonomamente** un labirinto sconosciuto (30x30 cm per cella)
* ✅ **Rilevare vittime** tramite marker colorati e lettere greche (Φ, Ψ, Ω)
* ✅ **Consegnare medikit** alle vittime che ne hanno bisogno (Φ Harmed, Ψ Secured)
* ✅ **Evitare ostacoli** come celle nere
* ✅ **Mappare l'ambiente** utilizzando algoritmo BFS (Breadth-First Search)
* ✅ **Calibrarsi automaticamente** rispetto ai muri laterali



---



## 🧠 Funzionamento Generale

### Architettura del Sistema



Il robot utilizza un'**architettura distribuita a due livelli**:


┌─────────────────────────────────────────────────────────┐
│                   RASPBERRY PI 4B                        │
│  ┌───────────────────────────────────────────────────┐  │
│  │  • Elaborazione immagini (OpenCV)                 │  │
│  │  • Riconoscimento colori (HSV)                    │  │
│  │  │  • Riconoscimento lettere greche (OCR)         │  │
│  │  • Algoritmo navigazione (BFS)                    │  │
│  │  • Gestione memoria labirinto                     │  │
│  │  • Decisioni strategiche                          │  │
│  └───────────────────────────────────────────────────┘  │
└──────────────────────┬──────────────────────────────────┘
                       │ Seriale USB (115200 baud)
┌──────────────────────┴──────────────────────────────────┐
│                   ARDUINO MEGA 2560                     │
│  ┌───────────────────────────────────────────────────┐  │
│  │  • Controllo motori (movimento preciso)           │  │
│  │  • Lettura sensori (8x VL53L0X, 3x ultrasuoni)   │  │
│  │  • Calibrazione automatica (allineamento muri)    │  │
│  │  • Rilevamento ostacoli (nero, rosso, pendenze)   │  │
│  │  • Controllo servo (camera, medikit)              │  │
│  │  • Sicurezza movimento (giroscopio MPU6050)       │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘


Ciclo Operativo
┌──────────────────┐
│  INIZIO CICLO    │
└────────┬─────────┘
         │
         ▼
┌──────────────────────────────┐
│ 1. AUTOCALIBRAZIONE          │
│    Allinea robot ai muri     │
│    laterali con VL53L0X      │
└────────┬─────────────────────┘
         │
         ▼
┌──────────────────────────────┐
│ 2. SCANSIONE AMBIENTE        │
│    • Ruota camera frontale   │
│    • Rileva muri (ultrasuoni)│
│    • Se muro: cattura foto   │
│    • Analizza colori/lettere │
│    • Ruota camera destra     │
│    • Ripeti                  │
│    • Ruota camera sinistra   │
│    • Ripeti                  │
└────────┬─────────────────────┘
         │
         ▼
┌──────────────────────────────┐
│ 3. AGGIORNAMENTO MAPPA       │
│    • Aggiungi celle visitabili│
│    • Segna celle con muri    │
│    • Aggiorna memoria        │
└────────┬─────────────────────┘
         │
         ▼
┌──────────────────────────────┐
│ 4. DECISIONE DIREZIONE       │
│    • Algoritmo BFS           │
│    • Scelta cella successiva │
│    • Calcola rotazione       │
└────────┬─────────────────────┘
         │
         ▼
┌──────────────────────────────┐
│ 5. MOVIMENTO                 │
│    • Ruota (se necessario)   │
│    • Avanza a step (30mm)    │
│    • Controlla celle nere    │
│    • Controlla pendenza      │
│    • Se problema: TORNA      │
└────────┬─────────────────────┘
         │
         ▼
┌──────────────────────────────┐
│ 6. GESTIONE VITTIME          │
│    • Se Harmed: consegna     │
│    •    2 medikit            │
│    • Se Secured: consegna    │
│    •    1 medikit            │
│    • Se Unharmed: nulla      │
└────────┬─────────────────────┘
         │
         ▼
      RIPETI


---



## 🔧 Componenti Hardware



### Unità di Controllo



|Componente|Modello|Quantità|Scopo|
|-|-|-|-|
|**Microcontrollore**|Arduino Mega 2560|1|Controllo real-time sensori/motori|
|**Computer di Bordo**|Raspberry Pi 4 (4GB)|1|Elaborazione visiva e navigazione|
|**Display**|LCD I2C 20x4|1|Visualizzazione stato sistema|

### 

### Sensori di Distanza

|Componente|Modello|Quantità|Posizione|Scopo|
|-|-|-|-|-|
|**ToF Laser**|VL53L0X|8|2 per lato (FL, FR, BR, BL)|Calibrazione precisa muri (±1mm)|
|**Ultrasuoni**|HC-SR04|3|Frontale, Destra, Sinistra|Rilevamento muri a distanza|



**Dettaglio VL53L0X:**

* **Front-Left 1/2**: Allineamento muro frontale sinistro
* **Front-Right 1/2**: Allineamento muro frontale destro
* **Back-Right 1/2**: Allineamento muro posteriore destro
* **Back-Left 1/2**: Allineamento muro posteriore sinistro



### Sensori Ambientali

|Componente|Modello|Quantità|Scopo|
|-|-|-|-|
|**Giroscopio/Accelerometro**|MPU6050|1|Rilevamento pendenze/rampe (±17°)|
|**Sensore Colore**|TCS3200|1|Rilevamento celle nere/rosse|
|**Camera USB**|Logitech C270 / compatibile|1|Cattura immagini muri|

### 

### Attuatori

|Componente|Modello|Quantità|Scopo|
|-|-|-|-|
|**Motori DC**|Con encoder|2|Movimento differenziale|
|**Driver Motori**|L298N|2|Controllo potenza motori|
|**Servo Camera**|SG90 / MG996R|1|Rotazione camera (0°/90°/180°)|
|**Servo Medikit**|SG90|1|Meccanismo consegna medikit|

### 

### Interfaccia Utente

|Componente|Tipo|Quantità|Scopo|
|-|-|-|-|
|**Pulsante START**|Pulsante momentaneo|1|Avvio/Stop sistema|
|**LED Stato**|LED|1|Indicatore sistema attivo|

### 

### Alimentazione

|Componente|Specifiche|Quantità|Alimenta|
|-|-|-|-|
|**Batteria LiPo**|11.1V 2200mAh 3S|1|Motori + Arduino|
|**Power Bank USB**|5V 10000mAh|1|Raspberry Pi 4|
|**Regolatore Tensione**|Step-down 5V|1|Sensori + Servo|



---



## 💾 Mappatura Pin Arduino Mega

### 

### Digital I/O

|Pin|Componente|Funzione|
|-|-|-|
|**2**|Pulsante START|INPUT\_PULLUP|
|**3**|Motor Left ENA|PWM velocità|
|**4**|Motor Right ENB|PWM velocità|
|**9**|Servo Camera|PWM posizione|
|**10**|Servo Medikit|PWM posizione|
|**13**|LED Stato|OUTPUT|
|**20**|I2C SDA|Bus dati|
|**21**|I2C SCL|Bus clock|
|**24**|Motor Left IN1|Direzione|
|**25**|Motor Left IN2|Direzione|
|**26**|Motor Right IN3|Direzione|
|**27**|Motor Right IN4|Direzione|
|**30-35**|Ultrasuoni|TRIG/ECHO (3 sensori)|
|**36-40**|TCS3200|S0/S1/S2/S3/OUT|
|**42-49**|VL53L0X XSHUT|Reset sensori ToF|

### 

### Bus I2C (indirizzo)

|Dispositivo|Indirizzo|Funzione|
|-|-|-|
|VL53L0X #0-7|0x30-0x37|Sensori distanza laser|
|MPU6050|0x68|Giroscopio/accelerometro|
|LCD I2C|0x27 o 0x3F|Display 20x4|



---



## 📦 Architettura Software

### 

### File e Struttura



```


robocup\_2026/
│
├── arduino/
│   ├── arduino\_v1.ino          # Versione base
│   └── arduino\_v2.ino          # Con watchdog/heartbeat
│
├── raspberry/
│   ├── main\_v1.py              # Versione base
│   ├── main\_v2.py              # Con watchdog/heartbeat
│   ├── test\_setup.py           # Test installazione librerie
│   └── test\_arduino\_comm.py    # Test comunicazione seriale
│
├── docs/
│   ├── README.md               # Questo file
│   ├── PIN\_MAPPING.md          # Dettaglio mappatura pin
│   └── WIRING\_DIAGRAM.png      # Schema collegamenti
│
└── config/
    └── requirements.txt        # Dipendenze Python
```



---



## 🔵 Versione V1 - Base

### 

### Caratteristiche

La **versione V1** implementa le funzionalità core del robot con un'architettura semplificata.

#### 

#### Arduino V1

**Focus**: Controllo sensori/motori affidabile

**Caratteristiche principali:**

* ✅ Lettura sensori VL53L0X (8 sensori ToF)
* ✅ Lettura ultrasuoni HC-SR04 (3 sensori)
* ✅ Controllo motori differenziale (L298N)
* ✅ Autocalibrazione muri (allineamento laterale)
* ✅ Rilevamento ostacoli (nero, rosso, pendenza)
* ✅ Controllo servo (camera e medikit)
* ✅ Comunicazione seriale bidirezionale
* ✅ State machine per ciclo operativo
* ✅ Display LCD con feedback stato

**Flusso operativo:**

```
CALIBRATION → SCAN\_FRONT → SCAN\_RIGHT → SCAN\_LEFT 
    ↓
WAIT\_DIRECTION → MOVE → WAIT\_CONFIRM → CALIBRATION (ripeti)
```

**Comunicazione Arduino → Raspberry:**

* Invia presenza muro (1 = muro, 0 = libero)
* Invia esito movimento (1 = successo, 0 = fallito)
* Invia conferma fine scansione (1)

**Comunicazione Raspberry → Arduino:**

* Riceve codice rilevazione (0-6)
* Riceve direzione movimento (0-4)
* Riceve conferma (1)

#### Raspberry Pi V1

**Focus**: Navigazione BFS e visione

**Caratteristiche principali:**

* ✅ Algoritmo BFS (Breadth-First Search)
* ✅ Memoria celle visitate (set-based)
* ✅ Coda celle da esplorare (FIFO)
* ✅ Lista celle non percorribili
* ✅ Rilevamento colori HSV (OpenCV)
* ✅ Riconoscimento lettere greche (Tesseract OCR)
* ✅ Gestione coordinate cartesiane (x, y)
* ✅ Calcolo direzioni relative
* ✅ Logging dettagliato operazioni

**Classi principali:**

```python
class Cella:
    # Rappresenta cella labirinto con coordinate (x, y)
    
class MemoriaCelle:
    # Set delle celle già visitate
    
# Funzioni navigazione:
- calcolaCoordinataCellaFrontale()
- calcolaCoordinataCellaDestra()
- calcolaCoordinataCellaSinistra()
- scegliDirezione()  # BFS
```

**Algoritmo BFS:**

1. Scansiona ambiente corrente
2. Aggiungi celle accessibili a coda
3. Prendi prima cella dalla coda (FIFO)
4. Muovi verso quella cella
5. Marca come visitata
6. Ripeti fino a coda vuota

**Rilevamento colori (HSV):**

```python
ROSSO:   H:\[0-10, 170-180] S:\[100-255] V:\[100-255]
GIALLO:  H:\[20-30] S:\[100-255] V:\[100-255]
VERDE:   H:\[40-80] S:\[50-255] V:\[50-255]
```

**Rilevamento lettere:**

* Preprocessing: conversione grayscale + threshold
* OCR: Tesseract con config greco (`-l ell`)
* Pattern matching: ricerca Φ/Ψ/Ω nel testo



---

## 🟢 Versione V2 - Avanzata

### Caratteristiche

La **versione V2** aggiunge meccanismi di **sicurezza** e **monitoraggio** per competizioni reali.

#### Arduino V2

**Novità rispetto a V1:**

##### 1\. **Watchdog Timer**

##### 2\. **Heartbeat Attivo**

##### 3\. **Filtro Input Seriale**

##### 4\. **Buffer Circolare Sensori**





### Diagramma Flusso Heartbeat (V2)

```
┌─────────────────────────────────────────────────────┐
│              RASPBERRY PI (Thread Monitor)          │
│                                                     │
│  while True:                                        │
│      Invia "HB" → Arduino                           │
│      ║                                              │
│      ║  ┌─────────────────────────────────┐         │
│      ║  │ Controlla timestamp ultimo HB   │         │
│      ║  │ ricevuto da Arduino             │         │
│      ║  └─────────────┬───────────────────┘         │
│      ║                │                             │
│      ║         \\\[Timeout > 5s?]                      │
│      ║          ╱           ╲                       │
│      ║        SÌ            NO                      │
│      ║        │              │                      │
│      ║  Arduino MORTO   Arduino VIVO                │
│      ║        │              │                      │
│      ║   Blocca invii   Permetti invii              │
│      ║                                              │
│      sleep(1s)                                      │
│      ║                                              │
└──────╫──────────────────────────────────────────────┘
       ║
       ║ Seriale USB 115200 baud
       ║
┌──────╫──────────────────────────────────────────────┐
│      ▼              ARDUINO                         │
│                                                     │
│  void loop():                                       │
│      wdt\\\_reset()  ← Reset watchdog                  │
│                                                     │
│      if (millis - lastHB > 1000):                   │
│          Serial.println("HB")  → Raspberry          │
│          lastHB = millis                            │
│                                                     │
│      \\\[Esegue logica robot]                          │
│                                                     │
│      if (Serial.available()):                       │
│          String input = Serial.readStringUntil()    │
│          if (input == "HB"):                        │
│              continue  ← Ignora, è heartbeat        │
│          else:                                      │
│              processCommand(input)                  │
│                                                     │
└─────────────────────────────────────────────────────┘


```



##### 

##### ```

#### 

#### Vantaggi V2

* ✅ **Robustezza**: Recovery automatico da blocchi Arduino
* ✅ **Monitoraggio**: Sapere sempre se Arduino è operativo
* ✅ **Affidabilità**: Continua esecuzione anche con disconnessioni temporanee
* ✅ **Debugging**: Log dettagliati su problemi connessione
* ✅ **Sicurezza**: Evita comandi a sistema non responsive
* ✅ **Filtro rumore**: Sensori più stabili con mediana mobile

#### Overhead V2

* ⚠️ **+150 linee codice**: Più complessità
* ⚠️ **+1 thread**: Uso CPU leggermente maggiore (trascurabile)
* ⚠️ **Latenza**: +50-100ms per filtri (accettabile)

#### Quando usare V2

* ✅ **Competizioni ufficiali**
* ✅ **Test lunghi** (>10 minuti)
* ✅ **Ambienti con interferenze** (EMI, WiFi)
* ✅ **Sistemi critici** dove affidabilità è prioritaria
* ✅ **Debugging problemi intermittenti**

### 

## Versione V3 - Cognitive Targets

I cognitive targets sostituiscono i singoli colori sui muri e consistono in 5 cerchi concentrici (diametri: 1cm, 2cm, 3cm, 4cm, 5cm) colorati con:

Colore	Valore

Nero	-2

Rosso	-1

Giallo	0

Verde	+1

Blu	+2



Sistema di rilevamento:



&nbsp;   Rileva tutti e 5 i cerchi concentrici (HoughCircles)

&nbsp;   Identifica il colore dominante di ogni cerchio

&nbsp;   Somma i valori associati ai colori

&nbsp;   Mappa la somma al tipo di vittima:

&nbsp;       Somma +2 → Harmed (2 medikit)

&nbsp;       Somma +1 → Stable (1 medikit)

&nbsp;       Somma 0 → Unharmed (0 medikit)

&nbsp;       Altre somme → Fake target (ignora)



Dual Detection System:

Il robot implementa un doppio sistema di fallback:



text

Muro rilevato

&nbsp;   ↓

1\. Cerca COGNITIVE TARGET (cerchi concentrici)

&nbsp;   ↓ (se non trovato)

2\. Cerca LETTERA GRECA (Φ/Ψ/Ω con OCR)

&nbsp;   ↓ (se non trovato)

3\. Nessuna vittima



Questo garantisce massima compatibilità con entrambi i sistemi di segnalazione.

### 



## 🚀 Installazione e Configurazione

### 

### Requisiti Hardware

* ✅ Arduino Mega 2560
* ✅ Raspberry Pi 4 (2GB+ RAM)
* ✅ Scheda microSD (16GB+ Classe 10)
* ✅ Cavo USB A-B (Arduino ↔ Raspberry)
* ✅ Tutti i componenti listati nella sezione Hardware



---

### 

### Monitoraggio

#### Terminale Raspberry

```
======================================================================
  CICLO 3
  Posizione: (1, 2) | Direzione: 0
  Visitate: 7 | Da visitare: 4
  Arduino: ✓ ALIVE                    ← Solo V2
======================================================================

\[===== SCANSIONE AMBIENTE =====]
\[1/3] Scansione FRONTALE...
\[Arduino] \[DEBUG] Distanza FRONT: 125 mm
\[← Arduino] 1
\[INFO] Muro presente → rilevo colori/lettere
\[INFO] Frame acquisito
\[DEBUG] Pixel - R:1523 Y:234 G:189
\[INFO] Rilevato ROSSO
\[→ Arduino] 1
...
```

#### Display LCD Robot

```
┌────────────────────┐
│ SISTEMA ATTIVO     │
│ Pos: (2,1) Dir: 1  │
│ Visitate: 12       │
│                    │
└────────────────────┘
```

### Stop Sistema

* Premi nuovamente **pulsante START**
* Oppure `Ctrl+C` nel terminale
* Robot ferma motori e torna in standby



---



## 📚 Riferimenti

### 

### Documentazione Librerie

* [VL53L0X Pololu](https://github.com/pololu/vl53l0x-arduino)
* [MPU6050 Electronic Cats](https://github.com/ElectronicCats/mpu6050)
* [OpenCV Python](https://docs.opencv.org/4.x/d6/d00/tutorial_py_root.html)
* [PySerial](https://pythonhosted.org/pyserial/)

### 

### Regolamento RoboCup

* [RoboCup Rescue Maze Rules 2026](https://rescue.robocup.org/)

### 

### Risorse Aggiuntive

* [Arduino Mega Pinout](https://www.arduino.cc/en/Hacking/PinMapping2560)
* [Raspberry Pi GPIO](https://pinout.xyz/)
* [I2C Scanning](https://learn.adafruit.com/scanning-i2c-addresses)



---



## 👥 Autori

**Team**: mENIAC  
**Anno**: 2026  
**Competizione**: RoboCup Rescue Maze



---



## 📄 Licenza

Questo progetto è rilasciato sotto licenza MIT. Vedi file `LICENSE` per dettagli.



---

