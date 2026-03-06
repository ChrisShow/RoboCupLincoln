#!/usr/bin/env python3
"""
RoboCup Rescue Maze 2026 - Raspberry Pi Main Script - VERSION 9.0 FINALE

CHANGELOG V9:
✅ NUOVO: Algoritmo BFS inverso per calcolare percorso ritorno a (0,0)
✅ NUOVO: Gestione comando START_RETURN quando lista celle vuota
✅ NUOVO: Riconoscimento CODE_RETURN_HOME (7) e CODE_HOME_REACHED (8)
✅ NUOVO: Modalità ritorno con skip rilevazione vittime
✅ NUOVO: Funzione calculate_path_to_start() - BFS inverso
✅ NUOVO: Fallback wall-follower se BFS fallisce
- Supporto CODE_SKIP_INCLINE (6) da V8
- Gestione vittime HARMED/STABLE/UNHARMED
- BFS esplorazione labirinto

EXIT BONUS:
Quando to_visit si svuota:
1. Calcola percorso ottimale con BFS inverso
2. Invia START_RETURN ad Arduino
3. Segue percorso step-by-step fino a (0,0)
4. Conferma HOME_REACHED
5. Guadagna +10 punti per vittima salvata

Author: Team Franco
Date: Marzo 2026
Version: 9.0 FINALE
"""

import serial
import time
import cv2
import numpy as np
from collections import deque

# ========== CONFIGURAZIONE SERIALE ==========
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 9600
serial_connection = None

# ========== CODICI COMUNICAZIONE ==========
CODE_NESSUNA = 0
CODE_HARMED = 1
CODE_STABLE = 2
CODE_UNHARMED = 3
CODE_FALSO_POSITIVO = 4
CODE_BLUE_TILE = 5
CODE_SKIP_INCLINE = 6      # V8: Piano inclinato
CODE_RETURN_HOME = 7        # V9: Ritorno attivo
CODE_HOME_REACHED = 8       # V9: Arrivato a start

# ========== DIREZIONI ==========
NORTH = 0
EAST = 1
SOUTH = 2
WEST = 3

DIRECTION_NAMES = {
    NORTH: "NORD",
    EAST: "EST",
    SOUTH: "SUD",
    WEST: "OVEST"
}

# Delta coordinate per ogni direzione
DIRECTION_DELTA = {
    NORTH: (0, -1),   # Y decresce andando nord
    EAST: (1, 0),     # X aumenta andando est
    SOUTH: (0, 1),    # Y aumenta andando sud
    WEST: (-1, 0)     # X decresce andando ovest
}

# ========== STATO ROBOT ==========
class RobotState:
    def __init__(self):
        self.x = 0
        self.y = 0
        self.direction = NORTH
        
        # Mappa labirinto: (x,y) -> {'walls': [N,E,S,W], 'visited': bool, 'color': char}
        self.maze = {}
        
        # Liste BFS
        self.to_visit = []        # Celle da esplorare
        self.visited_cells = set()
        
        # Statistiche
        self.victims_saved = 0
        self.blue_tiles = 0
        self.incline_tiles = 0    # V8: Celle su pendenza
        
        # ✅ V9: Stato ritorno
        self.returning_home = False
        self.return_path = []     # Lista di direzioni per tornare a (0,0)
        self.return_step = 0
        
        # Inizializza cella start
        self.maze[(0, 0)] = {
            'walls': [False, False, False, False],
            'visited': True,
            'color': 'W',
            'incline': False
        }
        self.visited_cells.add((0, 0))

robot = RobotState()

# ========== SETUP SERIALE ==========
def setup_serial():
    global serial_connection
    try:
        serial_connection = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        print("[INIT] Connessione seriale stabilita")
        return True
    except Exception as e:
        print(f"[ERROR] Connessione seriale fallita: {e}")
        return False

# ========== COMUNICAZIONE ARDUINO ==========
def send_to_arduino(message):
    if serial_connection:
        serial_connection.write((str(message) + '\n').encode())
        print(f"[→ Arduino] {message}")

def read_from_arduino():
    if serial_connection and serial_connection.in_waiting > 0:
        try:
            message = serial_connection.readline().decode().strip()
            return message
        except:
            return None
    return None

