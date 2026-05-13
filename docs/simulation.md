# Simulazione Algoritmo mENIAC — Passo per Passo

## Labirinto di Prova

Griglia 2×3 celle (30×30 cm ciascuna). Asse Y cresce verso NORD.

```
     X=0        X=1
     ┌─────┐
Y=2  │ BLU │   (inaccessibile da EST — muro W di (1,2))
     └──┬──┘
        │ (passaggio NORD-SUD)
     ┌──┴──┬─────┐
Y=1  │ NRM │ NRM │
     └──┬──┴──┬──┘
        │     │ (tentativo → CELLA NERA)
     ┌──┴──┐  ╳ (1,2) = cella nera
Y=0  │START│
     └─────┘
     (0,0)
```

### Muri definiti

| Cella    | Colore | N     | E     | S     | W     |
|----------|--------|-------|-------|-------|-------|
| (0,0)    | NORM   | aperto| muro  | muro  | muro  |
| (0,1)    | NORM   | aperto| aperto| aperto| muro  |
| (1,1)    | NORM   | muro  | muro  | muro  | aperto|
| (0,2)    | BLU    | muro  | muro  | aperto| muro  |
| (1,2)    | NERO   | —     | —     | —     | —     | ← trap, robot non ci arriva mai

Il robot tenta di raggiungere (1,2) da (1,1) → rilevato NERO → retromarcia.

---

## Legenda

| Simbolo | Significato |
|---------|-------------|
| `TX:`   | Arduino → Pi (Serial.println) |
| `RX:`   | Pi → Arduino (comm.send) |
| `LCD:`  | Schermo LCD Arduino |
| `DBG:`  | Serial debug [D] (filtrato da Pi) |
| `LOG:`  | Log terminale Pi |
| `[ARD]` | Esecuzione interna Arduino |
| `[PI]`  | Esecuzione interna Pi |

---

## FASE 0 — Avvio Hardware

### Arduino `setup()`

```
[ARD] Serial.begin(115200)
[ARD] Wire.begin() — bus I2C attivo
[ARD] lcd.init(), lcd.backlight()
[ARD] EEPROM.get(0, config) → magic=0xABCD1234 ✓ (calibrazione valida)
[ARD] BTN_START == HIGH (non premuto) → skip calibrazione
[ARD] ToF init: 6 sensori XSHUT LOW → HIGH uno alla volta
      sensors[0] (FRONT) → addr 0x30, startContinuous()
      sensors[1] (BACK)  → addr 0x31, startContinuous()
      sensors[2] (RA)    → addr 0x32, startContinuous()
      sensors[3] (RP)    → addr 0x33, startContinuous()
      sensors[4] (LA)    → addr 0x34, startContinuous()
      sensors[5] (LP)    → addr 0x35, startContinuous()
LCD:  "NO MPU MODE / BACKUP FW"
[ARD] delay(1000)
[ARD] medikitServo.attach(SERVO_PIN=4)
[ARD] medikitServo.write(90), servoAngle=90, delay(400)
LCD:  "SERVO INIT / 90 -> 0deg..."
[ARD] servoSlowMove(0, msPerDeg=12):
        90 step di 1° × 12ms = 1080ms totali
        servoAngle = 0
LCD:  "mENIAC 2026 / READY TO START"
```

### Pi `main()`

```
[PI]  Comm(): serial.Serial('/dev/ttyACM0', 115200) aperta
[PI]  VisionSystem(): YOLO model_v3 caricato
[PI]  cap_sx = VideoCapture(0) — camera sinistra OK
[PI]  cap_dx = VideoCapture(2) — camera destra OK
[PI]  time.sleep(2) — stabilizzazione
[PI]  flush buffer seriale (while comm.receive())
LOG:  [SYS] Sistema pronto. In attesa di <INIT> da Arduino...
LOG:  [SYS] Modello YOLO: caricato
LOG:  [SYS] Camera SX: OK   Camera DX: OK
LOG:  [SYS] Premi il tasto START su Arduino per iniziare.
[PI]  while True: msg = comm.receive() → None → time.sleep(0.01) → loop
```

---

## STEP 1 — Utente preme BTN_START

```
[ARD] digitalRead(BTN_START) == LOW
[ARD] delay(200)
[ARD] isRunning = true
LCD:  "mENIAC 2026 / GARA INIZIATA!"
DBG:  "=== GARA INIZIATA (NO MPU) ==="
TX:   <INIT>
[ARD] delay(100)
[ARD] scanSensors() ← prima scansione dalla cella (0,0)
```

