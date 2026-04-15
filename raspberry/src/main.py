import serial
import time
import collections
import cv2
from ultralytics import YOLO
import os
import heapq

# --- CONFIGURAZIONE ---
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200
SAVE_FOR_TRAINING = False  # Metti a True per raccogliere immagini per il dataset
SAVE_DIR = os.path.join(os.path.dirname(__file__), 'vision', 'scatti_gara')

if SAVE_FOR_TRAINING and not os.path.exists(SAVE_DIR):
    os.makedirs(SAVE_DIR)
    print(f"[SYSTEM] Cartella salvataggio dataset creata: {SAVE_DIR}")

# Soglia di confidenza minima per accettare un rilevamento YOLO (fix #6)
VISION_CONF_THRESHOLD = 0.65

# Pausa stabilizzazione ottica AWB prima di scattare (fix #7)
CAMERA_SETTLE_TIME = 0.3  # secondi

# Cognitive targets non gestiti intenzionalmente:
# il modello v3 riconosce solo PHI, OMEGA, PSI e FAKE.
# Qualsiasi altra immagine (inclusi i cognitive) viene ignorata (restituisce null).

# Costanti direzioni assolute
NORD, EST, SUD, OVEST = 0, 1, 2, 3
DIR_NOMI = {NORD: "NORD", EST: "EST", SUD: "SUD", OVEST: "OVEST"}
COLORE_NOMI = {0: "NORMALE", 1: "GRIGIO(chk)", 2: "ROSSO(chk)", 3: "BLU(attesa)", 4: "NERO(trap)"}

def log(categoria, msg):
    """Stampa su terminale con timestamp e categoria."""
    ts = time.strftime('%H:%M:%S')
    print(f"[{ts}][{categoria}] {msg}")
DIR_DX = [0, 1, 0, -1]   # Delta X per direzione N/E/S/O
DIR_DY = [1, 0, -1, 0]   # Delta Y per direzione N/E/S/O


class Cell:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.walls = [None, None, None, None]  # 0:N, 1:E, 2:S, 3:O — None=sconosciuto
        self.visited = False
        self.color = 0  # 0=bianco, 1=grigio(checkpoint), 2=rosso(danger), 3=blu(delay), 4=nero(trap)


class Robot:
    def __init__(self):
        self.x = 0
        self.y = 0
        self.orientation = NORD
        self.map = {}
        self.stack = []          # Stack DFS per backtracking
        self.visited = set()
        self.last_move = 0       # Ultimo <GO:X> inviato (direzione relativa)
        self.returning_home = False
        self.dijkstra_path = []
        self.checkpoints = set() # Celle grigie/rosse (checkpoint di gara)
        self.no_transit = set()  # Celle blu: transitabili una volta sola, poi bloccate

    def update_position(self, steps=1):
        """
        Aggiorna orientamento e posizione dopo un movimento confermato.
        steps=1 per MOVE:OK, steps=2 per MOVE:RAMP.
        L'orientamento viene aggiornato UNA SOLA VOLTA indipendentemente dai passi.
        """
        # Aggiorna orientamento: last_move è la direzione relativa inviata ad Arduino
        # (0=dritto → stessa dir, 1=dx → +1, 2=dietro → +2, 3=sx → +3)
        self.orientation = (self.orientation + self.last_move) % 4
        # Aggiorna posizione di 'steps' celle nella direzione corrente
        self.x += DIR_DX[self.orientation] * steps
        self.y += DIR_DY[self.orientation] * steps

    def get_or_create_cell(self, x, y):
        if (x, y) not in self.map:
            self.map[(x, y)] = Cell(x, y)
        return self.map[(x, y)]

    def get_shortest_path(self, start, end):
        """Dijkstra su griglia. Restituisce lista di celle dal nodo start al nodo end."""
        queue = [(0, start, [])]
        seen = set()
        while queue:
            (cost, curr, path) = heapq.heappop(queue)
            if curr in seen:
                continue
            seen.add(curr)
            path = path + [curr]
            if curr == end:
                return path
            cell = self.map.get(curr)
            if not cell:
                continue
            for direction, is_wall in enumerate(cell.walls):
                if is_wall is False:  # False = passabile (non c'è muro)
                    nx = curr[0] + DIR_DX[direction]
                    ny = curr[1] + DIR_DY[direction]
                    if (nx, ny) in self.map and (nx, ny) not in seen and (nx, ny) not in self.no_transit:
                        heapq.heappush(queue, (cost + 1, (nx, ny), path))
        return []