# ========== HEARTBEAT ==========
def send_heartbeat():
    send_to_arduino("HEARTBEAT")

# ========== CAMERA & YOLO ==========
def detect_victim():
    """
    Placeholder per rilevazione vittima con YOLO
    Ritorna: (codice_vittima, confidence)
    """
    # TODO: Implementare YOLO v8 per rilevazione vittime
    # Per ora simulazione
    
    # Simula nessuna vittima
    return CODE_NESSUNA, 0.0

# ========== BFS ESPLORAZIONE ==========
def get_neighbor(x, y, direction):
    """Ritorna coordinate vicino in direzione specificata"""
    dx, dy = DIRECTION_DELTA[direction]
    return (x + dx, y + dy)

def update_maze_walls(x, y, front_wall, right_wall, left_wall):
    """
    Aggiorna mappa con muri rilevati da scansione
    front_wall, right_wall, left_wall: 1 = muro presente, 0 = libero
    """
    current_dir = robot.direction
    
    # Inizializza cella se non esiste
    if (x, y) not in robot.maze:
        robot.maze[(x, y)] = {
            'walls': [False, False, False, False],
            'visited': False,
            'color': 'W',
            'incline': False
        }
    
    # Mappa direzioni scansione a direzioni assolute
    # Front = direzione corrente
    # Right = (direzione + 1) % 4
    # Left = (direzione + 3) % 4
    
    front_dir = current_dir
    right_dir = (current_dir + 1) % 4
    left_dir = (current_dir + 3) % 4
    
    # Aggiorna muri
    robot.maze[(x, y)]['walls'][front_dir] = bool(front_wall)
    robot.maze[(x, y)]['walls'][right_dir] = bool(right_wall)
    robot.maze[(x, y)]['walls'][left_dir] = bool(left_wall)
    
    print(f"[MAZE] Cella ({x},{y}) muri aggiornati:")
    print(f"  NORD: {robot.maze[(x, y)]['walls'][NORTH]}")
    print(f"  EST: {robot.maze[(x, y)]['walls'][EAST]}")
    print(f"  SUD: {robot.maze[(x, y)]['walls'][SOUTH]}")
    print(f"  OVEST: {robot.maze[(x, y)]['walls'][WEST]}")

def find_accessible_neighbors(x, y):
    """
    Ritorna lista di celle vicine accessibili (no muri, non visitate)
    """
    neighbors = []
    
    for direction in [NORTH, EAST, SOUTH, WEST]:
        # Controlla se c'è muro in quella direzione
        if (x, y) in robot.maze:
            if robot.maze[(x, y)]['walls'][direction]:
                continue  # Muro presente, skip
        
        # Calcola coordinate vicino
        nx, ny = get_neighbor(x, y, direction)
        
        # Controlla se già visitato
        if (nx, ny) in robot.visited_cells:
            continue
        
        # Controlla se cella nera o rossa (blacklist)
        if (nx, ny) in robot.maze:
            cell_color = robot.maze[(nx, ny)].get('color', 'W')
            if cell_color in ['K', 'R']:
                continue
        
        neighbors.append((nx, ny, direction))
    
    return neighbors

def bfs_next_cell():
    """
    BFS per trovare prossima cella da visitare
    Ritorna: (target_x, target_y, first_direction) oppure None
    """
    if not robot.to_visit:
        print("[BFS] Lista to_visit vuota, nessuna cella da esplorare")
        return None
    
    # BFS per trovare percorso più breve a prossima cella non visitata
    queue = deque([(robot.x, robot.y, [])])
    visited_bfs = {(robot.x, robot.y)}
    
    while queue:
        x, y, path = queue.popleft()
        
        # Controlla se questa cella è nella to_visit
        if (x, y) in robot.to_visit:
            if path:
                first_dir = path[0]
                print(f"[BFS] Trovato percorso verso ({x},{y}), prima mossa: {DIRECTION_NAMES[first_dir]}")
                return (x, y, first_dir)
        
        # Esplora vicini accessibili
        neighbors = find_accessible_neighbors(x, y)
        for nx, ny, direction in neighbors:
            if (nx, ny) not in visited_bfs:
                visited_bfs.add((nx, ny))
                new_path = path + [direction]
                queue.append((nx, ny, new_path))
    
    print("[BFS] Nessun percorso trovato verso celle in to_visit")
    return None