**Arduino — `scanSensors()` da (0,0) orientato NORD:**

```
[ARD] delay(40) — stabilizzazione post-stop
[ARD] 3 letture ToF, media:
        FRONT: ~100cm (corridor aperto su 2 celle) → clampToF: 100 (ok)
        RA+RP → avg_R: ~14cm (muro EST a destra)
        LA+LP → avg_L: ~14cm (muro OVEST a sinistra)
[ARD] BACK:  ~5cm  (muro SUD dietro)
[ARD] F = (100 <= 25?) = 0   R = (14 <= 18?) = 1   L = (14 <= 18?) = 1
[ARD] getDetectedColor() → r,g,b ~ config.normal → winner=0 (NORM)
      grigio/rosso disabilitati → winner rimane 0
LCD:  "SCAN F:0 R:1 L:1 / Col:NORM"
DBG:  "SCAN F:100.0cm R:14.0cm L:14.0cm Col:0"
TX:   <DATA:0,011>
```

**Pi riceve `<INIT>`:**

```
[PI]  msg == "<INIT>"
[PI]  robot.x=0, robot.y=0, robot.orientation=NORD(0)
[PI]  robot.visited={}, robot.map={}, robot.stack=[]
[PI]  robot.no_transit=set(), robot.pending_blue=None
LOG:  [NAV] ========================================
LOG:  [NAV] GARA INIZIATA — mappa resettata
LOG:  [NAV] Posizione: (0,0)  Orientamento: NORD
LOG:  [NAV] ========================================
```

**Pi riceve `<DATA:0,011>`:**

```
[PI]  inner = "0,011"  →  c=0, f=0, r=1, l=1
[PI]  curr = (0,0)
[PI]  cell(0,0) creata, visited=True, color=0
[PI]  robot.visited = {(0,0)}
[PI]  Muri cella (0,0):
        walls[NORD(0)]          = (f==1) = False   ← passabile
        walls[EST(1)]           = (r==1) = True    ← muro
        walls[OVEST(3)]         = (l==1) = True    ← muro
        walls[SUD(2)]           = False            ← da dove veniva (per convenzione)
[PI]  propaga_simmetria(): nessun vicino ancora in mappa → nessuna propagazione
LOG:  [NAV] --- Cella (0,0) | Ori: NORD | F:NO R:SI L:SI | Pavimento: NORMALE ---
LOG:  [NAV] Celle esplorate: 1  Stack DFS: 0
[PI]  pending_blue=None, c=0 → skip blue logic
[PI]  Scansione visiva:
        r=1 → parete DESTRA, l=1 → parete SINISTRA, f=0 → no frontale
LOG:  [VIS] Scansione pareti: R, L
[PI]  flush_cameras(cap_sx, cap_dx, n=8) — svuota buffer
[PI]  time.sleep(0.3) — settle AWB
[PI]  vision.scan_wall('R', cap_dx): 3 frame → nessun rilevamento
[PI]  vision.scan_wall('L', cap_sx): 3 frame → nessun rilevamento
LOG:  [VIS] Nessuna vittima rilevata
[PI]  vittime = [] → no VIC command
[PI]  comm.send("<VIC:DONE>")
RX:   <VIC:DONE>
[ARD] handleCommand("VIC:DONE") → Serial.println("<VIC:DONE>")
TX:   <VIC:DONE>
[PI]  m == "<VIC:DONE>" → break loop attesa
```

**Pi calcola prossima mossa — `get_next_move()`:**

```
[PI]  curr=(0,0), cell=map[(0,0)]
[PI]  returning_home=False → fase ESPLORAZIONE
[PI]  Priorità rel: [0(dritto), 1(dx), 3(sx)]
        rel=0 → abs_d=(NORD+0)%4=NORD(0)
        walls[NORD]=False (passabile) ✓
        (0,1) not in visited ✓
        robot.stack.append((0,0))
        return (0, (0,1))
[PI]  rel_dir=0, target=(0,1)
[PI]  robot.last_move = 0
LOG:  [NAV] [ESPLORA] GO:AVANTI → (0,1)  (celle esplorate: 1)
[PI]  comm.send("<GO:0>")
RX:   <GO:0>
```

---

## STEP 2 — Avanzamento (0,0) → (0,1)

**Arduino riceve `<GO:0>`:**

```
[ARD] handleCommand("GO:0")
[ARD] d=0, dirs[0]="AVANTI"
LCD:  "GO: AVANTI"
DBG:  "Movimento verso: AVANTI"
[ARD] d==0 → nessuna rotazione
[ARD] moveForward()
```

