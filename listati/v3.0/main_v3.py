#!/usr/bin/env python3
"""
RoboCup Rescue Maze 2026 - Raspberry Pi V3
Versione con Cognitive Targets + Lettere Greche
"""

import cv2
import numpy as np
import serial
import time
import sys
import os
import threading
from collections import deque

try:
    import pytesseract
    TESSERACT_AVAILABLE = True
except ImportError:
    TESSERACT_AVAILABLE = False
    print("[WARN] pytesseract non disponibile - OCR disabilitato")

# =================== CONFIGURAZIONE ===================

# Seriale
SERIAL_PORT = "/dev/ttyACM0"
BAUDRATE = 115200
SERIAL_TIMEOUT = 5

# Heartbeat e Watchdog
HEARTBEAT_INTERVAL = 1.0
WATCHDOG_TIMEOUT = 5.0

# Camera
CAMERA_INDEX = 0
CAMERA_WIDTH = 640
CAMERA_HEIGHT = 480

# Codici comunicazione - AGGIORNATI PER V3
CODE_NESSUNA = 0
CODE_HARMED = 1      # Φ (Phi) oppure Somma = 2
CODE_STABLE = 2      # Ψ (Psi) oppure Somma = 1
CODE_UNHARMED = 3    # Ω (Omega) oppure Somma = 0
CODE_FALSO_POSITIVO = 4  # Cognitive target con somma diversa da 0/1/2

# Codici direzione
DIR_TERMINA = 0
DIR_DRITTO = 1
DIR_DESTRA = 2
DIR_SINISTRA = 3
DIR_DIETRO = 4

# Cognitive Targets
COGNITIVE_TARGET_DIAMETER = 50  # mm (5 cm)
COGNITIVE_TARGET_MIN_PIXELS = 150  # Pixel minimi per rilevamento
CIRCLE_DIAMETERS = [10, 20, 30, 40, 50]  # mm (1cm, 2cm, 3cm, 4cm, 5cm)

# Valori colori cognitive targets
COLOR_VALUES = {
    'black': -2,
    'red': -1,
    'yellow': 0,
    'green': 1,
    'blue': 2
}

# Mappatura somma → codice
SCORE_TO_CODE = {
    2: CODE_HARMED,    # Harmed victim
    1: CODE_STABLE,    # Stable victim
    0: CODE_UNHARMED   # Unharmed victim
}

# Lettere greche → codici
GREEK_LETTER_TO_CODE = {
    'Φ': CODE_HARMED,    # Phi → Harmed (2 medikit)
    'Ψ': CODE_STABLE,    # Psi → Stable (1 medikit)
    'Ω': CODE_UNHARMED   # Omega → Unharmed (0 medikit)
}

# =================== SISTEMA HEARTBEAT/WATCHDOG ===================

class SystemMonitor:
    """Gestisce heartbeat e watchdog per Arduino"""
    
    def __init__(self):
        self.last_arduino_heartbeat = time.time()
        self.arduino_alive = True
        self.monitor_thread = None
        self.running = False
        self.ser = None
    
    def set_serial(self, serial_connection):
        self.ser = serial_connection
    
    def start_monitoring(self):
        if self.monitor_thread is None or not self.monitor_thread.is_alive():
            self.running = True
            self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
            self.monitor_thread.start()
            print("[MONITOR] Thread heartbeat avviato")
    
    def stop_monitoring(self):
        self.running = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=2)
        print("[MONITOR] Thread heartbeat fermato")
    
    def _monitor_loop(self):
        while self.running:
            elapsed = time.time() - self.last_arduino_heartbeat
            
            if elapsed > WATCHDOG_TIMEOUT:
                if self.arduino_alive:
                    print(f"[ERRORE CRITICO] Arduino non risponde da {elapsed:.1f}s!")
                    self.arduino_alive = False
            else:
                if not self.arduino_alive:
                    print("[INFO] Arduino tornato online!")
                    self.arduino_alive = True
            
            # Invia heartbeat ad Arduino
            if self.ser and self.ser.is_open:
                try:
                    self.ser.write(b"HB\n")
                except Exception as e:
                    print(f"[WARN] Errore invio heartbeat: {e}")
            
            time.sleep(HEARTBEAT_INTERVAL)
    
    def update_heartbeat(self):
        self.last_arduino_heartbeat = time.time()
        if not self.arduino_alive:
            print("[INFO] Arduino tornato online!")
            self.arduino_alive = True
    
    def is_arduino_alive(self):
        return self.arduino_alive

