"""
RoboCup Rescue Maze 2026 - Raspberry Pi VERSION 6.0
Con Watchdog + Heartbeat + Cognitive Targets + Blue Tiles + LED Victim Signal

CHANGELOG V6:
✅ FIX BUG CRITICO: gestione blue_tiles_visited (set locale)
✅ MIGLIORAMENTO: soglie HSV più robuste per colori cognitive targets
✅ MIGLIORAMENTO: gestione recovery movimento fallito
✅ OTTIMIZZAZIONE: camera release dopo ogni frame
"""

import serial
import time
import cv2
import numpy as np
import pytesseract
import threading
from collections import deque

# =================== CONFIGURAZIONE ===================

SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200
CAMERA_INDEX = 0

# Codici comunicazione
CODE_NESSUNA = 0
CODE_HARMED = 1       # Phi O Somma=2
CODE_STABLE = 2       # Psi O Somma=1
CODE_UNHARMED = 3     # Omega O Somma=0
CODE_FALSO_POSITIVO = 4  # Cognitive target con somma invalida
CODE_BLUE_TILE = 5    # Cella blu rilevata

DIR_TERMINA = 0
DIR_DRITTO = 1
DIR_DESTRA = 2
DIR_SINISTRA = 3
DIR_DIETRO = 4

# Cognitive targets: colori → valori
COLOR_VALUES = {
    'black': -2,
    'red': -1,
    'yellow': 0,
    'green': 1,
    'blue': 2,
    'unknown': 0
}

# Somma → Codice vittima
SCORE_TO_CODE = {
    2: CODE_HARMED,
    1: CODE_STABLE,
    0: CODE_UNHARMED
}

# ========== MIGLIORAMENTO V6: SOGLIE HSV PIÙ ROBUSTE ==========
# Valori ottimizzati per illuminazione variabile (campo ufficiale)

COLOR_HSV_RANGES = {
    'black': ([0, 0, 0], [180, 255, 40]),         # V basso
    'red1': ([0, 100, 50], [10, 255, 255]),       # Rosso inferiore
    'red2': ([170, 100, 50], [180, 255, 255]),    # Rosso superiore
    'yellow': ([20, 80, 80], [35, 255, 255]),     # Giallo
    'green': ([40, 60, 50], [80, 255, 255]),      # Verde
    'blue': ([90, 80, 50], [130, 255, 255])       # Blu
}

# =================== HEARTBEAT MONITOR ===================

class HeartbeatMonitor(threading.Thread):
    def __init__(self, serial_conn):
        super().__init__()
        self.serial_conn = serial_conn
        self.last_received = time.time()
        self.timeout = 5  # secondi
        self.running = True
        self.daemon = True

    def run(self):
        print("[HEARTBEAT] Thread monitoraggio avviato")
        while self.running:
            elapsed = time.time() - self.last_received

            if elapsed > self.timeout:
                print(f"[CRITICAL] Arduino non risponde da {elapsed:.1f}s! Recovery...")
                self.recovery()

            # Invia heartbeat a Arduino
            try:
                self.serial_conn.write(b"HB\n")
            except:
                print("[ERROR] Impossibile inviare heartbeat")

            time.sleep(1)

    def update(self):
        """Chiamata quando riceve HB da Arduino"""
        self.last_received = time.time()

    def recovery(self):
        """Tentativo di recovery comunicazione"""
        print("[RECOVERY] Tentativo reset seriale...")
        try:
            self.serial_conn.close()
            time.sleep(1)
            self.serial_conn.open()
            self.last_received = time.time()
            print("[RECOVERY] Seriale ripristinata")
        except Exception as e:
            print(f"[RECOVERY] Fallito: {e}")

    def stop(self):
        self.running = False

# =================== COMUNICAZIONE SERIALE ===================

def init_serial():
    """Inizializza connessione seriale con Arduino"""
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)  # Attesa stabilizzazione
        print(f"[OK] Seriale aperta su {SERIAL_PORT} @ {BAUD_RATE}")
        return ser
    except Exception as e:
        print(f"[ERROR] Impossibile aprire seriale: {e}")
        return None

