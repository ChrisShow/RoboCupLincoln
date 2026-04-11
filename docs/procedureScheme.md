# Algoritmo Dettagliato di Navigazione FSM
**Progetto:** RoboCup Junior Rescue Maze 2026

**Architettura:** Raspberry Pi 4B (Master/Cervello) + Arduino Mega 2560 (Slave/Riflessi)

**Setup Fisico:**
* Dimensioni scocca nuda robot: 17 cm (lunghezza) x 10 cm (larghezza)
* Dimensioni totali robot (incluse ruote): 17 cm (lunghezza) x 13 cm (larghezza)

**Altre precisazioni:**
* Dimensioni Cella: 30 cm x 30 cm
* Dispenser Medikit sul retro
* Telecamere laterali (Destra e Sinistra)
* Sensori ToF (6 Totali): 1 Fronte centrale, 1 Retro centrale, 2 Fiancata Destra (Anteriore e Posteriore), 2 Fiancata Sinistra (Anteriore e Posteriore).

---

## 1. CALCOLO SOGLIE SENSORI (Posizionamento al Centro Cella)
Essendo il robot 17x10 cm e la cella 30x30 cm, quando il centro del robot coincide con il centro della cella (15 cm dai muri virtuali):
* **Asse Longitudinale (Fronte/Retro):** Distanza centro-bordo robot = 8.5 cm. Il ToF frontale/dorsale leggerà al netto **6.5 cm** (15 - 8.5).
* **Asse Trasversale (Destra/Sinistra):** Distanza centro-bordo robot = 5 cm. I ToF laterali leggeranno al netto **10 cm** (15 - 5).

**Soglie Rilevamento Muro (Tolleranza):**
* Muro Frontale presente se lettura <= **12 cm**.
* Muro Laterale presente se la media dei due sensori (Anteriore + Posteriore) è <= **15 cm**.

---

## 2. PROTOCOLLO DI COMUNICAZIONE SERIALE
La comunicazione avviene tramite pacchetti racchiusi tra `<` e `>`.
* **Arduino -> Pi:** `<INIT>`, `<DATA:C,FRL>`, `<SCAN:OK>`, `<VIC:DONE>`, `<MOVE:OK>`, `<MOVE:BLACK>`, `<LOP>`
* **Pi -> Arduino:** `<WAIT:5>`, `<SCAN:FRONT>`, `<SCAN:DONE>`, `<VIC:POS,TIPO>`, `<GO:DIR>`

---

## 3. FLUSSO DI ESECUZIONE (L'Algoritmo)

**1) Inizializzazione:** Il robot viene posizionato nella cella di START. Il pulsante START sveglia Arduino e fa partire l'algoritmo inviando al Raspberry il pacchetto `<INIT>`.

**2) Scansione Sensoriale:** Arduino attiva il sensore di colore e legge i ToF per mappare i muri:
* ToF Frontale Centrale <= 12 cm -> `F=1`, altrimenti `0`.
* Media ToF Destro (Ant + Post) <= 15 cm -> `R=1`, altrimenti `0`.
* Media ToF Sinistro (Ant + Post) <= 15 cm -> `L=1`, altrimenti `0`.
* Invia a Raspberry il pacchetto `<DATA:C,FRL>` (es: `<DATA:0,101>`). Codici colore: 0=Normale, 1=Grigia (Checkpoint), 2=Rossa (Danger), 3=Blu (Delay).

**3) Memoria Pi:** Raspberry riceve `<DATA:C,FRL>`, posiziona la cella nello Stack (LIFO) al primo posto e:
* a. Se `C=1`: Mette la cella al primo posto nella lista checkpoint visitati.
* b. Se `C=3`: Attende 5 secondi prima di proseguire.
* c. Inserisce i dati dei muri (`FRL`) nelle coordinate (x,y) dell'oggetto Cella corrente.
* d. Rimuove la cella dalla lista delle "da visitare" (se presente).