# Istanza globale
system_monitor = SystemMonitor()

# =================== COMUNICAZIONE SERIALE ===================

def inizializzaSeriale():
    global ser
    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=SERIAL_TIMEOUT)
        time.sleep(2)
        print(f"[OK] Connesso ad Arduino su {SERIAL_PORT}")
        
        # Configura monitor
        system_monitor.set_serial(ser)
        system_monitor.start_monitoring()
        
        return True
    except Exception as e:
        print(f"[ERRORE] Impossibile connettersi ad Arduino: {e}")
        return False

def riceviDaArduino():
    """Riceve dati da Arduino filtrando heartbeat"""
    try:
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()
                
                # Filtra heartbeat
                if line == "HB":
                    system_monitor.update_heartbeat()
                    continue
                
                # Aggiorna timestamp comunque
                system_monitor.update_heartbeat()
                
                # Log ricezione
                print(f"[← Arduino] {line}")
                return int(line)
            
            time.sleep(0.01)
    except ValueError:
        print(f"[WARN] Valore non numerico ricevuto: {line}")
        return 0
    except Exception as e:
        print(f"[ERRORE] Errore ricezione: {e}")
        return 0

def inviaAdArduino(valore):
    """Invia comando ad Arduino con controllo stato"""
    if not system_monitor.is_arduino_alive():
        print("[ERRORE] Arduino non risponde, invio annullato")
        return False
    
    try:
        messaggio = str(valore) + "\n"
        ser.write(messaggio.encode())
        print(f"[→ Arduino] {valore}")
        return True
    except Exception as e:
        print(f"[ERRORE] Errore invio: {e}")
        return False

# =================== VISIONE - COGNITIVE TARGETS ===================

def rilevaColoreCerchio(frame, center_x, center_y, radius_mm, pixel_per_mm):
    """
    Rileva il colore dominante in un cerchio specifico
    
    Args:
        frame: immagine BGR
        center_x, center_y: coordinate centro target
        radius_mm: raggio cerchio in mm
        pixel_per_mm: fattore conversione mm → pixel
    
    Returns:
        nome colore ('black', 'red', 'yellow', 'green', 'blue', 'unknown')
    """
    radius_px = int(radius_mm * pixel_per_mm / 2)  # Raggio in pixel
    
    # Crea maschera circolare
    mask = np.zeros(frame.shape[:2], dtype=np.uint8)
    cv2.circle(mask, (center_x, center_y), radius_px, 255, -1)
    
    # Estrai regione
    masked = cv2.bitwise_and(frame, frame, mask=mask)
    
    # Converti in HSV
    hsv = cv2.cvtColor(masked, cv2.COLOR_BGR2HSV)
    
    # Conta pixel per ogni colore
    color_counts = {}
    
    # NERO: V < 50
    black_mask = cv2.inRange(hsv, np.array([0, 0, 0]), np.array([180, 255, 50]))
    black_mask = cv2.bitwise_and(black_mask, black_mask, mask=mask)
    color_counts['black'] = cv2.countNonZero(black_mask)
    
    # ROSSO: H:[0-10] + [170-180], S:[100-255], V:[100-255]
    red_mask1 = cv2.inRange(hsv, np.array([0, 100, 100]), np.array([10, 255, 255]))
    red_mask2 = cv2.inRange(hsv, np.array([170, 100, 100]), np.array([180, 255, 255]))
    red_mask = cv2.bitwise_or(red_mask1, red_mask2)
    red_mask = cv2.bitwise_and(red_mask, red_mask, mask=mask)
    color_counts['red'] = cv2.countNonZero(red_mask)
    
    # GIALLO: H:[20-30], S:[100-255], V:[100-255]
    yellow_mask = cv2.inRange(hsv, np.array([20, 100, 100]), np.array([30, 255, 255]))
    yellow_mask = cv2.bitwise_and(yellow_mask, yellow_mask, mask=mask)
    color_counts['yellow'] = cv2.countNonZero(yellow_mask)
    
    # VERDE: H:[40-80], S:[50-255], V:[50-255]
    green_mask = cv2.inRange(hsv, np.array([40, 50, 50]), np.array([80, 255, 255]))
    green_mask = cv2.bitwise_and(green_mask, green_mask, mask=mask)
    color_counts['green'] = cv2.countNonZero(green_mask)
    
    # BLU: H:[100-130], S:[100-255], V:[100-255]
    blue_mask = cv2.inRange(hsv, np.array([100, 100, 100]), np.array([130, 255, 255]))
    blue_mask = cv2.bitwise_and(blue_mask, blue_mask, mask=mask)
    color_counts['blue'] = cv2.countNonZero(blue_mask)
    
    # Trova colore dominante
    if not color_counts:
        return 'unknown'
    
    dominant_color = max(color_counts, key=color_counts.get)
    max_count = color_counts[dominant_color]
    
    # Soglia minima 30% dei pixel del cerchio
    total_pixels_circle = np.pi * (radius_px ** 2)
    threshold = total_pixels_circle * 0.3
    
    if max_count < threshold:
        return 'unknown'
    
    return dominant_color