class VisionSystem:
    def __init__(self):
        model_path = os.path.join(
            os.path.dirname(__file__),
            'vision', 'model_v3', 'runs', 'detect', 'train', 'weights', 'best.pt'
        )
        self.model = None
        if os.path.exists(model_path):
            self.model = YOLO(model_path)
            print(f"[VISION] YOLO Modello V3 caricato: {model_path}")
        else:
            print(f"[VISION] ATTENZIONE: modello non trovato in {model_path}")

        # Mappatura classi v3: 0:FAKE, 1:OMEGA, 2:PHI, 3:PSI
        # mappa_tipi: nome_classe → TIPO kit (1=illesa, 2=stabile, 3=grave)
        self.mappa_tipi = {"PHI": 3, "PSI": 2, "OMEGA": 1}

    def scan_wall(self, pos, cap, n_frames=3):
        """
        Cattura n_frames dalla camera, esegue inferenza YOLO su ognuno.
        Ritorna la vittima con la confidenza più alta tra tutti i frame.
        Usare più frame riduce i falsi negativi da motion blur residuo o luce sfavorevole.
        """
        if self.model is None or cap is None:
            return []

        target_scelto = None
        max_conf = 0.0
        best_frame = None

        for i in range(n_frames):
            # Scarta 1 frame stantio tra ogni scatto
            cap.read()
            ret, frame = cap.read()
            if not ret or frame is None:
                continue

            frame_quadrato = cv2.copyMakeBorder(
                frame, 80, 80, 0, 0,
                cv2.BORDER_CONSTANT, value=[0, 0, 0]
            )
            results = self.model(frame_quadrato, imgsz=640, verbose=False,
                                 conf=VISION_CONF_THRESHOLD)

            for box in results[0].boxes:
                cls_name = results[0].names[int(box.cls[0])]
                conf = float(box.conf[0])
                if cls_name in self.mappa_tipi and conf > max_conf:
                    max_conf = conf
                    target_scelto = cls_name
                    best_frame = frame

        if target_scelto:
            tipo = self.mappa_tipi[target_scelto]
            print(f"[VISION] {target_scelto} su parete {pos} — "
                  f"conf {max_conf*100:.1f}% su {n_frames} frame — TIPO={tipo}")
            if SAVE_FOR_TRAINING and SAVE_DIR and best_frame is not None:
                ts = int(time.time() * 1000)
                cv2.imwrite(os.path.join(SAVE_DIR,
                            f"{ts}_{pos}_{target_scelto}.jpg"), best_frame)
            return [(tipo, pos)]

        return []


class Comm:
    def __init__(self):
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
            print(f"[COMM] Seriale aperta su {SERIAL_PORT} a {BAUD_RATE} baud")
        except Exception as e:
            print(f"[COMM] Errore apertura seriale: {e}")
            self.ser = None

    def send(self, msg):
        if self.ser:
            self.ser.write(f"{msg}\n".encode())
            print(f"TX: {msg}")

    def receive(self):
        if self.ser and self.ser.in_waiting > 0:
            return self.ser.readline().decode('utf-8', errors='ignore').strip()
        return None


def flush_cameras(*caps, n=8):
    """Svuota il buffer hardware delle camere scartando n frame. (fix #3)"""
    for _ in range(n):
        for cap in caps:
            cap.read()


OPPOSTO = {0: 2, 1: 3, 2: 0, 3: 1}  # N↔S, E↔W

def propaga_simmetria(robot):
    """
    Per ogni muro noto, scrive il muro speculare nella cella adiacente SE esiste.
    Le celle fuori labirinto non vengono mai create in robot.map → nessun rischio bordi.
    Esempio: cella (3,4) ha muro W → cella (2,4) ha muro E = stesso valore.
    """
    for (x, y), cell in robot.map.items():
        for d, is_wall in enumerate(cell.walls):
            if is_wall is None:
                continue
            nx = x + DIR_DX[d]
            ny = y + DIR_DY[d]
            neighbor = robot.map.get((nx, ny))
            if neighbor is not None and neighbor.walls[OPPOSTO[d]] is None:
                neighbor.walls[OPPOSTO[d]] = is_wall