**Arduino — `moveForward()` verso (0,1):**

```
LCD:  "AVANZAMENTO..."
DBG:  "Avanzamento iniziato"
[ARD] tof.ts=0, updateToF() → front=100cm
[ARD] 100 > FRONT_STUCK_THR(8) → NO stuck pre-check ✓

[ARD] moveCounter=0, lastMoveTime=millis()
[ARD] dF_start=100, dB_start=5 (muro SUD)
[ARD] wallFront=false (100 < 25? No)
[ARD] wallBack=true  (5 < 15? Yes)
[ARD] blackCount=0, stopCount=0, stuck=false
[ARD] setMotors(90, 90) ← robot inizia ad avanzare
[ARD] moveStart = millis()   ← T=0ms

--- LOOP AVANZAMENTO ---

T=0–249ms (BLACK_IGNORE_MS non ancora scaduto):
  getDetectedColor() NON chiamato — finestra di sicurezza
  ogni 20ms: updateToF(), calcolo drift anti-sbandamento
    ra≈14cm, la≈14cm → hasMuroR=true, hasMuroL=true
    center_err = 14-14 = 0
    angle_err  = (14-14)-(14-14) = 0
    drift = 0×6 + 0×4 = 0
    spd = 90 (wallFront=false)
    setMotors(90, 90)

T≈300ms (robot ha percorso ~3cm):
  BLACK_IGNORE_MS scaduto → getDetectedColor() attivo
  sensore colore legge ancora fondo bianco (NORM) → blackCount=0

T≈400ms:
  dB ≈ 35cm   (robot si è spostato ~30cm verso NORD, muro SUD ora 35cm dietro)
  wallBack=true AND dB(35) >= dB_start(5) + 29.5 = 34.5 → CONDIZIONE STOP ✓
  → break (fine-cella per muro posteriore)

[ARD] stopMotors()
[ARD] stuck=false → ramo OK
DBG:  "Avanzamento OK  F:70.0cm cnt=20 t=400ms"
LCD:  "AVANZ. OK / F:70.0cm"
TX:   <MOVE:OK>
[ARD] scanSensors() ← ora alla cella (0,1)
```

**Arduino — `scanSensors()` da (0,1) orientato NORD:**

```
[ARD] delay(40)
[ARD] Media 3 letture:
        FRONT: ~70cm (aperto verso (0,2) e oltre)
        avg_R:  ~14cm (muro... aspetta: (0,1) E=aperto verso (1,1))
```

> **Nota:** a (0,1) il muro E non esiste (aperto). Il sensore RA vede il corridoio verso destra: ~40cm. Il sensore LA vede ancora il muro W a ~14cm.

```
[ARD] avg_R ≈ 40cm (spazio aperto EST)
[ARD] avg_L ≈ 14cm (muro OVEST)
[ARD] F = (70 <= 25?) = 0
      R = (40 <= 18?) = 0
      L = (14 <= 18?) = 1
[ARD] getDetectedColor() → NORM → c=0
LCD:  "SCAN F:0 R:0 L:1 / Col:NORM"
DBG:  "SCAN F:70.0cm R:40.0cm L:14.0cm Col:0"
TX:   <DATA:0,001>
```

**Pi riceve `<MOVE:OK>`:**

```
[PI]  msg == "<MOVE:OK>"
[PI]  robot.update_position(steps=1):
        orientation = (NORD + last_move=0) % 4 = NORD
        x = 0 + DIR_DX[NORD](0) = 0
        y = 0 + DIR_DY[NORD](1) = 1
[PI]  robot.x=0, robot.y=1, robot.orientation=NORD
LOG:  [NAV] Avanzamento OK → pos (0,1)  ori NORD
```

**Pi riceve `<DATA:0,001>`:**

```
[PI]  c=0, f=0, r=0, l=1
[PI]  curr=(0,1)
[PI]  cell(0,1): visited=True, color=0
[PI]  walls[NORD]=False, walls[EST]=False, walls[OVEST]=True, walls[SUD]=False
[PI]  propaga_simmetria():
        (0,1).SUD=False → (0,0).NORD=False ✓ (già noto, invariato)
        (0,1).OVEST=True → vicino (−1,1) non in mappa → skip
        (0,1).EST=False → (1,1).OVEST=False (cella (1,1) non ancora in mappa → skip per ora)
LOG:  [NAV] --- Cella (0,1) | Ori: NORD | F:NO R:NO L:SI | Pavimento: NORMALE ---
LOG:  [NAV] Celle esplorate: 2  Stack DFS: 1
[PI]  pending_blue=None, c=0 → skip blue
[PI]  Scansione visiva: solo L=1
LOG:  [VIS] Scansione pareti: L
[PI]  vision.scan_wall('L', cap_sx): 3 frame → nessun rilevamento
LOG:  [VIS] Nessuna vittima rilevata
[PI]  vittime=[] → no VIC
[PI]  comm.send("<VIC:DONE>") → Arduino risponde <VIC:DONE> → break
```