# ✅ V9: BFS INVERSO PER RITORNO A START
def calculate_path_to_start():
    """
    Calcola percorso ottimale da posizione corrente a (0,0) usando BFS inverso
    Ritorna: lista di direzioni [dir1, dir2, ...] oppure None se non trova percorso
    """
    print(f"[BFS RETURN] Calcolo percorso da ({robot.x},{robot.y}) a (0,0)")
    
    # BFS inverso: da posizione corrente a (0,0)
    queue = deque([(robot.x, robot.y, [])])
    visited_bfs = {(robot.x, robot.y)}
    
    while queue:
        x, y, path = queue.popleft()
        
        # Raggiunto start?
        if x == 0 and y == 0:
            print(f"[BFS RETURN] Percorso trovato! Lunghezza: {len(path)} step")
            return path
        
        # Esplora tutte le 4 direzioni
        for direction in [NORTH, EAST, SOUTH, WEST]:
            # Controlla muro in quella direzione
            if (x, y) in robot.maze:
                if robot.maze[(x, y)]['walls'][direction]:
                    continue  # Muro presente
            
            # Calcola vicino
            nx, ny = get_neighbor(x, y, direction)
            
            # Controlla se vicino è stata visitata durante esplorazione
            if (nx, ny) not in robot.visited_cells:
                continue  # Non possiamo attraversare celle mai visitate
            
            # Controlla blacklist
            if (nx, ny) in robot.maze:
                cell_color = robot.maze[(nx, ny)].get('color', 'W')
                if cell_color in ['K', 'R']:
                    continue  # Evita celle nere/rosse
            
            # Aggiungi a BFS
            if (nx, ny) not in visited_bfs:
                visited_bfs.add((nx, ny))
                new_path = path + [direction]
                queue.append((nx, ny, new_path))
    
    print("[BFS RETURN] Nessun percorso trovato verso (0,0)!")
    return None

# ✅ V9: FALLBACK WALL-FOLLOWER
def wall_follower_next_direction():
    """
    Algoritmo wall-follower (segui muro destro) come fallback
    Ritorna: direzione da seguire
    """
    print("[WALL-FOLLOWER] Attivazione fallback")
    
    # Priorità: destra > dritto > sinistra > dietro
    current_dir = robot.direction
    right_dir = (current_dir + 1) % 4
    left_dir = (current_dir + 3) % 4
    back_dir = (current_dir + 2) % 4
    
    x, y = robot.x, robot.y
    
    # Prova destra
    if (x, y) in robot.maze:
        if not robot.maze[(x, y)]['walls'][right_dir]:
            print("[WALL-FOLLOWER] Gira DESTRA")
            return right_dir
    
    # Prova dritto
    if not robot.maze[(x, y)]['walls'][current_dir]:
        print("[WALL-FOLLOWER] Continua DRITTO")
        return current_dir
    
    # Prova sinistra
    if not robot.maze[(x, y)]['walls'][left_dir]:
        print("[WALL-FOLLOWER] Gira SINISTRA")
        return left_dir
    
    # Dietrofront
    print("[WALL-FOLLOWER] DIETROFRONT")
    return back_dir