def read_serial(ser, heartbeat_monitor):
    """Legge comando da Arduino, filtrando heartbeat"""
    try:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8').strip()

            # Filtra heartbeat
            if line == "HB":
                heartbeat_monitor.update()
                return None

            if line:
                print(f"[← Arduino] {line}")
                return line
    except Exception as e:
        print(f"[ERROR] Lettura seriale: {e}")

    return None

def send_to_arduino(ser, value):
    """Invia valore intero ad Arduino"""
    try:
        ser.write(f"{value}\n".encode('utf-8'))
        print(f"[→ Arduino] {value}")
    except Exception as e:
        print(f"[ERROR] Invio seriale: {e}")

# =================== COMPUTER VISION - COGNITIVE TARGETS ===================

def rilevaCognitiveTarget(frame):
    """
    Rileva cognitive target (cerchi concentrici 5 colori)
    Ritorna: codice vittima (0-4)
    """
    height, width = frame.shape[:2]
    center_x, center_y = width // 2, height // 2

    # Definisci 5 anelli concentrici (da esterno a interno)
    radii = [120, 90, 60, 40, 20]
    colors = []

    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    for radius in radii:
        # Estrai anello
        mask = np.zeros((height, width), dtype=np.uint8)
        cv2.circle(mask, (center_x, center_y), radius, 255, -1)
        if radius < radii[0]:
            cv2.circle(mask, (center_x, center_y), radii[radii.index(radius) + 1], 0, -1)

        ring_hsv = cv2.bitwise_and(hsv, hsv, mask=mask)
        color = classifyColor(ring_hsv, mask)
        colors.append(color)

    print(f"[CV] Colori rilevati (est→int): {colors}")

    # Calcola somma
    score = sum(COLOR_VALUES.get(c, 0) for c in colors)
    print(f"[CV] Somma calcolata: {score}")

    # Controlla se è vittima valida
    if score in SCORE_TO_CODE:
        victim_code = SCORE_TO_CODE[score]
        victim_types = {CODE_HARMED: "HARMED", CODE_STABLE: "STABLE", CODE_UNHARMED: "UNHARMED"}
        print(f"[CV] VITTIMA {victim_types[victim_code]} rilevata!")
        return victim_code
    else:
        print(f"[CV] Somma non valida ({score}) → FALSO POSITIVO")
        return CODE_FALSO_POSITIVO

def classifyColor(hsv_ring, mask):
    """
    Classifica colore dell'anello usando soglie HSV ottimizzate V6
    """
    # Estrai pixel validi (non neri/mascherati)
    valid_pixels = cv2.countNonZero(mask)
    if valid_pixels == 0:
        return 'unknown'

    # Test per ogni colore
    for color_name, (lower, upper) in COLOR_HSV_RANGES.items():
        lower_np = np.array(lower)
        upper_np = np.array(upper)
        color_mask = cv2.inRange(hsv_ring, lower_np, upper_np)

        match_pixels = cv2.countNonZero(color_mask)
        match_ratio = match_pixels / valid_pixels

        # Threshold di confidenza
        if match_ratio > 0.4:  # Almeno 40% dei pixel
            # Gestione rosso (due range)
            if color_name == 'red1' or color_name == 'red2':
                return 'red'
            return color_name

    # Default: colore sconosciuto
    return 'unknown'

def rilevaVittima(ser, heartbeat_monitor):
    """
    Acquisisce frame, rileva cognitive target, invia codice ad Arduino
    """
    print("[CV] Acquisizione frame in corso...")

    cap = cv2.VideoCapture(CAMERA_INDEX)
    if not cap.isOpened():
        print("[ERROR] Impossibile aprire camera")
        send_to_arduino(ser, CODE_NESSUNA)
        return

    # Leggi frame
    ret, frame = cap.read()
    cap.release()  # ✅ V6: Rilascio camera dopo uso

    if not ret:
        print("[ERROR] Impossibile acquisire frame")
        send_to_arduino(ser, CODE_NESSUNA)
        return

    # Analizza frame
    code = rilevaCognitiveTarget(frame)

    # Invia codice ad Arduino
    send_to_arduino(ser, code)