**Pi — `get_next_move()` da (0,1):**

```
[PI]  curr=(0,1), orientazione=NORD
      rel=0 → NORD: walls[NORD]=False, (0,2) not visited ✓
      stack.append((0,1))
      return (0, (0,2))
[PI]  robot.last_move=0
LOG:  [NAV] [ESPLORA] GO:AVANTI → (0,2)  (celle esplorate: 2)
[PI]  comm.send("<GO:0>")
RX:   <GO:0>
```

---

## STEP 3 — Avanzamento (0,1) → (0,2) — CELLA BLU

```
[ARD] moveForward() verso (0,2)
      front≈70cm > 8 → ok
      setMotors(90,90), moveStart=T

T≈300ms: cella (0,2) a pavimento BLU — getDetectedColor():
  TCS3200 legge frequenze caratteristiche del blu
  d_blue < d_norm < d_black → winner=3 (BLU)
  3 != 4 → NO black detection
  
T≈400ms: muro posteriore a 30cm di distanza → break
[ARD] stopMotors()
DBG:  "Avanzamento OK  F:5.0cm ..."
TX:   <MOVE:OK>
[ARD] scanSensors()
```

**`scanSensors()` da (0,2) orientato NORD:**

```
[ARD] FRONT: ~5cm (muro NORD — bordo labirinto)
      avg_R:  ~5cm (muro EST — bordo)
      avg_L:  ~5cm (muro OVEST — bordo)
[ARD] F=1, R=1, L=1
[ARD] getDetectedColor() → BLU → c=3
LCD:  "SCAN F:1 R:1 L:1 / Col:BLUE"
TX:   <DATA:3,111>
```

**Pi riceve `<MOVE:OK>` poi `<DATA:3,111>`:**

```
[PI]  <MOVE:OK>: pos (0,2), ori NORD

[PI]  <DATA:3,111>:
        c=3, f=1, r=1, l=1
        cell(0,2): color=3(BLU)
        walls[NORD]=True, walls[EST]=True, walls[OVEST]=True, walls[SUD]=False
LOG:  [NAV] --- Cella (0,2) | Ori: NORD | F:SI R:SI L:SI | Pavimento: BLU(attesa) ---
LOG:  [NAV] Celle esplorate: 3  Stack DFS: 2

[PI]  pending_blue=None, c=3 → BLU handling:
LOG:  [NAV] Cella BLU (0,2) — transito completo, attesa 5s obbligatoria
[PI]  comm.send("<WAIT:5>")
RX:   <WAIT:5>
      [ARD] LCD: "CELLA BLU / Attesa 5s..."
      [ARD] delay(5000)
      [ARD] LCD: "mENIAC 2026 / RUNNING"
[PI]  time.sleep(5.5)
[PI]  robot.pending_blue = (0,2)
LOG:  [NAV] Cella BLU (0,2) memorizzata — sarà bloccata all'uscita

[PI]  Scansione visiva: f=1,r=1,l=1 → tutte le pareti
LOG:  [VIS] Scansione pareti: R, L, F
[PI]  vision.scan_wall('R', cap_dx): nessun rilevamento
[PI]  vision.scan_wall('L', cap_sx): nessun rilevamento
[PI]  f=1 → scansione frontale:
      comm.send("<SCAN:FRONT>")
RX:   <SCAN:FRONT>
      [ARD] rotate(-90) — gira a destra 90°
             Fase1: setMotors(160,-160), delay(360ms)
             Fase2: setMotors(80,-80), delay(90ms)
             stopMotors(), delay(200ms)
      [ARD] Serial.println("<SCAN:OK>")
TX:   <SCAN:OK>
[PI]  m=="<SCAN:OK>" → break
[PI]  flush_cameras(cap_dx, n=5), time.sleep(0.3)
[PI]  vision.scan_wall('F', cap_dx): nessun rilevamento
[PI]  comm.send("<SCAN:DONE>")
RX:   <SCAN:DONE>
      [ARD] rotate(+90) — ritorna orientamento NORD
LOG:  [VIS] Nessuna vittima rilevata
[PI]  vittime=[] → comm.send("<VIC:DONE>") → attende <VIC:DONE> → ok
```

