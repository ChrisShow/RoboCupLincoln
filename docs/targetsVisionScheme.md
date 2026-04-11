# Pipeline Definitiva di Rilevamento Target
**Progetto:** RoboCup Junior Rescue Maze 2026

**Hardware Dedicato:** Raspberry Pi 4B + 2x Innomaker U20CAM 1080p

**Obiettivo:** Rilevamento robusto e classificazione di Lettere (Φ, Ψ, Ω) e Bersagli Cognitivi.

---

## I BERSAGLI (TARGETS) DA RILEVARE SECONDO IL REGOLAMENTO 2026

I target sono posizionati sui muri, a circa 7 cm dal pavimento. L'algoritmo non deve cercarli su muri che si affacciano su celle nere, argento, blu, rosse o con ostacoli/dossi/scale. 
Esistono due macro-categorie di target visivi, oltre ai "falsi positivi" (oggetti o configurazioni errate che il robot deve ignorare).

### 1. Letter Targets (Vittime a Lettera)
Sono lettere greche maiuscole stampate in colore nero, utilizzando un font sans-serif (come 'Arial'), con un'altezza fissa di 4 cm. Possono presentarsi anche ruotate.
Rappresentano lo stato di salute della vittima:
* **Harmed victim (Grave):** Lettera **Φ (Phi)**. Mappato sul nostro robot come `TIPO=3`.
* **Stable victim (Stabile):** Lettera **Ψ (Psi)**. Mappato sul nostro robot come `TIPO=2`.
* **Unharmed victim (Illesa):** Lettera **Ω (Omega)**. Mappato sul nostro robot come `TIPO=1`.

### 2. Cognitive Targets (Bersagli Cognitivi a Cerchi Concentrici)
Hanno la forma di un cerchio con diametro massimo di 5 cm, costituito da un massimo di 5 anelli concentrici. Il cerchio più interno ha diametro 1 cm, e ogni anello successivo aumenta di 1 cm (fino a 5 cm).
Ogni colore corrisponde a un valore matematico specifico:
* **Nero** = -2
* **Rosso** = -1
* **Giallo** = 0
* **Verde** = 1
* **Blu** = 2

Lo stato di salute si calcola **sommando i valori dei 5 anelli**. Il robot deve analizzare rigorosamente ogni anello separatamente e sommarne il valore, anche se i colori si ripetono in anelli adiacenti. 
In base alla somma si determina la gravità della vittima:
* **Harmed (Grave):** Somma uguale a **2**. (`TIPO=3`)
* **Stable (Stabile):** Somma uguale a **1**. (`TIPO=2`)
* **Unharmed (Illesa):** Somma uguale a **0**. (`TIPO=1`)

*(Nota sui Fake: Qualsiasi lettera diversa da Φ, Ψ, Ω, oppure un cognitive target la cui somma dia un risultato diverso da 0, 1 o 2, è considerato un falso bersaglio e deve essere ignorato, ovvero `TIPO=0`)*.

---

## FASE 1: STABILIZZAZIONE MECCANICA E ACQUISIZIONE
La più potente rete neurale fallirà se l'immagine di partenza è mossa o sovraesposta. L'acquisizione deve essere un rito rigoroso.

1. **Arresto Assoluto:** Il robot deve essere completamente fermo. L'elaborazione visiva non deve mai avvenire mentre i motori sono in funzione per evitare il "motion blur".
2. **Allineamento Ortogonale:** Se il target è frontale, l'Arduino ruota il robot di 90° per posizionare una delle telecamere laterali perfettamente parallela al muro, azzerando la distorsione prospettica.
3. **Svuotamento del Buffer Hardware:** Il sistema scarta i primi 5-10 fotogrammi letti dal sensore per assicurarsi di catturare l'immagine dell'istante presente.
4. **Assestamento Ottico (AWB e Autofocus):** Pausa di almeno 1 o 2 secondi prima dello scatto effettivo per bilanciare il bianco e regolare il fuoco.
5. **Scatto ad Alta Risoluzione:** Cattura forzata nativamente a 1080p (1920x1080) per garantire che i dettagli millimetrici dei cerchi concentrici non vadano persi.