def rilevaCognitiveTarget(frame):
    """
    Rileva e analizza cognitive target (cerchi concentrici)
    
    Returns:
        tuple: (codice, somma_valori) dove:
            - codice: CODE_HARMED/STABLE/UNHARMED/FALSO_POSITIVO/NESSUNA
            - somma_valori: valore numerico calcolato
    """
    print("[INFO] Analisi cognitive target...")
    
    # Converti in grayscale
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
        return (CODE_NESSUNA, None)
    
    circles = np.uint16(np.around(circles))
    
    # Trova cerchio più grande (dovrebbe essere il cerchio esterno da 5cm)
    largest_circle = None
    max_radius = 0
    
    for circle in circles[0, :]:
        x, y, r = circle
        if r > max_radius:
            max_radius = r
            largest_circle = (x, y, r)
    
    if largest_circle is None:
        print("[INFO] Nessun cognitive target valido")
        return (CODE_NESSUNA, None)
    
    center_x, center_y, outer_radius = largest_circle
    
    print(f"[DEBUG] Target trovato: centro=({center_x},{center_y}), raggio={outer_radius}px")
    
    # Calcola fattore conversione pixel/mm
    # outer_radius corrisponde a 25mm (raggio del cerchio da 5cm)
    pixel_per_mm = outer_radius / 25.0
    
    # Analizza ogni cerchio concentrico (dal più piccolo al più grande)
    detected_colors = []
    total_score = 0
    
    for diameter_mm in CIRCLE_DIAMETERS:
        color = rilevaColoreCerchio(frame, center_x, center_y, diameter_mm, pixel_per_mm)
        
        if color != 'unknown' and color in COLOR_VALUES:
            value = COLOR_VALUES[color]
            detected_colors.append((diameter_mm, color, value))
            total_score += value
            print(f"[DEBUG] Cerchio {diameter_mm}mm: {color.upper()} (valore={value:+d})")
        else:
            print(f"[DEBUG] Cerchio {diameter_mm}mm: colore non identificato")
    
    # Verifica se abbiamo almeno 3 cerchi rilevati
    if len(detected_colors) < 3:
        print(f"[WARN] Solo {len(detected_colors)} cerchi rilevati, troppo pochi")
        return (CODE_NESSUNA, None)
    
    print(f"[INFO] SOMMA TOTALE: {total_score:+d}")
    
    # Mappa somma → codice
    if total_score in SCORE_TO_CODE:
        code = SCORE_TO_CODE[total_score]
        victim_type = {
            CODE_HARMED: "HARMED (2 medikit)",
            CODE_STABLE: "STABLE (1 medikit)",
            CODE_UNHARMED: "UNHARMED (0 medikit)"
        }[code]
        print(f"[INFO] *** VITTIMA RILEVATA: {victim_type} ***")
        return (code, total_score)
    else:
        print(f"[INFO] Somma {total_score} non corrisponde a vittima (fake target)")
        return (CODE_FALSO_POSITIVO, total_score)