**Pi — `get_next_move()` da (0,2):**

```
[PI]  curr=(0,2), orientazione=NORD
      rel=0 → NORD: walls[NORD]=True ✗
      rel=1 → EST:  walls[EST]=True  ✗
      rel=3 → OVEST: walls[OVEST]=True ✗
      → nessuna direzione libera non visitata

[PI]  find_nearest_unexplored():
        BFS da (0,2): celle adiacenti passabili:
          SUD → (0,1) già visitata → aggiungi a queue, continua BFS
        Da (0,1): EST passabile → (1,1) non visitata ✓ → return (1,1)
        nearest = (1,1)

[PI]  get_shortest_path((0,2), (1,1)):
        Dijkstra: (0,2)→(0,1)→(1,1)  (path len=3)
        path = [(0,2),(0,1),(1,1)]
        path[1] = (0,1)

[PI]  target=(0,1), dx=0, dy=-1 → abs_d=SUD(2)
      rel_dir = (SUD - NORD) % 4 = (2-0)%4 = 2 (dietro = 180°)
[PI]  robot.last_move=2
LOG:  [NAV] [ESPLORA] GO:DIETRO → (0,1)  (celle esplorate: 3)
[PI]  comm.send("<GO:2>")
RX:   <GO:2>
```

---

## STEP 4 — Uscita dalla cella BLU: (0,2) → (0,1)

```
[ARD] handleCommand("GO:2")
      d=2 → rotate(180):
        Fase1: setMotors(-160,160), delay(704ms)
        Fase2: setMotors(-80,80), delay(176ms)
        stopMotors(), delay(200ms)
      [ARD] ora orientato SUD
[ARD] moveForward()
      front≈65cm (passaggio verso SUD aperto)
      > FRONT_STUCK_THR(8) → ok
      setMotors(90,90), moveStart=T
      muro posteriore(NORD) ≈ 5cm → wallBack=true

T≈300ms: cella (0,1) bianca sotto → NORM, blackCount=0
T≈400ms: dB ≈ 35cm >= 5+29.5=34.5 → break
[ARD] stopMotors()
TX:   <MOVE:OK>
[ARD] scanSensors() da (0,1) orientato SUD:
      FRONT ≈ 60cm (verso (0,0))
      avg_R ≈ 14cm (a SUD orientati: destra=OVEST=muro)
      avg_L ≈ 40cm (a SUD orientati: sinistra=EST=aperto)
      F=0, R=1, L=0, color=NORM(0)
TX:   <DATA:0,010>
```

**Pi riceve `<MOVE:OK>` poi `<DATA:0,010>`:**

```
[PI]  <MOVE:OK>:
        orientation=(NORD+2)%4=SUD(2)
        x=0+DIR_DX[SUD](0)=0, y=2+DIR_DY[SUD](-1)=1
        pos=(0,1), ori=SUD
LOG:  [NAV] Avanzamento OK → pos (0,1)  ori SUD

[PI]  <DATA:0,010>: c=0 ← NON BLU!
        pending_blue=(0,2), c!=3 →
LOG:  [NAV] Cella BLU (0,2) bloccata — bot ora su (0,1) (colore NORMALE)
        robot.no_transit.add((0,2))
        robot.pending_blue = None

[PI]  curr=(0,1), c=0, f=0, r=1, l=0
      (orientato SUD: muro a destra=OVEST già noto)
      walls[SUD]=False, walls[OVEST]=True, walls[EST]=False, walls[NORD]=False
      (aggiornamento parziale — muri già noti confermati)
LOG:  [NAV] --- Cella (0,1) | Ori: SUD | F:NO R:SI L:NO | Pavimento: NORMALE ---
LOG:  [NAV] Celle esplorate: 3  Stack DFS: 2

[PI]  scansione visiva: solo R=1 → parete OVEST
      vision.scan_wall('R', cap_dx): nessun rilevamento
[PI]  vittime=[] → VIC:DONE exchange → ok
```

**Pi — `get_next_move()` da (0,1) orientato SUD:**