**4) Analisi Vittime (Visione Laterale e Richiesta Frontale):** Raspberry, consultando i muri appena mappati, attiva le telecamere solo dove fisicamente possibile:
* a. Se `R=1` (Muro Destro): Raspberry attiva la Cam Destra a 1080p, scatta, esegue l'inferenza per cercare Lettere/Colori, e memorizza il risultato in RAM.
* b. Se `L=1` (Muro Sinistro): Raspberry attiva la Cam Sinistra a 1080p, scatta, esegue l'inferenza, e memorizza il risultato.
* c. Se `F=1` (Muro Frontale): Il Raspberry non avendo telecamere frontali invia ad Arduino il pacchetto `<SCAN:FRONT>`.

**5) Allineamento Telecamera Frontale:** Arduino riceve `<SCAN:FRONT>`. 
* Utilizzando il giroscopio MPU6050, fa ruotare il robot sul proprio asse di esattamente **90° in senso antiorario**.
* Adesso la fiancata destra del robot (e quindi la Cam Destra) è perfettamente parallela a quello che prima era il muro frontale. 
* Arduino frena i motori, stabilizza l'assetto e invia a Raspberry `<SCAN:OK>`.

**6) Scatto Frontale:** Raspberry riceve `<SCAN:OK>`, riapre la Cam Destra, scatta la foto a 1080p, esegue l'inferenza, salva il risultato e invia ad Arduino `<SCAN:DONE>`.

**7) Ripristino Allineamento:** Arduino riceve `<SCAN:DONE>` e aziona i motori al contrario, usando il giroscopio per ruotare di **90° in senso orario**, tornando esattamente all'angolo Yaw originale (0°) in cui era entrato nella cella.

**8) Compilazione e Invio Esiti:** Raspberry compila i risultati della visione. Per ogni vittima o target trovato sulle tre pareti, invia ad Arduino un pacchetto nel formato `<VIC:POS,TIPO>`.
* `POS`: Indica la parete della cella originale: `F` (Fronte), `R` (Destra), `L` (Sinistra).
* `TIPO`: Indica la gravità: `1` (Unharmed), `2` (Stable), `3` (Harmed).
* *Nota:* Se i muri non presentavano vittime o vittime fake, Raspberry salta direttamente al passaggio 10.

**9) Erogazione Medikit (Dispenser Posteriore):** Arduino riceve il pacchetto `<VIC:POS,TIPO>` ed esegue la routine fisica di soccorso:
* **Segnalazione:** Fa lampeggiare il LED di stato per 5 secondi (obbligo da regolamento) per notificare agli arbitri il rilevamento, se dalla rilevazione si riceve un codice `1` (Unharmed), allora si può passare direttamente all'ultimo punto del passaggio 9.
* **Manovra di Allineamento Posteriore:** Essendo il dispenser montato sul retro, il robot deve girare la schiena al muro bersaglio.
  * Se `POS=R`: Ruota di 90° in senso **antiorario**.
  * Se `POS=L`: Ruota di 90° in senso **orario**.
  * Se `POS=F`: Ruota di **180°**.
* **Erogazione Fisica:** Arduino aziona il servomotore SG90 inviando il segnale PWM di apertura per far cadere il cubetto, attende 1000ms tramite `millis()`, e invia il PWM di chiusura. Ripete questo ciclo un numero di volte pari al valore `TIPO`.
* **Ripristino:** Arduino esegue la rotazione inversa (basata sul giroscopio) per riportarsi all'angolo di ingresso della cella (0°).
* Terminata la procedura per tutte le vittime, Arduino invia a Raspberry `<VIC:DONE>`.

**10) Pathfinding:** Raspberry calcola la prossima direzione. Ordine di priorità per celle percorribili non visitate: DRITTO, DESTRA, SINISTRA. Se tutte sono visitate, va alla meno recentemente visitata. Invia `<GO:DIR>` (0=Dritto, 1=Destra, 2=Dietro, 3=Sinistra).