def find_nearest_unexplored(robot):
    """
    BFS dalla posizione attuale per trovare la cella adiacente conosciuta
    più vicina che non è ancora stata visitata.
    Usato per il backtracking intelligente al posto del semplice stack DFS.
    """
    queue = collections.deque([(robot.x, robot.y)])
    seen = {(robot.x, robot.y)}
    while queue:
        x, y = queue.popleft()
        cell = robot.map.get((x, y))
        if not cell:
            continue
        for d in range(4):
            if cell.walls[d] is False:
                nx = x + DIR_DX[d]
                ny = y + DIR_DY[d]
                if (nx, ny) not in seen and (nx, ny) not in robot.no_transit:
                    seen.add((nx, ny))
                    if (nx, ny) not in robot.visited:
                        return (nx, ny)  # Trovata! È la più vicina.
                    queue.append((nx, ny))
    return None  # Nessuna cella inesplorata raggiungibile


def get_next_move(robot):
    """
    Calcola la prossima mossa del robot.
    Ritorna (rel_dir, target_cell) oppure (None, None) se la missione è completata.

    rel_dir: direzione relativa per Arduino (0=dritto, 1=dx, 2=dietro, 3=sx)
    target_cell: coordinata (x,y) della cella destinazione
    """
    curr = (robot.x, robot.y)
    cell = robot.map.get(curr)
    if not cell:
        return None, None

    # --- FASE RITORNO A CASA ---
    if robot.returning_home:
        if not robot.dijkstra_path:
            return None, None
        target = robot.dijkstra_path.pop(0)
        dx = target[0] - robot.x
        dy = target[1] - robot.y
        abs_dir = {(0, 1): NORD, (1, 0): EST, (0, -1): SUD, (-1, 0): OVEST}.get((dx, dy))
        if abs_dir is None:
            log("ERR", f"Target non adiacente {curr} -> {target} — skip")
            return None, None
        return (abs_dir - robot.orientation) % 4, target

    # --- FASE ESPLORAZIONE ---
    # Priorità: Dritto(0) > Destra(1) > Sinistra(3) — evita oscillazione
    for rel in [0, 1, 3]:
        abs_d = (robot.orientation + rel) % 4
        if cell.walls[abs_d] is False:
            nx = robot.x + DIR_DX[abs_d]
            ny = robot.y + DIR_DY[abs_d]
            if (nx, ny) not in robot.visited:
                robot.stack.append(curr)
                return rel, (nx, ny)

    # --- BACKTRACKING INTELLIGENTE (BFS, non stack DFS semplice) ---
    # Trova la cella inesplorata più vicina raggiungibile via celle già mappate
    nearest = find_nearest_unexplored(robot)
    if nearest:
        path = robot.get_shortest_path(curr, nearest)
        if path and len(path) > 1:
            target = path[1]
            dx = target[0] - robot.x
            dy = target[1] - robot.y
            abs_d = {(0, 1): NORD, (1, 0): EST, (0, -1): SUD, (-1, 0): OVEST}.get((dx, dy))
            if abs_d is not None:
                return (abs_d - robot.orientation) % 4, target

    # --- MISSIONE COMPLETATA: torna a (0,0) con Dijkstra ---
    path = robot.get_shortest_path(curr, (0, 0))
    if path and len(path) > 1:
        robot.returning_home = True
        robot.dijkstra_path = path[1:]
        log("NAV", f"Esplorazione completata! Ritorno a casa via {len(robot.dijkstra_path)} celle.")
        return get_next_move(robot)

    if curr == (0, 0):
        log("NAV", "Missione Compiuta! Robot alla cella di partenza.")
    return None, None