# =================== VISIONE - LETTERE GRECHE ===================

def rilevaLetteraGreca(frame):
    """
    Rileva lettere greche (Φ, Ψ, Ω) tramite OCR Tesseract
    
    Returns:
        codice: CODE_HARMED/STABLE/UNHARMED/NESSUNA
    """
    if not TESSERACT_AVAILABLE:
        print("[WARN] Tesseract non disponibile, skip OCR")
        return CODE_NESSUNA
    
    print("[INFO] Analisi lettere greche...")
    
    # Preprocessing per OCR
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    _, thresh = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    
    # OCR con lingua greca
    custom_config = r'--oem 3 --psm 6 -l ell'
    
    try:
        text = pytesseract.image_to_string(thresh, config=custom_config)
        text_cleaned = text.strip()
        
        print(f"[DEBUG] OCR risultato: '{text_cleaned}'")
        
        # Cerca lettere greche
        for greek_letter, code in GREEK_LETTER_TO_CODE.items():
            if greek_letter in text_cleaned:
                victim_type = {
                    CODE_HARMED: "Φ (PHI) → HARMED (2 medikit)",
                    CODE_STABLE: "Ψ (PSI) → STABLE (1 medikit)",
                    CODE_UNHARMED: "Ω (OMEGA) → UNHARMED (0 medikit)"
                }[code]
                print(f"[INFO] *** LETTERA RILEVATA: {victim_type} ***")
                return code
        
        print("[INFO] Nessuna lettera greca riconosciuta")
        return CODE_NESSUNA
        
    except Exception as e:
        print(f"[ERRORE] OCR fallito: {e}")
        return CODE_NESSUNA

# =================== VISIONE - ANALISI COMPLETA ===================

def analizzaMuro(frame):
    """
    Analizza frame alla ricerca di cognitive target O lettere greche
    
    Strategy:
    1. Prima cerca cognitive target (cerchi concentrici)
    2. Se non trovati, cerca lettere greche
    3. Ritorna primo codice valido trovato
    
    Returns:
        codice: CODE_HARMED/STABLE/UNHARMED/FALSO_POSITIVO/NESSUNA
    """
    print("\n[===== ANALISI MURO =====]")
    
    # STEP 1: Cognitive Target
    code_target, score = rilevaCognitiveTarget(frame)
    
    if code_target != CODE_NESSUNA:
        # Target trovato e valido
        if code_target in [CODE_HARMED, CODE_STABLE, CODE_UNHARMED]:
            print(f"[RISULTATO] Cognitive target valido (codice {code_target})")
            return code_target
        elif code_target == CODE_FALSO_POSITIVO:
            print(f"[RISULTATO] Cognitive target fake (somma={score}), ignoro")
            # Continua con ricerca lettere greche
    
    # STEP 2: Lettere Greche
    code_letter = rilevaLetteraGreca(frame)
    
    if code_letter != CODE_NESSUNA:
        print(f"[RISULTATO] Lettera greca trovata (codice {code_letter})")
        return code_letter
    
    # STEP 3: Niente trovato
    print("[RISULTATO] Nessuna vittima rilevata")
    return CODE_NESSUNA

# =================== NAVIGAZIONE ===================

class Cella:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def __eq__(self, other):
        return self.x == other.x and self.y == other.y
    
    def __hash__(self):
        return hash((self.x, self.y))
    
    def __repr__(self):
        return f"({self.x},{self.y})"

class MemoriaCelle:
    def __init__(self):
        self.visitate = set()
        self.da_visitare = deque()
        self.non_percorribili = set()
    
    def aggiungiVisitata(self, cella):
        self.visitate.add(cella)
        if cella in self.da_visitare:
            self.da_visitare.remove(cella)
    
    def aggiungiDaVisitare(self, cella):
        if cella not in self.visitate and cella not in self.da_visitare and cella not in self.non_percorribili:
            self.da_visitare.append(cella)
    
    def aggiungiNonPercorribile(self, cella):
        self.non_percorribili.add(cella)
        if cella in self.da_visitare:
            self.da_visitare.remove(cella)
    
    def getProssimaCella(self):
        if self.da_visitare:
            return self.da_visitare.popleft()
        return None