**11) Movimento ed Elaborazione Sensoriale Avanzata (ToF x6):** Arduino riceve `<GO:DIR>`, ruota sul proprio asse col giroscopio verso la direzione bersaglio e avvia i motori in avanti. Durante l'avanzamento, il loop di Arduino esegue questi controlli simultanei ogni ~20ms incrementando un contatore locale ad ogni tornata di controlli:

* **FASE A: Allineamento Parallelo (Anti-Drift sulle fiancate):**
  * Arduino legge le coppie di sensori laterali che "vedono" un muro.
  * *Esempio Destra:* Se il robot si sta inclinando, il ToF Destro Anteriore leggerà (es.) 12 cm, mentre il Destro Posteriore leggerà 9 cm. 
  * *Correzione:* Arduino calcola l'errore (`Errore = Destro_Ant - Destro_Post`). Se l'errore è positivo, il muso punta a sinistra; il driver motore aumenta il PWM del lato Sinistro per raddrizzare il robot istantaneamente, finché i due sensori non danno lo stesso valore.

* **FASE B: Centratura di Carreggiata:**
  * Una volta parallelo, Arduino fa la media dei sensori laterali per mantenere il robot perfettamente al centro.
  * L'obiettivo è mantenere la lettura media a **10 cm**. Se il robot è parallelo ma a 7 cm dal muro destro, sposta delicatamente la potenza ai motori per slittare verso sinistra fino ai 10 cm nominali.

* **FASE C: Rilevamento Colore e Trappola Nera:**
  * Il sensore TCS3200 legge in continuo. Se rileva colore nero, scatta l'interrupt software.
  * I motori vengono invertiti. Per rientrare *esattamente* al centro della cella precedente. Per fare ciò si sfrutta il contatore locale precedente che è stato incrementato ogni 20ms quindi si muove alla stessa velocità di prima, con i motori a spinta all'indietro per (20*contatore) ms, ferma i motori e invia `<MOVE:BLACK>`.

* **FASE D: Condizione di Arrivo a 30 cm:**
  Prima di muoversi, Arduino effettua una lettura dal sensore ToF anteriore centrale e dal sensore ToF posteriore centrale:
  - Se la prima lettura risulta < 45 cm allora significa che la cella davanti al robot, nella direzione attuale, avrà un muro di fronte, si procede a velocità costante medio-bassa in avanti finchè il sensore ToF frontale non effettuerà una rilevazione di circa 6,5 cm. 
  - Discorso analogo per la seconda lettura, se essa risulta < 10 allora dietro al robot è presente un muro e bisogna continuare fin quando si raggiunge, a velocità medio-bassa, una misura di 30 cm più grande rispetto a quella presa in partenza
  - Se non si verifica nessuna delle due opzioni allora ci si trova in un corridoio abbastanza lungo pertanto il bot avanzerà a velocità medio-bassa per un totale di secondi atti a percorrere un totale di 30cm.
  
  Per queste fasi continuano a valere, inoltre, i passaggi relativi alla rilevazione del sensore di colore e al contatore locale incrementato ogni 20 ms per tornare indietro in caso di cella trappola. Se la mossa sarà completata, Arduino frenerà e invierà `<MOVE:OK>`.

**12) Aggiornamento Mappa:** Raspberry riceve l'esito:
* a. Se `<MOVE:BLACK>`: Mette la cella nera tra le inaccessibili, ricalcola (Punto 10) e reinvia `<GO:DIR>`.
* b. Se `<MOVE:OK>`: Aggiorna le coordinate (x,y). Se la lista "da esplorare" è vuota, calcola il ritorno allo START. Altrimenti, **riparte dal Punto 2**.

**13) Lack of Progress:** Se il tasto START viene premuto durante la gara, Arduino ferma i motori e invia `<LOP>`. Raspberry imposta le coordinate correnti al primo elemento della lista Checkpoint, azzera la direzione a `0` (Nord/Start) e attende un nuovo pacchetto `<INIT>` (Punto 1).