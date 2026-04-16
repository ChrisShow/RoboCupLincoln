# Documentazione Tecnica — Robot mENIAC
## RoboCup Junior Rescue Maze 2026

**Team:** mENIAC  
**Versione Documento:** 2.0  
**Data:** Aprile 2026

---

## Indice

1. [Architettura di Sistema](#1-architettura-di-sistema)
2. [Specifiche Fisiche e Componenti](#2-specifiche-fisiche-e-componenti)
3. [Protocollo di Comunicazione Seriale](#3-protocollo-di-comunicazione-seriale)
4. [Logica Master — Raspberry Pi 4B](#4-logica-master--raspberry-pi-4b)
5. [Logica Slave — Arduino Mega 2560](#5-logica-slave--arduino-mega-2560)
6. [Sistema di Visione Artificiale](#6-sistema-di-visione-artificiale)
7. [Soglie Geometriche e Calibrazione](#7-soglie-geometriche-e-calibrazione)
8. [Firmware di Backup (Senza MPU6050)](#8-firmware-di-backup-senza-mpu6050)
9. [Procedura di Avvio e Gara](#9-procedura-di-avvio-e-gara)

---

## 1. Architettura di Sistema

Il robot opera su un'architettura **Master/Slave distribuita** composta da due unità elaborative indipendenti che comunicano via seriale USB.

```
┌─────────────────────────────────┐     USB Serial     ┌──────────────────────────────────┐
│      MASTER: Raspberry Pi 4B    │ ◄────────────────► │    SLAVE: Arduino Mega 2560       │
│                                 │   115200 baud       │                                  │
│  • Navigazione e mappatura      │   pacchetti <...>   │  • Controllo motori (PID)        │
│  • Pathfinding DFS + Dijkstra   │                     │  • Anti-Drift ToF (6 sensori)    │
│  • Simmetria muri               │                     │  • Rotazione precisa MPU6050     │
│  • Visione AI (YOLOv8s)         │                     │  • Gestione rampe (Pitch)        │
│  • Decisione soccorso vittime   │                     │  • Sensore colore TCS3200        │
│  • Logging timestampato         │                     │  • Calibrazione EEPROM           │
└─────────────────────────────────┘                     │  • Erogazione medikit (SG90)     │
                                                        │  • Display LCD 16×2              │
            2× Innomaker U20CAM                         └──────────────────────────────────┘
            (Fiancate DX e SX, USB 3.0)
```

**Principio fondamentale:** Il Raspberry Pi è il "cervello" — decide dove andare e cosa fare. Arduino è il "corpo" — esegue movimenti fisici precisi e legge i sensori. Arduino non prende mai decisioni di navigazione autonomamente.

---

## 2. Specifiche Fisiche e Componenti

### 2.1 Geometria Robot

| Misura | Valore |
|---|---|
| Scocca (lunghezza × larghezza) | 17 cm × 10 cm |
| Ingombro totale con ruote | 17 cm × 13 cm |
| Centro di rotazione | Baricentro centrale |
| Front/Rear ToF (bordo scocca) | ±8.5 cm dal centro |
| Side ToF (bordo scocca) | ±5.0 cm dal centro |
| Ruote (bordo esterno) | ±6.5 cm dal centro |
| Sensore colore (punta) | +12.5 cm dal centro (anteriore) |
| Medikit (punta) | −9.5 cm dal centro (posteriore) |

### 2.2 Distinta Componenti (BOM)

| Componente | Modello | Qtà | Funzione |
|---|---|:---:|---|
| Single-Board Computer | Raspberry Pi 4B (4 GB RAM) | 1 | Master: visione e navigazione |
| Microcontrollore | Arduino Mega 2560 | 1 | Slave: motori e sensori real-time |
| Telecamere | Innomaker U20CAM 1080p USB | 2 | Rilevamento vittime (fiancate) |
| Sensori di distanza | VL53L0X (ToF laser) | 6 | Anti-drift, stop cella, rilevamento muri |
| Motori | TT Motor DC (giallo) | 4 | Trazione skid-steer |
| Driver motori | TB6612FNG | 2 | Controllo potenza/direzione motori |
| Sensore colore | TCS3200 | 1 | Rilevamento colore pavimento |
| Giroscopio / IMU | MPU6050 (I2C) | 1 | Rotazioni PID e rilevamento rampa |
| Servomotore | SG90 | 1 | Meccanismo rilascio medikit |
| Display | LCD 16×2 con I2C (PCF8574) | 1 | Debug e stato gara |
| Batteria | LiPo/Li-ion 12 V 4 Ah | 1 | Alimentazione principale |
| Regolatori DC-DC | XL4015E1 step-down | 2 | 5.1 V (Pi) e 6.0 V (motori/servo) |
| Pulsante | Tattile N.A. | 1 | START / STOP / calibrazione |
| LED stato | LED 5mm | 1 | Indicatore visuale soccorso attivo |

### 2.3 Assegnazione Pin Arduino Mega

| Periferica | Pin(s) |
|---|---|
| PWM Fiancata DX | 2 |
| PWM Fiancata SX | 3 |
| Servo medikit | 4 |
| DIR DX (IN1/IN2) | 22, 23 |
| DIR SX (IN1/IN2) | 24, 25 |
| XSHUT ToF (Fronte, Retro, DX-ant, DX-post, SX-ant, SX-post) | 30–35 |
| TCS3200 (S0, S1, S2, S3, OUT) | 38, 39, 40, 41, 42 |
| Pulsante START | 43 |
| LED stato | 45 |
| Bus I2C (SDA, SCL) — MPU6050, ToF, LCD | 20, 21 |

---

## 3. Protocollo di Comunicazione Seriale

La comunicazione avviene tramite pacchetti ASCII delimitati da `<` e `>`, a **115200 baud**. Ogni pacchetto è atomico e terminato da `\n`.

### 3.1 Arduino → Raspberry Pi

| Pacchetto | Descrizione |
|---|---|
| `<INIT>` | Robot pronto. Raspberry resetta mappa e avvia algoritmo. |
| `<DATA:C,FRL>` | Lettura sensori. `C`=colore pavimento (0–4), `FRL`=muri (1=presente, 0=assente). |
| `<MOVE:OK>` | Avanzamento 30 cm completato con successo. |
| `<MOVE:RAMP>` | Rampa superata (avanzamento di 60 cm = 2 celle). |
| `<MOVE:BLACK>` | Cella nera rilevata; robot rientrato al centro della cella precedente. |
| `<SCAN:OK>` | Rotazione 90° per scansione frontale completata. |
| `<VIC:DONE>` | Erogazione medikit completata. |
| `<LOP>` | Segnale Lack of Progress: Raspberry aggiorna mappa e riposiziona. |
| `[D] msg` | Messaggio di debug (non pacchetto navigazione); stampato come `[ARD] msg` sul terminale Pi. |

### 3.2 Raspberry Pi → Arduino

| Pacchetto | Descrizione |
|---|---|
| `<GO:D>` | Muovi in direzione relativa: `0`=avanti, `1`=destra, `2`=dietro, `3`=sinistra. |
| `<SCAN:FRONT>` | Ruota 90° SX per esporre camera laterale al muro frontale. |
| `<SCAN:DONE>` | Ripristina orientamento (ruota 90° DX) dopo scansione frontale. |
| `<VIC:POS,TIPO>` | Eroga medikit: `POS`=`F`/`R`/`L`, `TIPO`=`1`/`2`/`3`. |
| `<WAIT:N>` | Attendi N secondi (usato per celle blu). |

### 3.3 Codifica Colori (`C` in `<DATA:C,FRL>`)

| Valore | Colore | Azione Raspberry |
|---|---|---|
| 0 | Bianco (normale) | Prosegui |
| 1 | Grigio (checkpoint) | Registra checkpoint |
| 2 | Rosso (danger) | Registra checkpoint |
| 3 | Blu (delay) | Invia `<WAIT:5>`, imposta cella come `no_transit` |
| 4 | Nero (trappola) | Già gestito da Arduino con `<MOVE:BLACK>` |

---

## 4. Logica Master — Raspberry Pi 4B

Il firmware Master (`raspberry/src/main.py`) è scritto in Python 3 e gira in un loop event-driven.

### 4.1 Rappresentazione della Mappa

La mappa è un dizionario Python:

```python
robot.map: Dict[Tuple[int,int], Cell]
```

Ogni `Cell` memorizza:
- `walls[4]` — muri nei 4 lati assoluti (N/E/S/O); `True`=muro, `False`=apertura, `None`=ignoto
- `visited` — se la cella è stata percorsa
- `color` — colore rilevato dal TCS3200

Le coordinate crescono: Est=+X, Nord=+Y. L'orientamento assoluto del robot è tracciato come intero (0=N, 1=E, 2=S, 3=O).

### 4.2 Esplorazione DFS

L'algoritmo principale è un **Depth-First Search** con stack LIFO:

1. All'arrivo in una nuova cella, legge i muri da `<DATA:C,FRL>` e li converte in coordinate assolute.
2. Applica la **simmetria dei muri** (vedi §4.5) per popolare celle adiacenti senza muoversi.
3. Cerca la prossima direzione da esplorare con la priorità: avanti → destra → sinistra → dietro.
4. Se tutte le direzioni sono bloccate o già visitate, esegue **backtracking BFS** (`find_nearest_unexplored`): trova la cella inesplorata più vicina tramite BFS sul grafo noto e genera il percorso verso di essa.
5. Le celle in `no_transit` (blu già attraversate) sono **escluse** sia dall'esplorazione sia dal backtracking.

### 4.3 Ritorno a Casa con Dijkstra

Quando `find_nearest_unexplored()` restituisce `None` (labirinto completamente esplorato):

1. Si calcola il percorso minimo da posizione attuale a `(0,0)` con **Dijkstra** (`heapq`).
2. Il robot segue il percorso step by step inviando comandi `<GO:D>`.
3. Le celle `no_transit` sono escluse dal grafo Dijkstra.

### 4.4 Aggiornamento Posizione

```python
def update_position(steps=1):
    robot.facing = (robot.facing + last_move) % 4   # orientamento: una sola volta
    robot.x += DIR_DX[robot.facing] * steps          # posizione: × steps
    robot.y += DIR_DY[robot.facing] * steps
```

Per `<MOVE:RAMP>` si chiama `update_position(steps=2)`, avanzando di 2 celle senza doppiare la rotazione.

### 4.5 Propagazione Simmetria Muri

Dopo ogni aggiornamento mappa, si propaga ogni muro noto alla cella adiacente corrispondente:

```python
def propaga_simmetria(robot):
    for (x, y), cell in robot.map.items():
        for d, is_wall in enumerate(cell.walls):
            if is_wall is None:
                continue
            nx = x + DIR_DX[d]
            ny = y + DIR_DY[d]
            neighbor = robot.map.get((nx, ny))   # None se cella non in mappa
            if neighbor is not None and neighbor.walls[OPPOSTO[d]] is None:
                neighbor.walls[OPPOSTO[d]] = is_wall
```

**Nota di sicurezza:** `robot.map.get((nx, ny))` restituisce `None` per celle al bordo della mappa non ancora create — nessuna scrittura avviene su celle inesistenti.

### 4.6 Gestione Celle Blu

Le celle blu si possono attraversare **una sola volta**:
1. Raspberry riceve `<DATA:3,...>` → invia `<WAIT:5>` ad Arduino.
2. La cella corrente viene aggiunta a `robot.no_transit`.
3. Nelle iterazioni future, DFS e Dijkstra saltano questa cella (trattata come nera).

### 4.7 Logging Timestampato

Ogni evento significativo viene loggato sul terminale con formato:

```
[HH:MM:SS][CATEGORIA] messaggio
```

Categorie principali: `INIT`, `NAV`, `MAPPA`, `VIS`, `SOCCORSO`, `COLORE`, `ARD`.

I messaggi `[D] msg` ricevuti da Arduino vengono stampati come `[ARD] msg`.

---

## 5. Logica Slave — Arduino Mega 2560

Il firmware Slave (`arduino/firmware/src/main.ino`) è scritto in C++ per Arduino.

### 5.1 Calibrazione Colori EEPROM

All'accensione con pulsante tenuto premuto (o se la EEPROM non è inizializzata), il robot entra in modalità calibrazione interattiva:

1. Visualizza su LCD "POSIZIONA SU: BIANCO/NORM".
2. Operatore posiziona il robot sulla tessera e preme il pulsante.
3. Il sensore legge i valori RGB grezzi tramite `pulseIn()` (TCS3200 scaling 20%).
4. Ripete per: Grigio, Rosso, Blu, Nero.
5. Salva tutto in `EEPROM.put(0, config)` con magic word `0xABCD1234`.

Classificazione colore a runtime: **distanza Euclidea** al quadrato tra lettura corrente e campioni calibrati. Cast a `long` per evitare overflow 16-bit su Arduino Mega.

### 5.2 Cache Sensori ToF

I 6 VL53L0X condividono il bus I2C con indirizzi assegnati dinamicamente via pin XSHUT (0x30–0x35). Le letture vengono cachate in una struct `ToFCache` e aggiornate al massimo ogni 15 ms per ridurre la latenza del loop principale.

```cpp
struct ToFCache { float front, back, ra, rp, la, lp; unsigned long ts; };

void updateToF() {
  if (millis() - tof.ts < TOF_REFRESH_MS) return;
  // leggi tutti e 6...
  tof.ts = millis();
}
```

### 5.3 Rotazione PID con Anti-Windup

La funzione `rotate(float rel_deg)` usa un regolatore **PID** sull'angolo Yaw del giroscopio MPU6050:

- Target = Yaw attuale + angolo richiesto
- Loop a 10 ms, timeout 4 s
- Termine derivativo: differenza tra errore attuale e precedente
- **Anti-windup:** integrale limitato a `±INTEGRAL_CLAMP = 200`
- Stop quando `|errore| ≤ 1.5°`

```cpp
integral = constrain(integral + error, -INTEGRAL_CLAMP, INTEGRAL_CLAMP);
output = error*Kp + integral*Ki + derivative*Kd;
setMotors(constrain(-output,-150,150), constrain(output,-150,150));
```

### 5.4 Avanzamento con Anti-Drift Angolare

La funzione `moveForward()` esegue l'avanzamento di 30 cm (o 60 cm su rampa) con correzione continua:

**Anti-drift:** usa 4 sensori laterali (ant e post per lato) per correggere sia la centratura che l'angolazione:

```cpp
// Corridoio stretto: 2 muri
center_err = ra - la;                    // errore di centratura
angle_err  = (ra - rp) - (la - lp);     // errore di angolazione (yaw relativo)
drift = center_err * 6.0 + angle_err * 4.0;

// Solo muro destro:
drift = (ra - TARGET_LAT_DIST) * 10.0 + (ra - rp) * 4.0;

// Emergenza ruote (contatto imminente):
if (ra < 4.5 || rp < 4.5) drift = -60;
```

**Rilevazione muro frontale dinamica:** all'avvio, il ToF frontale legge ~36.5 cm (parete della cella successiva). Quando durante il movimento scende sotto `SOGLIA_MURO_FRONT = 25 cm`, il muro è entrato nel range e il robot inizia a tracciarlo fino a `TARGET_FRONT_DIST = 6.5 cm`.

**Condizioni di stop:**

| Condizione | Trigger |
|---|---|
| Muro frontale | `wallFront=true` e ToF front ≤ 6.5 cm |
| Muro posteriore | ToF back ≥ `dB_start + 29.5 cm` |
| Fail-safe | 90 cicli × 20 ms = 1.8 s (nessun muro rilevato) |

### 5.5 Gestione Rampe (Pitch Control)

Rilevamento tramite asse Pitch del MPU6050:

| Condizione | Azione |
|---|---|
| `\|Pitch\| > 15°` → salita | PWM boost a 230 |
| `\|Pitch\| > 15°` → discesa | PWM ridotto a 80 |
| `\|Pitch\| < 5°` dopo rampa | Stop → `<MOVE:RAMP>` → scan sensori |

Il limite dei 30 cm viene ignorato durante la rampa; il robot si ferma solo al rientro in piano.

### 5.6 Rilevamento Cella Nera

Durante ogni tick di 20 ms in `moveForward()`:

1. `getDetectedColor()` campiona TCS3200.
2. Se colore == 4 (nero): retromarcia per `moveCounter × 20 ms` (ritorno al centro precedente).
3. Invia `<MOVE:BLACK>` al Raspberry.
4. Chiama `scanSensors()` per inviare subito `<DATA:>` della cella precedente.

### 5.7 Soccorso Fisico

`handleRescue(char pos, int tipo)`:

1. Ruota verso il muro vittima (F→180°, R→-90°, L→+90°).
2. Eroga `tipo` medikit: servo SG90 a 0°, pausa 1 s, ritorna a 90°.
3. Ruota di ritorno all'orientamento originale.
4. Invia `<VIC:DONE>`.

### 5.8 Logging LCD e Debug Seriale

Ogni operazione significativa viene:
- **Visualizzata sul display LCD 16×2** (`lcdShow(riga1, riga2)`)
- **Inviata sul seriale** con prefisso `[D]` per essere stampata dal Raspberry (`dbg(msg)`)

Esempi di messaggi visualizzati: rotazione in corso (direzione + target), avanzamento (distanza front ToF), scan muri (F/R/L + colore), cella nera (durata retromarcia), rampa (pitch angle), erogazione kit (numero / totale).

---

## 6. Sistema di Visione Artificiale

### 6.1 Hardware

Due telecamere **Innomaker U20CAM 1080p** montate sulle fiancate DX e SX del robot, collegate via USB 3.0 al Raspberry Pi. Il codec è impostato a **MJPG** per ridurre la latenza di acquisizione.

### 6.2 Modello AI — YOLOv8s

Il sistema utilizza il modello **YOLOv8s** (`model_v3/best.pt`) addestrato su 4 classi:

| Classe | Vittima | Kit erogati |
|---|---|---|
| `PHI` (Φ) | Grave (Harmed) | 3 |
| `PSI` (Ψ) | Stabile (Stable) | 2 |
| `OMEGA` (Ω) | Illesa (Unharmed) | 1 |
| `FAKE` | Falso positivo | 0 (ignorato) |

**Cognitive targets non implementati:** per scelta progettuale, i target cognitivi (cerchi concentrici) non vengono rilevati per evitare penalità da falsi positivi.

### 6.3 Pipeline di Rilevamento Multi-Frame

Per ogni muro da analizzare, la funzione `scan_wall()` acquisisce **3 frame separati** e seleziona la detection con **confidenza massima**:

```python
def scan_wall(pos, cap, n_frames=3):
    flush_cameras(cap, n=8)          # svuota buffer hardware
    best_tipo, best_conf = 0, 0.0
    for _ in range(n_frames):
        cap.read()                   # frame intermedio scartato
        ret, frame = cap.read()
        tipo, conf = detect_victim(frame)
        if conf > best_conf:
            best_tipo, best_conf = tipo, conf
    return best_tipo
```

**Flush buffer:** prima di ogni scansione, si eseguono 8 letture a vuoto per eliminare frame residui accumulati durante il movimento.

### 6.4 Sequenza di Scansione Completa

Per ogni cella visitata, il Raspberry esegue:

1. **Muro sinistro** → camera SX (già parallela al muro).
2. **Muro destro** → camera DX (già parallela al muro).
3. **Muro frontale** → invia `<SCAN:FRONT>` ad Arduino (rotazione 90°), scatta con camera laterale ora frontale, invia `<SCAN:DONE>` (ripristino orientamento).

Se viene rilevata una vittima (`tipo > 0`), invia `<VIC:POS,TIPO>` ad Arduino e attende `<VIC:DONE>`.

---

## 7. Soglie Geometriche e Calibrazione

Tutte le soglie sono derivate analiticamente dalla geometria del robot (celle 30×30 cm, robot 17×10 cm).

### 7.1 Distanze al Centro Cella

| Sensore | Formula | Valore target |
|---|---|---|
| Front ToF → muro frontale | 15 − 8.5 | **6.5 cm** (`TARGET_FRONT_DIST`) |
| Rear ToF → muro posteriore | 15 − 8.5 | **6.5 cm** |
| Side ToF → muro laterale | 15 − 5.0 | **10.0 cm** (`TARGET_LAT_DIST`) |
| Ruote → muro laterale | 15 − 6.5 | **8.5 cm** (contatto a 6.5 cm) |
| Sensore colore → muro frontale | 15 − 12.5 | **2.5 cm** (clearance sicura) |

### 7.2 Soglie di Rilevamento Muro

| Costante | Valore | Ragionamento |
|---|---|---|
| `SOGLIA_MURO_FRONT` | 25.0 cm | Centro tra 6.5 cm (muro presente) e 36.5 cm (muro cella successiva) |
| `SOGLIA_MURO_LAT` | 18.0 cm | Centro tra 10 cm (muro presente) e 40 cm (corridoio aperto) |
| `DIST_PERICOLO_RUOTA` | 4.5 cm | Side ToF a 4.5 cm = ruota a 2 cm dal muro (contatto a ~0.5 cm) |

### 7.3 Parametri PID Rotazione (Valori Default)

| Costante | Valore |
|---|---|
| `KP_ROT` | 4.5 |
| `KI_ROT` | 0.1 |
| `KD_ROT` | 1.2 |
| `INTEGRAL_CLAMP` | ±200 |

---

## 8. Firmware di Backup (Senza MPU6050)

Il file `arduino/firmware/src/main_senza_mpu.ino` è un firmware alternativo completo che **non utilizza il giroscopio MPU6050**.

### 8.1 Differenze rispetto al Firmware Principale

| Funzionalità | Firmware principale | Firmware backup |
|---|---|---|
| Rotazione | PID su Yaw MPU6050 | Open-loop: velocità × tempo (`ROT_90_MS = 450 ms`) |
| Rotazione 180° | PID 180° | `ROT_180_MS = 880 ms` |
| Rilevazione rampa | `\|Pitch\| > 15°` (MPU6050) | `moveCounter ≥ 90` cicli senza stop → modalità rampa |
| Uscita rampa | `\|Pitch\| < 5°` | `moveCounter ≥ 185` cicli (≈60 cm) → `<MOVE:RAMP>` |
| EEPROM | 5 colori + PID gains | 5 colori (PID gains rimossi) |
| Anti-drift ToF | Identico | Identico |
| Logging LCD/debug | Identico | Identico + banner "NO MPU MODE" |

### 8.2 Quando Usarlo

Utilizzare `main_senza_mpu.ino` se:
- Il giroscopio MPU6050 è difettoso o non risponde.
- La calibrazione IMU produce offset eccessivi.
- Si necessita di un avvio rapido senza calibrazione giroscopio (~1.5 s risparmiati).

**Nota:** le rotazioni open-loop richiedono calibrazione empirica di `ROT_90_MS` e `ROT_180_MS` in base al peso del robot, tipo di superficie e carica della batteria.

---

## 9. Procedura di Avvio e Gara

### 9.1 Prima della Gara

1. Accendere il robot (interruttore principale).
2. **Calibrazione colori (opzionale):** tenere premuto il pulsante START durante l'avvio. Seguire le istruzioni su LCD per posizionare il robot sulle tessere di riferimento.
3. Attendere che LCD mostri `mENIAC 2026 / READY TO START`.
4. Posizionare il robot nella cella di partenza, orientato verso Nord.

### 9.2 Avvio Gara

1. Premere brevemente il pulsante START.
2. Arduino invia `<INIT>` al Raspberry → mappa resettata, robot in `(0,0)` orientamento N.
3. Arduino esegue `scanSensors()` e invia `<DATA:C,FRL>`.
4. Raspberry riceve dati, aggiorna mappa, calcola prossima mossa, invia `<GO:D>`.
5. Il ciclo continua autonomamente fino al completamento dell'esplorazione.

### 9.3 Stop e Lack of Progress

- **Stop manuale:** premere il pulsante START durante la gara → motori fermati.
- **LOP:** se il giudice posiziona il robot in una nuova cella e preme il tasto LOP (pin 44), Arduino invia `<LOP>` al Raspberry → la cella corrente viene rimossa dallo stack DFS e ri-aggiunta come inesplorata.

### 9.4 Monitoraggio

Il terminale del Raspberry Pi stampa tutti gli eventi in tempo reale con timestamp:

```
[09:14:32][INIT] Robot pronto. Orientamento: N. Pos: (0,0)
[09:14:32][MAPPA] Cella (0,0): muri F=1 R=0 L=1
[09:14:33][NAV] Prossima mossa: GO direzione relativa 0 (AVANTI)
[09:14:35][MAPPA] Aggiornato posizione → (0,1) orientamento N
[09:14:35][VIS] Scan SX: nessuna vittima
[09:14:35][VIS] Scan DX: nessuna vittima
```

Il display LCD di Arduino mostra in parallelo lo stato corrente dell'operazione fisica in esecuzione.