def main():
    comm = Comm()
    vision = VisionSystem()
    robot = Robot()

    # ID Camera: Sinistra=0, Destra=2
    cap_sx = cv2.VideoCapture(0)
    cap_dx = cv2.VideoCapture(2)

    for cap in [cap_sx, cap_dx]:
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    time.sleep(2)
    while comm.receive():
        pass

    log("SYS", "Sistema pronto. In attesa di <INIT> da Arduino...")
    log("SYS", f"Modello YOLO: {'caricato' if vision.model else 'NON TROVATO'}")
    log("SYS", f"Camera SX: {'OK' if cap_sx.isOpened() else 'ERRORE'}")
    log("SYS", f"Camera DX: {'OK' if cap_dx.isOpened() else 'ERRORE'}")
    log("SYS", "Premi il tasto START su Arduino per iniziare.")

    while True:
        msg = comm.receive()
        if not msg:
            time.sleep(0.01)
            continue

        # Messaggi di debug da Arduino (prefisso [D]) — stampa e vai avanti
        if msg.startswith("[D]"):
            log("ARD", msg[4:])
            continue

        log("COM", f"RX: {msg}")

        # ------------------------------------------------------------------ #
        #  INIT — Reset stato al premere del tasto START
        # ------------------------------------------------------------------ #
        if msg == "<INIT>":
            robot.x, robot.y, robot.orientation = 0, 0, NORD
            robot.visited = set()
            robot.map = {}
            robot.stack = []
            robot.returning_home = False
            robot.dijkstra_path = []
            robot.checkpoints = set()
            robot.no_transit = set()
            log("NAV", "="*40)
            log("NAV", "GARA INIZIATA — mappa resettata")
            log("NAV", "Posizione: (0,0)  Orientamento: NORD")
            log("NAV", "="*40)

        # ------------------------------------------------------------------ #
        #  DATA — Ricezione muri + colore, visione, calcolo prossima mossa
        # ------------------------------------------------------------------ #
        elif msg.startswith("<DATA:"):
            # Parsing: <DATA:C,FRL> dove C=colore(0-4), F/R/L=0/1
            inner = msg[6:-1]
            c_str, frl_str = inner.split(',')
            c = int(c_str)
            f, r, l = int(frl_str[0]), int(frl_str[1]), int(frl_str[2])

            curr = (robot.x, robot.y)
            cell = robot.get_or_create_cell(robot.x, robot.y)
            cell.visited = True
            cell.color = c
            robot.visited.add(curr)

            cell.walls[robot.orientation]           = (f == 1)
            cell.walls[(robot.orientation + 1) % 4] = (r == 1)
            cell.walls[(robot.orientation + 3) % 4] = (l == 1)
            cell.walls[(robot.orientation + 2) % 4] = False
            propaga_simmetria(robot)

            muri_str = f"F:{'SI' if f else 'NO'} R:{'SI' if r else 'NO'} L:{'SI' if l else 'NO'}"
            log("NAV", f"--- Cella {curr} | Ori: {DIR_NOMI[robot.orientation]} | {muri_str} | Pavimento: {COLORE_NOMI.get(c,'?')} ---")
            log("NAV", f"Celle esplorate: {len(robot.visited)}  Stack DFS: {len(robot.stack)}")

            if c == 3:
                log("NAV", f"Cella BLU rilevata — attesa 5s obbligatoria, poi bloccata")
                comm.send("<WAIT:5>")
                time.sleep(5.5)
                robot.no_transit.add(curr)
                log("NAV", f"Cella BLU {curr} bloccata — non transitabile in futuro")
            elif c == 1:
                robot.checkpoints.add(curr)
                log("NAV", f"Checkpoint GRIGIO salvato a {curr}  (tot checkpoints: {len(robot.checkpoints)})")
            elif c == 2:
                robot.checkpoints.add(curr)
                log("NAV", f"Checkpoint ROSSO salvato a {curr}  (tot checkpoints: {len(robot.checkpoints)})")

            # Scansione visiva pareti
            pareti_da_scansionare = []
            if r == 1: pareti_da_scansionare.append('R')
            if l == 1: pareti_da_scansionare.append('L')
            if f == 1: pareti_da_scansionare.append('F')

            if pareti_da_scansionare:
                log("VIS", f"Scansione pareti: {', '.join(pareti_da_scansionare)}")
            else:
                log("VIS", "Nessuna parete — skip visione")

            flush_cameras(cap_sx, cap_dx, n=8)
            time.sleep(CAMERA_SETTLE_TIME)

            vittime = []
            if r == 1:
                log("VIS", "Scansione parete DESTRA (3 frame)...")
                vittime.extend(vision.scan_wall('R', cap_dx))
            if l == 1:
                log("VIS", "Scansione parete SINISTRA (3 frame)...")
                vittime.extend(vision.scan_wall('L', cap_sx))

            if f == 1:
                log("VIS", "Scansione parete FRONTALE — richiedo rotazione Arduino")
                comm.send("<SCAN:FRONT>")
                while True:
                    m = comm.receive()
                    if m == "<SCAN:OK>":
                        break
                    if m and m.startswith("[D]"):
                        log("ARD", m[4:])
                    time.sleep(0.01)
                log("VIS", "Rotazione OK — scansione frontale (3 frame)...")
                flush_cameras(cap_dx, n=5)
                time.sleep(CAMERA_SETTLE_TIME)
                vittime.extend(vision.scan_wall('F', cap_dx))
                comm.send("<SCAN:DONE>")

            if vittime:
                log("VIS", f"Vittime trovate: {len(vittime)}")
                for v_tipo, v_pos in vittime:
                    TIPI = {1: "ILLESA(Omega)", 2: "STABILE(Psi)", 3: "GRAVE(Phi)"}
                    log("VIS", f"  → Parete {v_pos}: {TIPI.get(v_tipo, '?')} (tipo {v_tipo})")
            else:
                log("VIS", "Nessuna vittima rilevata")

            for v_tipo, v_pos in vittime:
                comm.send(f"<VIC:{v_pos},{v_tipo}>")

            comm.send("<VIC:DONE>")
            while True:
                m = comm.receive()
                if m == "<VIC:DONE>":
                    break
                if m and m.startswith("[D]"):
                    log("ARD", m[4:])
                time.sleep(0.01)

            if vittime:
                log("VIS", "Soccorso completato da Arduino")

            # Calcola e invia prossima mossa
            DIRS = {0: "AVANTI", 1: "DESTRA", 2: "DIETRO", 3: "SINISTRA"}
            rel_dir, target = get_next_move(robot)
            if rel_dir is not None:
                robot.last_move = rel_dir
                fase = "RITORNO" if robot.returning_home else "ESPLORA"
                log("NAV", f"[{fase}] GO:{DIRS[rel_dir]} → {target}  "
                           f"(celle esplorate: {len(robot.visited)})")
                comm.send(f"<GO:{rel_dir}>")
            else:
                log("NAV", "="*40)
                log("NAV", "MISSIONE COMPLETATA")
                log("NAV", f"Celle totali esplorate: {len(robot.visited)}")
                log("NAV", f"Vittime totali trovate: (vedi log VIS)")
                log("NAV", "="*40)

        # ------------------------------------------------------------------ #
        #  MOVE:OK — Avanzamento normale completato
        # ------------------------------------------------------------------ #
        elif msg == "<MOVE:OK>":
            robot.update_position(steps=1)
            log("NAV", f"Avanzamento OK → pos ({robot.x},{robot.y})  "
                       f"ori {DIR_NOMI[robot.orientation]}")

        # ------------------------------------------------------------------ #
        #  MOVE:RAMP — Rampa superata (avanza 2 celle)
        # ------------------------------------------------------------------ #
        elif msg == "<MOVE:RAMP>":
            robot.update_position(steps=2)
            log("NAV", f"RAMPA superata (+2 celle) → pos ({robot.x},{robot.y})  "
                       f"ori {DIR_NOMI[robot.orientation]}")

        # ------------------------------------------------------------------ #
        #  MOVE:BLACK — Cella nera rilevata, robot tornato indietro
        # ------------------------------------------------------------------ #
        elif msg == "<MOVE:BLACK>":
            curr = (robot.x, robot.y)
            abs_d = (robot.orientation + robot.last_move) % 4
            if curr in robot.map:
                robot.map[curr].walls[abs_d] = True
            if robot.stack and robot.stack[-1] == curr:
                robot.stack.pop()
            log("NAV", f"CELLA NERA bloccata — dir {DIR_NOMI[abs_d]} da {curr} "
                       f"marcata come muro")

        # ------------------------------------------------------------------ #
        #  LOP — Lack of Progress: il giudice ha spostato il robot (fix migl. #2)
        # ------------------------------------------------------------------ #
        elif msg == "<LOP>":
            log("NAV", "!!! LACK OF PROGRESS — robot spostato dal giudice !!!")
            log("NAV", f"Mappa conservata ({len(robot.visited)} celle). Reset stato navigazione.")
            robot.stack.clear()
            robot.returning_home = False
            robot.dijkstra_path = []
            log("NAV", "Stack DFS resettato. Attendo <DATA:> per riallinearmi.")


if __name__ == "__main__":
    main()