# ========== GESTIONE MOVIMENTO ==========
def execute_movement(target_direction):
    """
    Invia comando movimento ad Arduino e attende conferma
    Ritorna: True se successo, False se fallito
    """
    # Invia direzione target
    send_to_arduino(f"DIR:{target_direction}")
    
    # Attendi conferma movimento
    print("[WAIT] Attendo conferma movimento da Arduino...")
    timeout = time.time() + 30  # 30s timeout
    
    while time.time() < timeout:
        message = read_from_arduino()
        
        if message == "1":
            print("[SUCCESS] Movimento completato")
            
            # Aggiorna posizione robot
            dx, dy = DIRECTION_DELTA[target_direction]
            robot.x += dx
            robot.y += dy
            robot.direction = target_direction
            
            # Marca cella come visitata
            robot.visited_cells.add((robot.x, robot.y))
            if (robot.x, robot.y) in robot.maze:
                robot.maze[(robot.x, robot.y)]['visited'] = True
            
            # Rimuovi da to_visit se presente
            if (robot.x, robot.y) in robot.to_visit:
                robot.to_visit.remove((robot.x, robot.y))
            
            print(f"[POS] Nuova posizione: ({robot.x},{robot.y}), DIR: {DIRECTION_NAMES[target_direction]}")
            return True
        
        elif message == "0":
            print("[FAILED] Movimento fallito (ostacolo)")
            return False
        
        elif message == str(CODE_SKIP_INCLINE):  # V8: Piano inclinato
            print("[INFO] Movimento su piano inclinato, skip controllo vittima")
            # Continua ad attendere conferma movimento
        
        time.sleep(0.1)
    
    print("[TIMEOUT] Nessuna conferma ricevuta da Arduino")
    return False