# =================== MAPPA & PATHFINDING ===================

class MazeMap:
    def __init__(self):
        self.grid = {}  # {(x, y): {'walls': {DIR}, 'visited': bool, 'blue': bool}}
        self.current_pos = (0, 0)
        self.current_dir = 0  # 0=Nord, 1=Est, 2=Sud, 3=Ovest
        self.blue_tiles_visited = set()  # ✅ FIX V6: set locale

    def addCell(self, pos, walls, is_blue=False):
        """Aggiunge cella alla mappa"""
        if pos not in self.grid:
            self.grid[pos] = {'walls': walls, 'visited': False, 'blue': is_blue}
        else:
            # Aggiorna muri
            self.grid[pos]['walls'] = self.grid[pos]['walls'].union(walls)
            if is_blue:
                self.grid[pos]['blue'] = True

    def markVisited(self, pos):
        """Marca cella come visitata"""
        if pos in self.grid:
            self.grid[pos]['visited'] = True

    def isBlueVisited(self, pos):
        """✅ FIX V6: Controlla se cella blu già visitata"""
        return pos in self.blue_tiles_visited

    def markBlueVisited(self, pos):
        """✅ FIX V6: Marca cella blu come visitata (blacklist)"""
        self.blue_tiles_visited.add(pos)
        print(f"[MAP] Cella blu {pos} aggiunta a blacklist")

    def getNeighbor(self, pos, direction):
        """Calcola cella vicina in direzione"""
        x, y = pos
        if direction == 0:    # Nord
            return (x, y + 1)
        elif direction == 1:  # Est
            return (x + 1, y)
        elif direction == 2:  # Sud
            return (x, y - 1)
        elif direction == 3:  # Ovest
            return (x - 1, y)

    def bfs(self):
        """
        BFS per trovare cella non visitata più vicina
        ✅ V6: esclude celle blu già visitate
        Ritorna: lista di direzioni [DIR]
        """
        queue = deque([(self.current_pos, [])])
        visited_bfs = {self.current_pos}

        while queue:
            pos, path = queue.popleft()

            # Cella non visitata trovata?
            if not self.grid.get(pos, {}).get('visited', False):
                # ✅ V6: esclude celle blu già visitate
                if self.grid.get(pos, {}).get('blue', False) and self.isBlueVisited(pos):
                    print(f"[BFS] Cella blu {pos} saltata (già visitata)")
                    continue

                print(f"[BFS] Cella target: {pos}, path: {path}")
                return path

            # Esplora vicini
            for direction in range(4):
                # Controlla muri
                if direction in self.grid.get(pos, {}).get('walls', set()):
                    continue

                neighbor = self.getNeighbor(pos, direction)

                if neighbor not in visited_bfs:
                    visited_bfs.add(neighbor)
                    queue.append((neighbor, path + [direction]))

        # Nessuna cella non visitata
        print("[BFS] Nessuna cella non visitata trovata")
        return None

    def calculateDirection(self, target_dir):
        """
        Calcola comando movimento per raggiungere direzione target
        Ritorna: DIR_DRITTO / DIR_DESTRA / DIR_SINISTRA / DIR_DIETRO
        """
        delta = (target_dir - self.current_dir) % 4

        if delta == 0:
            return DIR_DRITTO
        elif delta == 1:
            return DIR_DESTRA
        elif delta == 2:
            return DIR_DIETRO
        elif delta == 3:
            return DIR_SINISTRA

# =================== SCANSIONE & MOVIMENTO ===================