---

## FASE 2: PRE-ELABORAZIONE SPAZIALE E BIFORCAZIONE
1. **Definizione della ROI (Region Of Interest):** Si taglia via brutalmente il 30% inferiore e superiore dell'immagine, isolando la fascia orizzontale del muro in cui possono trovarsi i target (circa a 7 cm da terra).
2. **Biforcazione del Flusso Logico:** Da qui, l'immagine prende due strade in OpenCV:
   * **Percorso A (Lettere - Forma):** L'immagine viene convertita in scala di grigi e binarizzata in modo adattivo (per far risaltare l'inchiostro nero sul muro bianco indipendentemente dalle ombre).
   * **Percorso B (Cognitive - Colore):** L'immagine viene convertita nello spazio colore HSV (Hue, Saturation, Value), molto più robusto del classico RGB per isolare i colori netti richiesti dal regolamento in condizioni di luce variabile.

---

## FASE 3: ESTRAZIONE ED ELABORAZIONE
A questo punto il sistema deve analizzare matematicamente cosa ha davanti.

### Analisi Lettere Greche (Percorso A)
1. **Ricerca Topologica:** Si cercano i contorni (contours) sull'immagine binarizzata.
2. **Filtraggio Geometrico:** Si scartano i contorni troppo grandi o troppo piccoli rispetto ai 4 cm regolamentari. 
3. **Inferenza del Modello:** Il contorno isolato ("quadratino") viene passato alla rete neurale (addestrata con il tuo dataset). La rete genererà un punteggio di confidenza. Se la rete decreta che l'oggetto è una "Φ" con il 90% di certezza, il target viene confermato come *Harmed*. Se decreta che è un "Fake" (es. una lettera errata o un graffio), il bersaglio viene ignorato.

### Analisi Cognitive Targets (Percorso B)
1. **Ricerca Cerchi (Hough Circle Transform):** OpenCV analizza l'immagine cercando forme perfettamente circolari (escludendo ombre quadrate o crepe).
2. **Estrazione Anelli:** Una volta trovato il cerchio esterno da ~5 cm, il codice crea 5 maschere circolari dal centro verso l'esterno, con raggio incrementale.
3. **Campionamento Colore e Mappatura:** Per ognuno dei 5 anelli isolati, si calcola il colore dominante in formato HSV e lo si confronta con le soglie pre-impostate (Nero, Rosso, Giallo, Verde, Blu). 
4. **Calcolo Matematico:** Il sistema assegna a ogni anello il suo valore numerico (da -2 a +2) ed esegue l'operazione di somma algebrica. Se la somma è 0, 1 o 2, il bersaglio è confermato, altrimenti è classificato come "Fake" (Falso positivo).

---

## FASE 4: DELIBERAZIONE E COMUNICAZIONE LOGICA
L'immagine è stata analizzata e il target è stato confermato (o come Lettera o come Cognitive). 

1. **Mappatura Intero:** * Vittima Grave (Lettera Φ oppure Somma=2) -> `TIPO = 3`
   * Vittima Stabile (Lettera Ψ oppure Somma=1) -> `TIPO = 2`
   * Vittima Illesa (Lettera Ω oppure Somma=0) -> `TIPO = 1`
   * Fake/Vuoto -> `TIPO = 0` (Nessuna erogazione necessaria).
2. **Determinazione Posizione:** In base alla telecamera usata o alla manovra (Destra = `R`, Sinistra = `L`, Frontale = `F`), si stabilisce il parametro `POS`.
3. **Invio Comando Seriale:** Il Raspberry confeziona il comando stringa e lo invia all'Arduino (es: `<VIC:L,3>` se è stata trovata una lettera Φ sul muro sinistro).
4. **Attesa Soccorso:** Il Raspberry attende in loop la risposta `<VIC:DONE>` dall'Arduino (che conferma l'avvenuto sgancio del medikit) prima di riprendere a scattare foto o a calcolare il percorso.