```
[PI]  curr=(0,1)
      rel=0 → SUD: walls[SUD]=False, (0,0) visitata ✗
      rel=1 → OVEST: walls[OVEST]=True ✗
      rel=3 → EST: walls[EST]=False, (1,1) non visitata ✓
        stack.append((0,1)) — già c'era, ora duplicato ma ok
        return (3, (1,1))    ← rel=3 = sinistra (per chi guarda SUD, EST è a sinistra)
[PI]  robot.last_move=3
LOG:  [NAV] [ESPLORA] GO:SINISTRA → (1,1)  (celle esplorate: 3)
[PI]  comm.send("<GO:3>")
RX:   <GO:3>
```

---

## STEP 5 — Rotazione e avanzamento verso (1,1)

```
[ARD] handleCommand("GO:3")
      d=3 → rotate(-90) — gira a destra di 90° (da SUD diventa OVEST... 
      
      Nota: rotate(-90) = destra fisica:
        setMotors(160,-160) Fase1 → 360ms
        setMotors(80,-80)   Fase2 →  90ms
        stop, delay(200ms)
      [ARD] robot ora orientato (da SUD girando destra) = OVEST?
```

> **Nota implementativa:** Arduino non traccia l'orientamento internamente — esegue solo la rotazione fisica. L'orientamento è tracciato esclusivamente dal Pi. `rotate(-90)` gira il robot fisicamente a destra. Partendo da SUD e girando a destra → robot punta a OVEST. Ma il Pi sa che `last_move=3 (sinistra)` e che l'orientamento sarà `(SUD+3)%4 = (2+3)%4 = 1 = EST`. Questo è corretto: da SUD, "sinistra" è EST.

```
[ARD] moveForward() verso (1,1)
      front ≈ 14cm (muro EST della cella (1,1)) — aspetta...
      
      Da (0,1) guardando EST:
        front sensor punta a EST
        (1,1) ha muro EST (confine labirinto) a 30cm di distanza
        front ≈ 30cm > FRONT_STUCK_THR(8) → ok, ma wallFront sarà true subito
        
[ARD] dF_start=30, wallFront=(30<25?)=false
      dB_start ≈ 40cm (muro OVEST dietro)
      wallBack = (40<15?) = false → condizione muro posteriore non usabile

[ARD] setMotors(90,90), moveStart=T

T=20ms tick: front≈28cm < 25cm → wallFront=true
T=250ms: BLACK_IGNORE scaduto, getDetectedColor()→NORM
T=tick con dF≤6.5cm:
  dF≈6cm, wallFront=true, dF<=TARGET_FRONT_DIST(6.5):
  stopCount++ → 1
  prossimo tick: ancora ≤6.5 → stopCount=2 >= STOP_CONFIRM(2) → break

[ARD] stopMotors()
DBG:  "Avanzamento OK  F:6.2cm cnt=15 t=320ms"
TX:   <MOVE:OK>
[ARD] scanSensors() da (1,1) orientato EST
```

**`scanSensors()` da (1,1) orientato EST:**

```
[ARD] FRONT ≈ 5cm  (muro EST — confine labirinto)
      avg_R ≈ 5cm  (muro SUD — orientati EST, destra=SUD)
      avg_L ≈ 5cm  (muro NORD — orientati EST, sinistra=NORD)
      (il lato OVEST è aperto verso (0,1))
      F=1, R=1, L=1
      getDetectedColor() → NORM → c=0
LCD:  "SCAN F:1 R:1 L:1 / Col:NORM"
TX:   <DATA:0,111>
```

**Pi riceve `<MOVE:OK>` poi `<DATA:0,111>`:**

```
[PI]  <MOVE:OK>:
        orientation = (SUD+3)%4 = EST(1)
        x=0+DIR_DX[EST](1)=1, y=1+DIR_DY[EST](0)=1
        pos=(1,1), ori=EST
LOG:  [NAV] Avanzamento OK → pos (1,1)  ori EST

[PI]  <DATA:0,111>: c=0, f=1, r=1, l=1
        cell(1,1): walls[EST]=True, walls[SUD]=True, walls[NORD]=True, walls[OVEST]=False
        propaga_simmetria():
          (1,1).OVEST=False → (0,1).EST=False ✓ (già noto)
          (1,1).NORD=True   → (1,2).SUD=True  ← (1,2) non in mappa, skip
LOG:  [NAV] --- Cella (1,1) | Ori: EST | F:SI R:SI L:SI | Pavimento: NORMALE ---
LOG:  [NAV] Celle esplorate: 4  Stack DFS: 3

[PI]  scansione visiva: f=1,r=1,l=1
      vision.scan_wall('R', cap_dx): 3 frame → nessun rilevamento
      vision.scan_wall('L', cap_sx): 3 frame → nessun rilevamento
      f=1 → SCAN:FRONT → rotate(-90) → scan → rotate(+90)
      nessuna vittima
[PI]  vittime=[] → VIC:DONE exchange → ok
```