def scanWalls(ser, heartbeat_monitor):
    """
    Riceve stato muri da Arduino (front, right, left)
    Ritorna: set di direzioni con muri
    """
    walls = set()

    # Ricevi 3 valori: front, right, left
    for direction_name, offset in [("front", 0), ("right", 1), ("left", 3)]:
        while True:
            line = read_serial(ser, heartbeat_monitor)
            if line is not None:
                try:
                    wall = int(line)
                    if wall == 1:
                        absolute_dir = (maze.current_dir + offset) % 4
                        walls.add(absolute_dir)
                        print(f"[SCAN] Muro a {direction_name} (dir {absolute_dir})")
                    break
                except ValueError:
                    pass

    return walls

def moveRobot(ser, heartbeat_monitor, direction_cmd):
    """
    Invia comando movimento ad Arduino, attende conferma
    ✅ V6: gestione recovery movimento fallito
    Ritorna: True se successo, False se fallito
    """
    print(f"[MOVE] Invio direzione: {direction_cmd}")
    send_to_arduino(ser, direction_cmd)

    # Attende conferma movimento
    while True:
        line = read_serial(ser, heartbeat_monitor)
        if line is not None:
            try:
                success = int(line)
                if success == 1:
                    print("[MOVE] Movimento completato con successo")
                    return True
                else:
                    print("[MOVE] ⚠️  Movimento FALLITO (ostacolo/pendenza/cella vietata)")
                    return False  # ✅ V6: ritorna False invece di ignorare
            except ValueError:
                pass

# =================== LOOP PRINCIPALE ===================

def main():
    global maze

    print("=== RoboCup Rescue Maze 2026 - Raspberry Pi V6 ===")

    # Inizializza seriale
    ser = init_serial()
    if ser is None:
        print("[FATAL] Impossibile avviare sistema senza seriale")
        return

    # Avvia heartbeat monitor
    heartbeat = HeartbeatMonitor(ser)
    heartbeat.start()

    # Inizializza mappa
    maze = MazeMap()

    print("[INFO] Sistema pronto. Avvio esplorazione...")

    iteration = 0
    max_iterations = 200  # Limite cicli per evitare loop infiniti

    while iteration < max_iterations:
        iteration += 1
        print(f"\n{'='*50}")
        print(f"[LOOP] Iterazione {iteration} - Posizione: {maze.current_pos}, Direzione: {maze.current_dir}")

        # 1. SCANSIONE MURI
        walls = scanWalls(ser, heartbeat)
        maze.addCell(maze.current_pos, walls)

        # 2. RILEVAMENTO VITTIMA
        rilevaVittima(ser, heartbeat)

        # 3. MARCA CELLA VISITATA
        maze.markVisited(maze.current_pos)

        # 4. BFS per prossima cella
        path = maze.bfs()

        if path is None:
            print("[END] Esplorazione completata!")
            send_to_arduino(ser, DIR_TERMINA)
            break

        # 5. MOVIMENTO
        next_dir = path[0]
        cmd = maze.calculateDirection(next_dir)

        # ✅ V6: gestione movimento fallito
        success = moveRobot(ser, heartbeat, cmd)

        if success:
            # Aggiorna posizione solo se movimento riuscito
            old_pos = maze.current_pos
            maze.current_pos = maze.getNeighbor(maze.current_pos, next_dir)
            maze.current_dir = next_dir

            print(f"[MAP] Posizione aggiornata: {old_pos} → {maze.current_pos}")

            # Controlla se cella blu
            if maze.grid.get(maze.current_pos, {}).get('blue', False):
                print("[MAP] ⚠️  Rilevata cella BLU")
                maze.markBlueVisited(maze.current_pos)  # ✅ V6: blacklist

        else:
            # Movimento fallito: marca direzione come muro virtuale
            print("[RECOVERY] Aggiunta muro virtuale per blocco movimento")
            maze.addCell(maze.current_pos, {next_dir})
            # Non aggiorna posizione, ripete BFS nella prossima iterazione

    # Fine esplorazione
    heartbeat.stop()
    ser.close()
    print("[INFO] Sistema terminato correttamente")

if __name__ == "__main__":
    main()