# Variabili globali navigazione
posizione_corrente = Cella(0, 0)
direzione_corrente = 0
memoria = MemoriaCelle()

def calcolaCoordinataCellaFrontale():
    x, y = posizione_corrente.x, posizione_corrente.y
    if direzione_corrente == 0: y += 1
    elif direzione_corrente == 1: x += 1
    elif direzione_corrente == 2: y -= 1
    elif direzione_corrente == 3: x -= 1
    return Cella(x, y)

def calcolaCoordinataCellaDestra():
    x, y = posizione_corrente.x, posizione_corrente.y
    if direzione_corrente == 0: x += 1
    elif direzione_corrente == 1: y -= 1
    elif direzione_corrente == 2: x -= 1
    elif direzione_corrente == 3: y += 1
    return Cella(x, y)

def calcolaCoordinataCellaSinistra():
    x, y = posizione_corrente.x, posizione_corrente.y
    if direzione_corrente == 0: x -= 1
    elif direzione_corrente == 1: y += 1
    elif direzione_corrente == 2: x += 1
    elif direzione_corrente == 3: y -= 1
    return Cella(x, y)

def scegliDirezione(cella_target):
    global direzione_corrente
    
    diff_x = cella_target.x - posizione_corrente.x
    diff_y = cella_target.y - posizione_corrente.y
    
    if diff_y == 1: direzione_target = 0
    elif diff_x == 1: direzione_target = 1
    elif diff_y == -1: direzione_target = 2
    elif diff_x == -1: direzione_target = 3
    else: return DIR_TERMINA
    
    diff_direzione = (direzione_target - direzione_corrente) % 4
    
    if diff_direzione == 0: return DIR_DRITTO
    elif diff_direzione == 1: return DIR_DESTRA
    elif diff_direzione == 2: return DIR_DIETRO
    elif diff_direzione == 3: return DIR_SINISTRA
    
    return DIR_TERMINA

# =================== CICLO PRINCIPALE ===================