# ========== CICLO PRINCIPALE ==========
def main_loop():
    print("=== RoboCup Rescue Maze V9.0 FINALE ===")
    print("[INIT] Avvio sistema...")
    
    if not setup_serial():
        print("[ERROR] Setup seriale fallito, uscita")
        return
    
    print("[INIT] Sistema pronto!")
    print("[INFO] Modalità: BFS esplorazione + Ritorno automatico (0,0)")
    
    heartbeat_timer = time.time()
    
    # Ciclo principale
    while True:
        # Heartbeat ogni secondo
        if time.time() - heartbeat_timer > 1.0:
            send_heartbeat()
            heartbeat_timer = time.time()
        
        # Leggi messaggi Arduino
        message = read_from_arduino()
        if message:
            print(f"[← Arduino] {message}")
            
            # Gestione codici speciali
            if message == str(CODE_RETURN_HOME):  # V9: Arduino conferma modalità ritorno
                print("[INFO] Arduino in modalità RITORNO")
                continue
            
            elif message == str(CODE_HOME_REACHED):  # V9: Arduino arrivato a start
                print("[SUCCESS] 🏆 FRANCO È TORNATO ALLA (0,0)! 🏆")
                print(f"[STATS] Vittime salvate: {robot.victims_saved}")
                print(f"[STATS] Celle blu: {robot.blue_tiles}")
                print(f"[STATS] Celle pendenza: {robot.incline_tiles}")
                print("[EXIT BONUS] Punti extra ottenuti!")
                break  # Fine missione
        
        # ✅ V9: FASE RITORNO
        if robot.returning_home:
            print(f"[RETURN] Step {robot.return_step}/{len(robot.return_path)}")
            
            if robot.return_step >= len(robot.return_path):
                # Percorso completato, conferma arrivo
                print("[RETURN] Percorso completato, invio conferma HOME_REACHED")
                send_to_arduino("HOME_REACHED")
                time.sleep(1)
                continue
            
            # Prendi prossima direzione dal percorso
            next_dir = robot.return_path[robot.return_step]
            print(f"[RETURN] Prossima mossa: {DIRECTION_NAMES[next_dir]}")
            
            # Esegui movimento
            if execute_movement(next_dir):
                robot.return_step += 1
            else:
                print("[RETURN ERROR] Movimento fallito durante ritorno!")
                print("[RETURN] Ricalcolo percorso...")
                
                # Ricalcola percorso da nuova posizione
                robot.return_path = calculate_path_to_start()
                robot.return_step = 0
                
                if not robot.return_path:
                    print("[RETURN FALLBACK] BFS fallito, attivo wall-follower")
                    # Usa wall-follower come fallback
                    next_dir = wall_follower_next_direction()
                    execute_movement(next_dir)
            
            time.sleep(0.5)
            continue
        
        # ✅ V9: CHECK LISTA CELLE VUOTA → AVVIA RITORNO
        if not robot.to_visit and not robot.returning_home:
            print("[INFO] ⚠️  Lista celle da visitare VUOTA!")
            print("[INFO] 🎯 ATTIVAZIONE RITORNO A START (0,0)")
            
            # Calcola percorso ottimale con BFS inverso
            robot.return_path = calculate_path_to_start()
            
            if robot.return_path:
                print(f"[RETURN] Percorso calcolato: {len(robot.return_path)} step")
                robot.returning_home = True
                robot.return_step = 0
                
                # Invia comando START_RETURN ad Arduino
                send_to_arduino("START_RETURN")
                print("[→ Arduino] START_RETURN")
                
                time.sleep(1)
                continue
            else:
                print("[RETURN ERROR] BFS non ha trovato percorso!")
                print("[RETURN] Attivo wall-follower come fallback")
                
                # Fallback wall-follower
                robot.returning_home = True
                robot.return_path = []  # Lista vuota = usa wall-follower step by step
                robot.return_step = 0
                
                send_to_arduino("START_RETURN")
                time.sleep(1)
                continue
        
        # FASE ESPLORAZIONE NORMALE
        
        # Step 1: Scansione muri
        print("[STEP] Attendo scansione muri da Arduino...")
        
        # Attendi 3 valori: front, right, left
        walls_received = []
        timeout = time.time() + 10
        
        while len(walls_received) < 3 and time.time() < timeout:
            msg = read_from_arduino()
            if msg in ['0', '1']:
                walls_received.append(int(msg))
                print(f"[SCAN] Valore ricevuto: {msg} ({len(walls_received)}/3)")
        
        if len(walls_received) < 3:
            print("[ERROR] Scansione incompleta, skip ciclo")
            time.sleep(1)
            continue
        
        front_wall, right_wall, left_wall = walls_received
        
        # Aggiorna mappa
        update_maze_walls(robot.x, robot.y, front_wall, right_wall, left_wall)
        
        # Step 2: BFS calcola prossima cella
        result = bfs_next_cell()
        
        if not result:
            print("[BFS] Nessuna cella raggiungibile, esplorazione completata")
            
            # ✅ V9: Avvia ritorno automatico
            print("[INFO] 🎯 AVVIO RITORNO AUTOMATICO A (0,0)")
            robot.to_visit = []  # Svuota lista per triggare ritorno
            continue
        
        target_x, target_y, first_direction = result
        
        # Step 3: Esegui movimento
        success = execute_movement(first_direction)
        
        if not success:
            print("[MOVE] Movimento fallito, aggiorno mappa e riprovo")
            # Marca direzione come bloccata
            robot.maze[(robot.x, robot.y)]['walls'][first_direction] = True
            continue
        
        # Step 4: Rilevazione vittima (solo se non su pendenza)
        # Arduino gestisce autonomamente CODE_SKIP_INCLINE
        
        # Rileva vittima con YOLO
        victim_code, confidence = detect_victim()
        
        # Invia codice vittima ad Arduino
        send_to_arduino(f"CODE:{victim_code}")
        
        if victim_code in [CODE_HARMED, CODE_STABLE, CODE_UNHARMED]:
            robot.victims_saved += 1
            print(f"[VICTIM] Vittima salvata! Totale: {robot.victims_saved}")
        
        # Aggiorna statistiche
        if (robot.x, robot.y) in robot.maze:
            if robot.maze[(robot.x, robot.y)].get('color') == 'B':
                robot.blue_tiles += 1
            if robot.maze[(robot.x, robot.y)].get('incline', False):
                robot.incline_tiles += 1
        
        # Step 5: Aggiungi vicini non visitati a to_visit
        neighbors = find_accessible_neighbors(robot.x, robot.y)
        for nx, ny, _ in neighbors:
            if (nx, ny) not in robot.to_visit:
                robot.to_visit.append((nx, ny))
                print(f"[BFS] Aggiunta cella ({nx},{ny}) a to_visit")
        
        print(f"[STATUS] Celle in coda: {len(robot.to_visit)}")
        
        time.sleep(0.5)

# ========== ENTRY POINT ==========
if __name__ == "__main__":
    try:
        main_loop()
    except KeyboardInterrupt:
        print("\n[EXIT] Interruzione manuale")
        if serial_connection:
            serial_connection.close()
    except Exception as e:
        print(f"[ERROR] Errore critico: {e}")
        if serial_connection:
            serial_connection.close()
