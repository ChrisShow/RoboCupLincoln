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
                    if (nx, ny) in self.map and (nx, ny) not in seen:
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

    def scan_wall(self, pos, frame):
        """
        Analizza un frame con YOLO.
        Ritorna lista di tuple (tipo_kit, posizione) per le vittime trovate.
        tipo_kit: 1=Illesa(Omega), 2=Stabile(Psi), 3=Grave(Phi), 0=Fake/nessuno
        """
        if self.model is None or frame is None:
            return []

        # Aggiunge padding verticale per rendere il frame più quadrato (migliora YOLO)
        frame_quadrato = cv2.copyMakeBorder(
            frame, 80, 80, 0, 0,
            cv2.BORDER_CONSTANT, value=[0, 0, 0]
        )

        # fix #6: soglia confidenza minima passata direttamente a YOLO
        results = self.model(frame_quadrato, imgsz=640, verbose=False, conf=VISION_CONF_THRESHOLD)
        boxes = results[0].boxes
        names = results[0].names

        target_scelto = None
        max_conf = 0.0

        for box in boxes:
            cls_name = names[int(box.cls[0])]
            conf = float(box.conf[0])
            if cls_name in self.mappa_tipi and conf > max_conf:
                max_conf = conf
                target_scelto = cls_name

        if target_scelto:
            tipo = self.mappa_tipi[target_scelto]
            print(f"[VISION] Rilevato {target_scelto} su parete {pos} — conf {max_conf*100:.1f}% — TIPO={tipo}")
            if SAVE_FOR_TRAINING and SAVE_DIR:
                ts = int(time.time() * 1000)
                cv2.imwrite(os.path.join(SAVE_DIR, f"{ts}_{pos}_{target_scelto}.jpg"), frame)
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
                if (nx, ny) not in seen:
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
            print(f"[NAV] ERRORE: target non adiacente {curr} -> {target}")
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
        print(f"[NAV] Esplorazione completata! Ritorno a casa via {len(robot.dijkstra_path)} celle.")
        return get_next_move(robot)

    if curr == (0, 0):
        print("[NAV] Missione Compiuta! Robot alla cella di partenza.")
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
    # Svuota buffer seriale iniziale
    while comm.receive():
        pass

    print("[SYSTEM] In attesa di <INIT> da Arduino...")

    while True:
        msg = comm.receive()
        if not msg:
            time.sleep(0.01)
            continue

        print(f"RX: {msg}")

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
            print("[NAV] Gara Iniziata. Mappa resettata.")

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

            # Scrivi i muri in coordinate assolute
            cell.walls[robot.orientation]           = (f == 1)  # Muro davanti
            cell.walls[(robot.orientation + 1) % 4] = (r == 1)  # Muro a destra
            cell.walls[(robot.orientation + 3) % 4] = (l == 1)  # Muro a sinistra
            # Direzione da cui siamo arrivati: sicuramente non c'è muro
            cell.walls[(robot.orientation + 2) % 4] = False

            # fix #2: gestione colori speciali
            if c == 3:  # BLU → attendi 5 secondi (regolamento)
                print(f"[NAV] Cella BLU a {curr} — invio WAIT:5")
                comm.send("<WAIT:5>")
                time.sleep(5.5)
            elif c == 1:  # GRIGIO → checkpoint di gara
                robot.checkpoints.add(curr)
                print(f"[NAV] Checkpoint GRIGIO a {curr}")
            elif c == 2:  # ROSSO → checkpoint di gara (danger zone)
                robot.checkpoints.add(curr)
                print(f"[NAV] Checkpoint ROSSO a {curr}")

            # fix #3 + fix #7: svuota buffer camere e lascia stabilizzare AWB
            flush_cameras(cap_sx, cap_dx, n=8)
            time.sleep(CAMERA_SETTLE_TIME)

            # Acquisisci frame laterali
            _, frame_r = cap_dx.read()
            _, frame_l = cap_sx.read()

            # Scansione vittime
            vittime = []
            if r == 1:
                vittime.extend(vision.scan_wall('R', frame_r))
            if l == 1:
                vittime.extend(vision.scan_wall('L', frame_l))

            # Scansione muro frontale: Arduino ruota 90° per portarlo di lato
            if f == 1:
                comm.send("<SCAN:FRONT>")
                while True:
                    m = comm.receive()
                    if m == "<SCAN:OK>":
                        break
                    time.sleep(0.01)
                # Svuota buffer e stabilizza dopo la rotazione
                flush_cameras(cap_dx, n=5)
                time.sleep(CAMERA_SETTLE_TIME)
                _, frame_f = cap_dx.read()
                vittime.extend(vision.scan_wall('F', frame_f))
                comm.send("<SCAN:DONE>")

            # Invia comandi di soccorso all'Arduino
            for v_tipo, v_pos in vittime:
                comm.send(f"<VIC:{v_pos},{v_tipo}>")

            # Segnala fine lista vittime; Arduino risponderà con <VIC:DONE>
            comm.send("<VIC:DONE>")
            while True:
                m = comm.receive()
                if m == "<VIC:DONE>":
                    break
                time.sleep(0.01)

            # Calcola e invia prossima mossa
            rel_dir, target = get_next_move(robot)
            if rel_dir is not None:
                robot.last_move = rel_dir
                comm.send(f"<GO:{rel_dir}>")
                print(f"[NAV] ({robot.x},{robot.y}) ori={robot.orientation} → GO:{rel_dir} verso {target}")
            else:
                print("[NAV] Nessuna mossa disponibile — missione completata.")

        # ------------------------------------------------------------------ #
        #  MOVE:OK — Avanzamento normale completato
        # ------------------------------------------------------------------ #
        elif msg == "<MOVE:OK>":
            robot.update_position(steps=1)
            print(f"[NAV] Pos: ({robot.x}, {robot.y})  Ori: {robot.orientation}")

        # ------------------------------------------------------------------ #
        #  MOVE:RAMP — Rampa superata (avanza 2 celle)
        # ------------------------------------------------------------------ #
        elif msg == "<MOVE:RAMP>":
            robot.update_position(steps=2)
            print(f"[NAV] RAMPA superata! Pos: ({robot.x}, {robot.y})")

        # ------------------------------------------------------------------ #
        #  MOVE:BLACK — Cella nera rilevata, robot tornato indietro (fix #1)
        # ------------------------------------------------------------------ #
        elif msg == "<MOVE:BLACK>":
            curr = (robot.x, robot.y)
            # Marca la direzione tentata come inaccessibile (tratta come muro)
            abs_d = (robot.orientation + robot.last_move) % 4
            if curr in robot.map:
                robot.map[curr].walls[abs_d] = True
                print(f"[NAV] Cella NERA in dir {abs_d} da {curr} — marcata come muro")
            # Se avevamo pushato questa cella sullo stack per esplorare la nera, rimuovila
            if robot.stack and robot.stack[-1] == curr:
                robot.stack.pop()
            # Arduino ha già chiamato scanSensors() → arriverà un nuovo <DATA:>

        # ------------------------------------------------------------------ #
        #  LOP — Lack of Progress: il giudice ha spostato il robot (fix migl. #2)
        # ------------------------------------------------------------------ #
        elif msg == "<LOP>":
            print("[NAV] LOP ricevuto! Robot spostato manualmente.")
            # Conserva la mappa ma resetta lo stato di navigazione
            # Il robot verrà posizionato su una cella nota dal giudice
            robot.stack.clear()
            robot.returning_home = False
            robot.dijkstra_path = []
            # Arduino invierà un nuovo <DATA:> per resincronizzarsi
            print("[NAV] Stack DFS resettato. Attendo nuovo <DATA:> per riallinearmi.")


if __name__ == "__main__":
    main()