def main():
    global posizione_corrente, direzione_corrente, ser, cap
    
    print("\n" + "="*70)
    print("  ROBOCUP RESCUE MAZE 2026 - VERSIONE V3")
    print("  Cognitive Targets + Lettere Greche")
    print("="*70 + "\n")
    
    # Inizializza seriale
    if not inizializzaSeriale():
        return
    
    # Inizializza camera
    cap = cv2.VideoCapture(CAMERA_INDEX)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAMERA_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAMERA_HEIGHT)
    
    if not cap.isOpened():
        print("[ERRORE] Impossibile aprire camera")
        return
    
    print("[OK] Camera inizializzata\n")
    
    # Marca posizione iniziale come visitata
    memoria.aggiungiVisitata(posizione_corrente)
    
    ciclo = 0
    
    try:
        while True:
            ciclo += 1
            
            print("\n" + "="*70)
            print(f"  CICLO {ciclo}")
            print(f"  Posizione: {posizione_corrente} | Direzione: {direzione_corrente}")
            print(f"  Visitate: {len(memoria.visitate)} | Da visitare: {len(memoria.da_visitare)}")
            print(f"  Arduino: {'✓ ALIVE' if system_monitor.is_arduino_alive() else '✗ TIMEOUT'}")
            print("="*70 + "\n")
            
            # ===== SCANSIONE AMBIENTE =====
            print("[===== SCANSIONE AMBIENTE =====]")
            
            # FRONTALE
            print("[1/3] Scansione FRONTALE...")
            muro_frontale = riceviDaArduino()
            
            if muro_frontale == 1:
                print("[INFO] Muro presente → analisi vittima")
                
                ret, frame = cap.read()
                if ret:
                    print("[INFO] Frame acquisito")
                    codice = analizzaMuro(frame)
                    inviaAdArduino(codice)
                else:
                    print("[ERRORE] Impossibile leggere dalla camera")
                    inviaAdArduino(CODE_NESSUNA)
            else:
                print("[INFO] Nessun muro → cella visitabile")
                cella_front = calcolaCoordinataCellaFrontale()
                memoria.aggiungiDaVisitare(cella_front)
                inviaAdArduino(CODE_NESSUNA)
            
            # DESTRA
            print("\n[2/3] Scansione DESTRA...")
            muro_destra = riceviDaArduino()
            
            if muro_destra == 1:
                print("[INFO] Muro presente → analisi vittima")
                
                ret, frame = cap.read()
                if ret:
                    print("[INFO] Frame acquisito")
                    codice = analizzaMuro(frame)
                    inviaAdArduino(codice)
                else:
                    print("[ERRORE] Impossibile leggere dalla camera")
                    inviaAdArduino(CODE_NESSUNA)
            else:
                print("[INFO] Nessun muro → cella visitabile")
                cella_right = calcolaCoordinataCellaDestra()
                memoria.aggiungiDaVisitare(cella_right)
                inviaAdArduino(CODE_NESSUNA)
            
            # SINISTRA
            print("\n[3/3] Scansione SINISTRA...")
            muro_sinistra = riceviDaArduino()
            
            if muro_sinistra == 1:
                print("[INFO] Muro presente → analisi vittima")
                
                ret, frame = cap.read()
                if ret:
                    print("[INFO] Frame acquisito")
                    codice = analizzaMuro(frame)
                    inviaAdArduino(codice)
                else:
                    print("[ERRORE] Impossibile leggere dalla camera")
                    inviaAdArduino(CODE_NESSUNA)
            else:
                print("[INFO] Nessun muro → cella visitabile")
                cella_left = calcolaCoordinataCellaSinistra()
                memoria.aggiungiDaVisitare(cella_left)
                inviaAdArduino(CODE_NESSUNA)
            
            # ===== DECISIONE DIREZIONE =====
            print("\n[===== DECISIONE DIREZIONE =====]")
            
            prossima_cella = memoria.getProssimaCella()
            
            if prossima_cella is None:
                print("[INFO] Tutte le celle esplorate!")
                print("[INFO] Invio comando TERMINA")
                inviaAdArduino(DIR_TERMINA)
                break
            
            print(f"[INFO] Prossima cella da visitare: {prossima_cella}")
            
            direzione = scegliDirezione(prossima_cella)
            
            direzione_str = {
                DIR_DRITTO: "DRITTO",
                DIR_DESTRA: "DESTRA",
                DIR_SINISTRA: "SINISTRA",
                DIR_DIETRO: "DIETRO"
            }.get(direzione, "TERMINA")
            
            print(f"[INFO] Direzione scelta: {direzione_str}")
            inviaAdArduino(direzione)
            
            if direzione == DIR_TERMINA:
                print("[INFO] Impossibile raggiungere cella, termino")
                break
            
            # ===== ATTESA MOVIMENTO =====
            print("\n[===== ATTESA MOVIMENTO =====]")
            esito_movimento = riceviDaArduino()
            
            if esito_movimento == 1:
                print("[OK] Movimento completato con successo")
                
                # Aggiorna direzione
                if direzione == DIR_DESTRA:
                    direzione_corrente = (direzione_corrente + 1) % 4
                elif direzione == DIR_SINISTRA:
                    direzione_corrente = (direzione_corrente - 1) % 4
                elif direzione == DIR_DIETRO:
                    direzione_corrente = (direzione_corrente + 2) % 4
                
                # Aggiorna posizione
                posizione_corrente = prossima_cella
                memoria.aggiungiVisitata(posizione_corrente)
                
                print(f"[INFO] Nuova posizione: {posizione_corrente}, direzione: {direzione_corrente}")
            else:
                print("[WARN] Movimento fallito (ostacolo/nero/rosso)")
                memoria.aggiungiNonPercorribile(prossima_cella)
            
            # Conferma fine ciclo
            inviaAdArduino(1)
            
    except KeyboardInterrupt:
        print("\n[INFO] Interruzione utente (Ctrl+C)")
    
    finally:
        # Cleanup
        print("\n[INFO] Chiusura sistema...")
        system_monitor.stop_monitoring()
        
        if 'cap' in globals() and cap.isOpened():
            cap.release()
        
        if 'ser' in globals() and ser.is_open:
            ser.close()
        
        print("[INFO] Sistema terminato\n")

if __name__ == "__main__":
    main()
