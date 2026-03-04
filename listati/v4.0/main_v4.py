"""
RoboCup Rescue Maze 2026 - Raspberry Pi V4
Versione con Watchdog + Heartbeat + Cognitive Targets + Blue Tiles + LED Victim Signal

NOVITÀ V4:
- Blue tiles: visitabili una sola volta, stazionamento 5s, poi blacklist
- LED signal: lampeggio bianco 5s (500ms ON/OFF) prima di rilasciare medikit
- Gestione blacklist celle blu in BFS
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
CODE_HARMED = 1           # Phi O Somma=2
CODE_STABLE = 2           # Psi O Somma=1
CODE_UNHARMED = 3         # Omega O Somma=0
CODE_FALSO_POSITIVO = 4   # Cognitive target con somma invalida
CODE_BLUE_TILE = 5        # NUOVO V4: Cella blu rilevata

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
    print("[CV] Analisi cognitive target...")
    
    # Converti in grayscale e applica blur
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (9, 9), 2)
    
    # Rileva cerchi con HoughCircles
    circles = cv2.HoughCircles(
        blurred,
        cv2.HOUGH_GRADIENT,
        dp=1,
        minDist=30,
        param1=50,
        param2=30,
        minRadius=15,
        maxRadius=60
    )
    
    if circles is None:
        print("[INFO] Nessun cerchio rilevato")
        return CODE_NESSUNA
    
    circles = np.uint16(np.around(circles))
    
    # Trova cerchio più grande (5cm esterno)
    largest_circle = max(circles[0, :], key=lambda c: c[2])
    center_x, center_y, radius_px = largest_circle
    
    print(f"[INFO] Cerchio esterno rilevato: centro=({center_x},{center_y}), raggio={radius_px}px")
    
    # Calcola pixel_per_mm (cerchio esterno = 50mm diametro = 25mm raggio)
    pixel_per_mm = radius_px / 25.0
    
    # Analizza ogni cerchio concentrico (1cm, 2cm, 3cm, 4cm, 5cm)
    total_score = 0
    detected_colors = []
    
    for diameter_mm in [10, 20, 30, 40, 50]:
        radius_mm = diameter_mm / 2.0
        color = rilevaColoreCerchio(frame, center_x, center_y, radius_mm, pixel_per_mm)
        value = COLOR_VALUES.get(color, 0)
        total_score += value
        detected_colors.append(color)
        
        print(f"[DEBUG] Cerchio {diameter_mm}mm: {color.upper()} (valore={value:+d})")
    
    print(f"[INFO] Somma totale: {total_score:+d}")
    print(f"[INFO] Colori rilevati: {detected_colors}")
    
    # Mappa somma a codice vittima
    if total_score in SCORE_TO_CODE:
        code = SCORE_TO_CODE[total_score]
        victim_type = ['HARMED', 'STABLE', 'UNHARMED'][code - 1]
        print(f"[RISULTATO] Cognitive Target: {victim_type} (codice {code})")
        return code
    else:
        print(f"[INFO] Somma {total_score} non corrisponde a vittima (fake target)")
        return CODE_FALSO_POSITIVO

def rilevaColoreCerchio(frame, center_x, center_y, radius_mm, pixel_per_mm):
    """
    Rileva colore dominante in un cerchio specifico
    Ritorna: 'black', 'red', 'yellow', 'green', 'blue', 'unknown'
    """
    # Converti radius da mm a pixel
    radius_px = int(radius_mm * pixel_per_mm)
    
    # Crea maschera circolare
    mask = np.zeros(frame.shape[:2], dtype=np.uint8)
    cv2.circle(mask, (int(center_x), int(center_y)), radius_px, 255, -1)
    
    # Estrai ROI e converti in HSV
    roi = cv2.bitwise_and(frame, frame, mask=mask)
    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
    
    # Conta pixel per ogni colore
    total_pixels = cv2.countNonZero(mask)
    threshold = total_pixels * 0.3  # 30% pixel per validità
    
    # NERO: V < 50
    black_mask = cv2.inRange(hsv, np.array([0, 0, 0]), np.array([180, 255, 50]))
    black_count = cv2.countNonZero(cv2.bitwise_and(black_mask, mask))
    
    # ROSSO: H:[0-10] + [170-180]
    red_mask1 = cv2.inRange(hsv, np.array([0, 100, 100]), np.array([10, 255, 255]))
    red_mask2 = cv2.inRange(hsv, np.array([170, 100, 100]), np.array([180, 255, 255]))
    red_mask = cv2.bitwise_or(red_mask1, red_mask2)
    red_count = cv2.countNonZero(cv2.bitwise_and(red_mask, mask))
    
    # GIALLO: H:[20-30]
    yellow_mask = cv2.inRange(hsv, np.array([20, 100, 100]), np.array([30, 255, 255]))
    yellow_count = cv2.countNonZero(cv2.bitwise_and(yellow_mask, mask))
    
    # VERDE: H:[40-80]
    green_mask = cv2.inRange(hsv, np.array([40, 50, 50]), np.array([80, 255, 255]))
    green_count = cv2.countNonZero(cv2.bitwise_and(green_mask, mask))
    
    # BLU: H:[100-130]
    blue_mask = cv2.inRange(hsv, np.array([100, 100, 100]), np.array([130, 255, 255]))
    blue_count = cv2.countNonZero(cv2.bitwise_and(blue_mask, mask))
    
    color_counts = {
        'black': black_count,
        'red': red_count,
        'yellow': yellow_count,
        'green': green_count,
        'blue': blue_count
    }
    
    # Ritorna colore dominante (>30% pixel)
    for color, count in sorted(color_counts.items(), key=lambda x: x[1], reverse=True):
        if count > threshold:
            return color
    
    return 'unknown'

# =================== OCR - LETTERE GRECHE ===================

def rilevaLetteraGreca(frame):
    """
    Rileva lettere greche Φ/Ψ/Ω con OCR Tesseract
    Ritorna: codice vittima (0-3)
    """
    print("[OCR] Analisi lettere greche...")
    
    # Preprocessing
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    _, thresh = cv2.threshold(gray, 127, 255, cv2.THRESH_BINARY)
    
    # OCR con Tesseract (lingua greca)
    custom_config = r'--oem 3 --psm 6 -c tessedit_char_whitelist=ΦΨΩ'
    text = pytesseract.image_to_string(thresh, lang='ell', config=custom_config)
    text = text.strip()
    
    print(f"[OCR] Testo rilevato: '{text}'")
    
    # Mapping lettera → codice
    if 'Φ' in text or 'φ' in text:
        print("[RISULTATO] Lettera Greca: Φ (Phi) → HARMED")
        return CODE_HARMED
    elif 'Ψ' in text or 'ψ' in text:
        print("[RISULTATO] Lettera Greca: Ψ (Psi) → STABLE")
        return CODE_STABLE
    elif 'Ω' in text or 'ω' in text:
        print("[RISULTATO] Lettera Greca: Ω (Omega) → UNHARMED")
        return CODE_UNHARMED
    else:
        print("[INFO] Nessuna lettera greca rilevata")
        return CODE_NESSUNA

# =================== ANALISI COMPLETA MURO ===================

def analizzaMuro(camera):
    """
    Strategy:
    1. Prima cerca cognitive target (cerchi)
    2. Se non trovato, cerca lettere greche (OCR)
    3. Ritorna primo codice valido
    """
    ret, frame = camera.read()
    
    if not ret:
        print("[ERROR] Impossibile acquisire frame dalla camera")
        return CODE_NESSUNA
    
    # STEP 1: Cognitive Target
    code = rilevaCognitiveTarget(frame)
    
    if code != CODE_NESSUNA and code != CODE_FALSO_POSITIVO:
        return code
    
    # STEP 2: Lettera Greca (fallback)
    code = rilevaLetteraGreca(frame)
    
    if code != CODE_NESSUNA:
        return code
    
    print("[RISULTATO] Nessuna vittima rilevata")
    return CODE_NESSUNA

# =================== NAVIGAZIONE BFS ===================

class Maze:
    def __init__(self):
        self.cells = {}  # {(x,y): {'visited': bool, 'walls': {0:N,1:E,2:S,3:W}, 'victim': code}}
        self.current_pos = (0, 0)
        self.current_dir = 0  # 0=Nord, 1=Est, 2=Sud, 3=Ovest
        self.blue_tiles_visited = set()  # NUOVO V4: traccia celle blu già visitate
        
        # Inizializza cella iniziale
        self.cells[self.current_pos] = {
            'visited': True,
            'walls': {0: False, 1: False, 2: False, 3: False},
            'victim': CODE_NESSUNA
        }
    
    def update_walls(self, front_wall, right_wall, left_wall):
        """Aggiorna muri della cella corrente"""
        if self.current_pos not in self.cells:
            self.cells[self.current_pos] = {
                'visited': True,
                'walls': {0: False, 1: False, 2: False, 3: False},
                'victim': CODE_NESSUNA
            }
        
        # Mappa direzioni assolute
        front_dir = self.current_dir
        right_dir = (self.current_dir + 1) % 4
        left_dir = (self.current_dir - 1) % 4
        
        self.cells[self.current_pos]['walls'][front_dir] = front_wall
        self.cells[self.current_pos]['walls'][right_dir] = right_wall
        self.cells[self.current_pos]['walls'][left_dir] = left_wall
        
        print(f"[MAZE] Pos {self.current_pos}, Dir {self.current_dir}")
        print(f"[MAZE] Muri: F={front_wall}, R={right_wall}, L={left_wall}")
    
    def update_victim(self, code):
        """Aggiorna vittima nella cella corrente"""
        if self.current_pos in self.cells:
            self.cells[self.current_pos]['victim'] = code
            print(f"[MAZE] Vittima registrata in {self.current_pos}: codice {code}")
    
    def mark_blue_tile_visited(self, pos):
        """NUOVO V4: Marca cella blu come visitata (non rivisitabile)"""
        self.blue_tiles_visited.add(pos)
        print(f"[MAZE] Cella blu {pos} aggiunta a blacklist")
    
    def get_next_position(self, pos, direction):
        """Calcola posizione successiva data direzione assoluta"""
        x, y = pos
        if direction == 0:    # Nord
            return (x, y + 1)
        elif direction == 1:  # Est
            return (x + 1, y)
        elif direction == 2:  # Sud
            return (x, y - 1)
        elif direction == 3:  # Ovest
            return (x - 1, y)
    
    def has_wall(self, pos, direction):
        """Verifica se c'è muro in direzione assoluta"""
        if pos not in self.cells:
            return False
        return self.cells[pos]['walls'].get(direction, False)
    
    def is_blue_tile(self, pos):
        """NUOVO V4: Verifica se cella è blu e già visitata"""
        return pos in self.blue_tiles_visited
    
    def bfs_next_move(self):
        """
        Trova prossima cella non visitata più vicina con BFS
        NUOVO V4: Esclude celle blu già visitate
        """
        queue = deque([(self.current_pos, [])])
        visited_bfs = {self.current_pos}
        
        while queue:
            pos, path = queue.popleft()
            
            # Esplora direzioni adiacenti
            for direction in range(4):
                next_pos = self.get_next_position(pos, direction)
                
                # Condizioni per proseguire:
                # 1. Cella non visitata O cella blu non in blacklist
                # 2. Nessun muro
                # 3. Non già visitata in questo BFS
                unvisited = (next_pos not in self.cells or not self.cells[next_pos]['visited'])
                not_blacklisted = not self.is_blue_tile(next_pos)  # NUOVO V4
                no_wall = not self.has_wall(pos, direction)
                not_in_bfs = next_pos not in visited_bfs
                
                if unvisited and not_blacklisted and no_wall and not_in_bfs:
                    # Trovata cella target!
                    return self.calculate_moves(path + [direction])
                
                if no_wall and not_in_bfs:
                    queue.append((next_pos, path + [direction]))
                    visited_bfs.add(next_pos)
        
        print("[BFS] Mappa completamente esplorata")
        return DIR_TERMINA
    
    def calculate_moves(self, path):
        """Calcola primo movimento da path BFS"""
        if not path:
            return DIR_TERMINA
        
        first_dir = path[0]
        
        # Calcola movimento relativo rispetto a current_dir
        relative_dir = (first_dir - self.current_dir) % 4
        
        if relative_dir == 0:
            return DIR_DRITTO
        elif relative_dir == 1:
            return DIR_DESTRA
        elif relative_dir == 3:
            return DIR_SINISTRA
        elif relative_dir == 2:
            return DIR_DIETRO
    
    def execute_move(self, direction):
        """Aggiorna posizione dopo movimento"""
        if direction == DIR_DRITTO:
            self.current_pos = self.get_next_position(self.current_pos, self.current_dir)
        elif direction == DIR_DESTRA:
            self.current_dir = (self.current_dir + 1) % 4
            self.current_pos = self.get_next_position(self.current_pos, self.current_dir)
        elif direction == DIR_SINISTRA:
            self.current_dir = (self.current_dir - 1) % 4
            self.current_pos = self.get_next_position(self.current_pos, self.current_dir)
        elif direction == DIR_DIETRO:
            self.current_dir = (self.current_dir + 2) % 4
            self.current_pos = self.get_next_position(self.current_pos, self.current_dir)
        
        # Marca cella come visitata
        if self.current_pos not in self.cells:
            self.cells[self.current_pos] = {
                'visited': True,
                'walls': {0: False, 1: False, 2: False, 3: False},
                'victim': CODE_NESSUNA
            }
        else:
            self.cells[self.current_pos]['visited'] = True
        
        print(f"[MAZE] Nuova posizione: {self.current_pos}, direzione: {self.current_dir}")

