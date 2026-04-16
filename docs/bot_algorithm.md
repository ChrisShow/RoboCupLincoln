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

## 4. SPECIFICHE FISICHE E SOGLIE (Versione Definitiva 22cm)

**Geometria robot:**
| Componente | Posizione dal centro rotazione |
|---|---|
| Scocca | 17cm × 10cm |
| Front ToF (bordo scocca ant.) | 8.5cm avanti |
| Rear ToF (bordo scocca post.) | 8.5cm dietro |
| Side ToF (bordo scocca lat.) | 5.0cm lateralmente |
| Sensore colore (punta) | 12.5cm avanti (+4cm oltre Front ToF) |
| Medikit (punta) | 9.5cm dietro (+1cm oltre Rear ToF) |
| Ruote (punta) | 8.0cm lateralmente (+3cm oltre Side ToF) |

**Letture ToF al centro cella (celle 30×30cm, centro = 15cm da ogni muro):**
| Sensore | Formula | Valore |
|---|---|---|
| Front ToF → muro frontale | 15 - 8.5 | **6.5 cm** → `TARGET_FRONT_DIST` |
| Side ToF → muro laterale | 15 - 5.0 | **10.0 cm** → `TARGET_LAT_DIST` |
| Sensore colore → muro front. | 15 - 12.5 | **2.5 cm** (clearance sicura) |
| Ruote → muro laterale | 15 - 8.0 | **7.0 cm** (contatto a 3cm dal Side ToF) |

**Soglie di rilevamento:**
*   **SOGLIA_MURO_FRONT = 25.0 cm:** Discrimina muro presente (6.5cm) da apertura (36.5cm+). Usata anche nella rilevazione *dinamica* durante `moveForward()`: quando il ToF frontale scende sotto 25cm durante il moto, il muro della cella successiva è entrato nel range e il robot inizia a tracciarlo fino al punto di stop (6.5cm).
*   **SOGLIA_MURO_LAT = 18.0 cm:** Discrimina muro presente (10cm) da apertura (40cm+).
*   **DIST_PERICOLO_RUOTA = 4.5 cm:** Side ToF a 4.5cm = ruota a 1.5cm dal muro. Contatto avviene a 3.0cm.

**Posizionamento Sensore Colore:**
*   Il sensore è a 12.5cm dal centro rotazione. Raggiunge il bordo della cella successiva quando il centro ha percorso 15 - 12.5 = **2.5cm**. La retromarcia (`moveCounter * 20ms`) riporta il robot al centro della cella precedente.
