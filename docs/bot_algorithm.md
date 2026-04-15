# Algoritmo Definitivo di Navigazione FSM
**Progetto:** RoboCup Junior Rescue Maze 2026 - mENIAC Team

**Architettura:** Raspberry Pi 4B (Cervello/Visione) + Arduino Mega 2560 (Riflessi/Movimento)

---

## 1. PROTOCOLLO DI COMUNICAZIONE SERIALE
La comunicazione avviene tramite pacchetti racchiusi tra `<` e `>`.

* **Arduino -> Pi:**
  * `<INIT>`: Robot pronto nella cella di Start.
  * `<DATA:C,FRL>`: Dati sensori (C=Colore, FRL=Muri Fronte/Destra/Sinistra).
  * `<SCAN:OK>`: Allineamento per scatto frontale completato.
  * `<VIC:DONE>`: Routine di soccorso (medikit) terminata.
  * `<MOVE:OK>`: Movimento di 30cm completato con successo.
  * `<MOVE:BLACK>`: Rilevata cella nera, robot tornato al centro della cella precedente.
  * `<MOVE:RAMP>`: Superata rampa di 60cm (salita/discesa).
  * `<LOP>`: Premuto tasto Lack of Progress.

* **Pi -> Arduino:**
  * `<GO:DIR>`: Muovi in direzione relativa (0=Dritto, 1=DX, 2=Dietro, 3=SX).
  * `<SCAN:FRONT>`: Ruota di 90° per permettere la visione del muro frontale.
  * `<SCAN:DONE>`: Ripristina allineamento dopo scatto frontale.
  * `<VIC:POS,TIPO>`: Eroga medikit (POS=F/R/L, TIPO=1/2/3).
  * `<WAIT:5>`: Attesa obbligatoria (es. cella blu).

---

## 2. LOGICA DEL MASTER (Raspberry Pi)

1.  **Mappatura e Navigazione (DFS + Dijkstra):**
    *   Utilizza un algoritmo **DFS (Depth First Search)** per l'esplorazione, mantenendo uno Stack delle celle visitate.
    *   Mantiene una mappa globale coordinata `(x,y)` e l'orientamento assoluto `(N,E,S,O)`.
    *   Se l'esplorazione è completa, utilizza l'algoritmo di **Dijkstra** per calcolare il percorso minimo verso lo START `(0,0)`.
    *   Gestione Rampa: Se riceve `<MOVE:RAMP>`, incrementa le coordinate di **due unità** nella direzione di marcia.

2.  **Visione Ibrida (YOLOv8 + Matematica):**
    *   Utilizza il modello `best.pt` (YOLOv8 Detection) per individuare Lettere e Cognitive Targets.
    *   **Priorità:** Lettere Greche (`PHI`=3, `PSI`=2, `OMEGA`=1) > `COGNITIVE` > `FAKE`/`null`.
    *   **Analisi Matematica:** Se YOLO rileva un `COGNITIVE`, esegue il campionamento ellittico a 5 anelli sul ritaglio (crop) per compensare la distorsione fish-eye della lente Innomaker.

---

## 3. LOGICA DELLO SLAVE (Arduino Mega)

1.  **Calibrazione Interattiva (EEPROM):**
    *   All'accensione, se viene premuto il tasto START, entra in modalità calibrazione colori.
    *   Salva i valori RGB di Bianco, Grigio, Rosso, Blu e Nero nella EEPROM per adattarsi alle luci di gara.

2.  **Controllo PID e Anti-Drift:**
    *   **Rotazione:** Utilizza un regolatore PID sull'angolo Yaw del giroscopio MPU6050 per rotazioni precise.
    *   **Avanzamento:** Utilizza i 6 sensori ToF per mantenere il robot parallelo ai muri (Anti-Drift) e centrato nella corsia.

3.  **Gestione Rampe (Pitch Control):**
    *   Monitora l'angolo di Pitch. Se `abs(Pitch) > 15°`:
        *   **Salita:** Aumenta il PWM (Boost) per vincere la gravità.
        *   **Discesa:** Riduce il PWM (Freno) per mantenere il controllo.
    *   Continua il movimento ignorando il limite dei 30cm finché il robot non torna in piano (`abs(Pitch) < 5°`).

4.  **Soccorso Fisico:**
---

## 4. SPECIFICHE FISICHE E RICALCOLO SOGLIE (Versione 22cm)
A causa dell'installazione del sensore di colore frontale, la lunghezza del robot è passata da 17cm a **22cm**. 

**Ricalcolo distanze al centro cella (30x30cm):**
*   **Distanza Centro-Bordo:** 11.0 cm (22 / 2).
*   **Distanza ToF -> Muro:** 4.0 cm (15 - 11).
*   **TARGET_FRONT_DIST:** Impostata a **4.0 cm** per l'arresto al centro della cella.
*   **SOGLIA_MURO_FRONT:** Ridotta a **9.0 cm** per evitare falsi rilevamenti di muri distanti.

**Posizionamento Sensore Colore:**
*   Il sensore è montato sul fronte (11cm dal centro). Quando rileva il Nero (inizio cella successiva), il centro del robot si trova a circa 19cm dall'inizio della cella corrente. La logica di rientro automatico (`moveCounter * 20ms`) rimane valida per riportare il robot al centro della cella precedente.