# =================== MAIN LOOP ===================

def main():
    print("=" * 60)
    print("RoboCup Rescue Maze 2026 - Raspberry Pi V4")
    print("Cognitive Targets + Blue Tiles + LED Victim Signal")
    print("=" * 60)
    
    # Inizializza seriale
    ser = init_serial()
    if not ser:
        print("[FATAL] Impossibile proseguire senza seriale")
        return
    
    # Avvia heartbeat monitor
    heartbeat_monitor = HeartbeatMonitor(ser)
    heartbeat_monitor.start()
    
    # Inizializza camera
    camera = cv2.VideoCapture(CAMERA_INDEX)
    if not camera.isOpened():
        print("[ERROR] Impossibile aprire camera")
        return
    
    print("[OK] Camera inizializzata")
    
    # Inizializza maze
    maze = Maze()
    
    try:
        while True:
            # Attendi comando da Arduino
            cmd = None
            while cmd is None:
                cmd = read_serial(ser, heartbeat_monitor)
                time.sleep(0.01)
            
            # Gestisci messaggi log (iniziano con '[')
            if cmd.startswith('['):
                continue
            
            try:
                value = int(cmd)
            except ValueError:
                print(f"[WARN] Comando non valido: {cmd}")
                continue
            
            # ===== SCANSIONE MURI =====
            
            if value == 0 or value == 1:
                # Ricezione stato muro (0=assente, 1=presente)
                wall_present = (value == 1)
                
                if wall_present:
                    # Analizza muro per cognitive target/lettera
                    code = analizzaMuro(camera)
                    send_to_arduino(ser, code)
                else:
                    # Nessun muro, nessuna vittima
                    send_to_arduino(ser, CODE_NESSUNA)
                
                # Attendi prossimo stato muro o richiesta direzione
                continue
            
            # ===== BLUE TILE DETECTED =====
            
            elif value == CODE_BLUE_TILE:
                # NUOVO V4: Arduino ha rilevato cella blu
                print(f"[INFO] Cella blu rilevata in posizione {maze.current_pos}")
                maze.mark_blue_tile_visited(maze.current_pos)
                continue
            
            # ===== RICHIESTA DIREZIONE =====
            
            # Nota: dopo scansione 3 muri, Arduino chiederà direzione
            # Usa codici esistenti come trigger (es. se riceve codici fuori range 0-5)
            
            # Calcola prossima direzione con BFS
            direction = maze.bfs_next_move()
            
            if direction == DIR_TERMINA:
                print("[INFO] Esplorazione completata!")
                send_to_arduino(ser, DIR_TERMINA)
                break
            
            print(f"[BFS] Direzione calcolata: {direction}")
            send_to_arduino(ser, direction)
            
            # Attendi conferma movimento
            confirm = None
            while confirm is None:
                confirm = read_serial(ser, heartbeat_monitor)
                time.sleep(0.01)
            
            try:
                success = int(confirm)
                if success == 1:
                    print("[OK] Movimento completato con successo")
                    maze.execute_move(direction)
                else:
                    print("[WARN] Movimento fallito, ostacolo rilevato")
            except ValueError:
                print(f"[WARN] Conferma non valida: {confirm}")
    
    except KeyboardInterrupt:
        print("\n[INFO] Interruzione utente")
    
    finally:
        # Cleanup
        heartbeat_monitor.stop()
        camera.release()
        ser.close()
        print("[INFO] Risorse rilasciate. Shutdown completato.")

if __name__ == "__main__":
    main()