**Pi — `get_next_move()` da (1,1):**

```
[PI]  curr=(1,1), ori=EST
      rel=0 → EST:  walls[EST]=True  ✗
      rel=1 → SUD:  walls[SUD]=True  ✗
      rel=3 → NORD: walls[NORD]=True ✗
      → nessuna direzione libera non visitata

[PI]  find_nearest_unexplored():
        BFS da (1,1): OVEST=False → (0,1) visitata → continua
        Da (0,1): NORD=False → (0,2) visitata, in no_transit ✓ (skip)
                  EST=False → (1,1) già visitata
                  SUD=False → (0,0) visitata
        Da (0,0): NORD=False → (0,1) già in seen
        nessuna cella non visitata raggiungibile!
        return None
```

> **Tutte le celle raggiungibili sono state esplorate!** (0,0), (0,1), (1,1), (0,2). La cella nera (1,2) è protetta dal muro NORD di (1,1). La cella blu (0,2) è in `no_transit`. Missione completata → ritorno a casa.

```
[PI]  nearest=None → passa al ramo RITORNO
[PI]  get_shortest_path((1,1), (0,0)):
        Dijkstra: (1,1)→OVEST→(0,1)→SUD→(0,0)
        path = [(1,1),(0,1),(0,0)]
        robot.returning_home = True
        robot.dijkstra_path = [(0,1),(0,0)]
LOG:  [NAV] Esplorazione completata! Ritorno a casa via 2 celle.
[PI]  get_next_move() ricorsivo:
        returning_home=True
        target = dijkstra_path.pop(0) = (0,1)
        dx=0-1=-1, dy=1-1=0 → abs_dir=OVEST(3)
        rel_dir=(OVEST-EST)%4=(3-1)%4=2 (dietro)
[PI]  robot.last_move=2
LOG:  [NAV] [RITORNO] GO:DIETRO → (0,1)  (celle esplorate: 4)
[PI]  comm.send("<GO:2>")
```

---

## STEP 6 — Tentativo fallito: muro rilevato a (1,1)

> In questo labirinto (1,1) ha muro a NORD, quindi il robot NON tenta mai di andare verso (1,2) dalla navigazione normale. Il rilevamento della cella NERA avviene invece se il robot prova a spostarsi in una direzione che fisicamente porta su un pavimento nero. Mostriamo il scenario alternativo per completezza.

### SCENARIO ALTERNATIVO — Rilevamento cella nera

> Ipotesi: il muro NORD di (1,1) non esiste (non rilevato correttamente), e Pi ordina `<GO:0>` verso (1,2).

```
[ARD] moveForward() verso (1,2)
      front ≈ 60cm (spazio aperto), ok
      setMotors(90,90), moveStart=T

T=0–249ms: skip black check (finestra iniziale)

T=260ms:
  getDetectedColor():
    TCS3200 legge frequenza alta per tutti i canali (superfice nera assorbe luce)
    d_black = distanza minima → winner=4 (NERO)
    blackCount++ → 1

T=320ms:
  getDetectedColor() → NERO ancora
  blackCount++ → 2 >= BLACK_CONFIRM(2) → CELLA NERA CONFERMATA

[ARD] stopMotors()
[ARD] elapsed = 320ms (tempo dall'avvio)
LCD:  "CELLA NERA! / Ritorno..."
DBG:  "Cella NERA (x2) retromarcia 320ms"
[ARD] setMotors(-90,-90) ← retromarcia
[ARD] delay(320ms) ← stesso tempo → torna alla posizione di partenza
[ARD] stopMotors()
LCD:  "CELLA NERA / Rientrato OK"
DBG:  "Rientro completato"
TX:   <MOVE:BLACK>
[ARD] scanSensors() ← scansione da (1,1), robot tornato indietro
TX:   <DATA:0,111>  (stesso di prima)
```

**Pi gestisce `<MOVE:BLACK>`:**

```
[PI]  msg == "<MOVE:BLACK>"
[PI]  curr = (1,1)
[PI]  abs_d = (EST + last_move=0) % 4 = EST(1)  ← se era GO:0 (dritto)
      
      Ma da EST, dritto=EST e (1,1).NORD=True già noto...
      Se invece il tentativo era GO:0 da orientazione NORD-precedente:
      abs_d = (NORD + 0) % 4 = NORD(0)
      
[PI]  robot.map[(1,1)].walls[NORD] = True  ← confermato muro (o cella nera=muro)
[PI]  stack.pop() se top==(1,1)
LOG:  [NAV] CELLA NERA bloccata — dir NORD da (1,1) marcata come muro
```

---

## STEP 7 — SCENARIO STUCK (angolo)

> Robot si incastra in un angolo durante una rotazione errata. All'inizio di `moveForward()`, front sensor legge 3cm.

```
[ARD] moveForward()
[ARD] tof.ts=0, updateToF() → front=3.0cm
[ARD] 3.0 <= FRONT_STUCK_THR(8.0) → PRE-CHECK STUCK ATTIVATO
LCD:  "STUCK pre-move / F:3.0cm"
DBG:  "STUCK pre-move: front=3.0cm <= 8.0"
[ARD] setMotors(-90,-90) ← backup immediato
[ARD] delay(900ms)        ← BACKUP_MS
[ARD] stopMotors()
TX:   <MOVE:STUCK>
[ARD] scanSensors() ← scansione dalla posizione attuale (leggermente arretrata)
TX:   <DATA:0,...>
```

**Pi gestisce `<MOVE:STUCK>`:**

```
[PI]  msg == "<MOVE:STUCK>"
[PI]  curr = posizione corrente del robot
[PI]  abs_d = (orientation + last_move) % 4
[PI]  robot.map[curr].walls[abs_d] = True  ← direzione bloccata
[PI]  stack.pop() se top==curr
LOG:  [NAV] STUCK — dir [X] da [curr] bloccata.
LOG:  [NAV]         Robot ha fatto backup. DFS sceglie altra direzione.
[PI]  ← al prossimo <DATA:>, DFS sceglierà direzione alternativa
```

---

## STEP 8 — Ritorno a casa: (1,1) → (0,1) → (0,0)

```
[ARD] GO:2 → rotate(180) → moveForward() → OK → pos=(0,1)
TX:   <MOVE:OK>  +  <DATA:0,010>

[PI]  pos=(0,1), ori=OVEST
      dijkstra_path=[(0,0)]
      next target=(0,0)
      dx=0-0=0, dy=0-1=-1 → abs_dir=SUD
      rel_dir=(SUD-OVEST)%4=(2-3)%4=3 (sinistra)
LOG:  [NAV] [RITORNO] GO:SINISTRA → (0,0)  (celle esplorate: 4)
[PI]  comm.send("<GO:3>")

[ARD] rotate(-90) + moveForward() → OK → pos=(0,0)
TX:   <MOVE:OK>  +  <DATA:0,011>

[PI]  pos=(0,0), ori=SUD
      dijkstra_path=[] → return (None, None)
LOG:  [NAV] ========================================
LOG:  [NAV] MISSIONE COMPLETATA
LOG:  [NAV] Celle totali esplorate: 4
LOG:  [NAV] Vittime totali trovate: (vedi log VIS)
LOG:  [NAV] ========================================
```

---

## Riepilogo Completo

| Step | Da   | A    | Comando | Risultato    | Celle esp. |
|------|------|------|---------|--------------|-----------|
| 1    | —    | (0,0)| INIT    | SCAN iniziale| 1         |
| 2    | (0,0)| (0,1)| GO:0    | MOVE:OK      | 2         |
| 3    | (0,1)| (0,2)| GO:0    | MOVE:OK + BLU (5s attesa) | 3 |
| 4    | (0,2)| (0,1)| GO:2    | MOVE:OK + BLU bloccata | 3 |
| 5    | (0,1)| (1,1)| GO:3    | MOVE:OK      | 4         |
| 6*   | (1,1)| (1,2)| GO:0*   | MOVE:BLACK*  | 4         |
| 7    | (1,1)| (0,1)| GO:2    | MOVE:OK (ritorno) | 4    |
| 8    | (0,1)| (0,0)| GO:3    | MOVE:OK (casa!) | 4      |

`*` = scenario alternativo (cella nera) — non si verifica in questo labirinto poiché (1,1) ha muro NORD. Mostrato a scopo dimostrativo.

### Messaggi seriali totali scambiati

```
Arduino → Pi:  <INIT>, <DATA:0,011>, <MOVE:OK>×6, <DATA:×6>, <VIC:DONE>×5, <SCAN:OK>×1
Pi → Arduino:  <GO:0>×3, <GO:2>×2, <GO:3>×2, <WAIT:5>, <VIC:DONE>×5, <SCAN:FRONT>, <SCAN:DONE>
```
