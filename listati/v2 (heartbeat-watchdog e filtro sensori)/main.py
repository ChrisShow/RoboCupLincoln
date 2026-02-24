#!/usr/bin/env python3
"""
RoboCup Rescue Maze 2026 - Raspberry Pi V2
Versione avanzata con Heartbeat/Watchdog
"""

import cv2
import numpy as np
import serial
import time
import sys
import os
import threading
from collections import deque

# =================== CONFIGURAZIONE ===================

# Seriale
SERIAL_PORT = "/dev/ttyACM0"
BAUDRATE = 115200
SERIAL_TIMEOUT = 5

# Heartbeat e Watchdog
HEARTBEAT_INTERVAL = 1.0  # secondi
WATCHDOG_TIMEOUT = 5.0    # secondi

# Camera
CAMERA_INDEX = 0
CAMERA_WIDTH = 640
CAMERA_HEIGHT = 480

# Codici comunicazione
CODE_NESSUNA_RILEVAZIONE = 0
CODE_ROSSO = 1
CODE_GIALLO = 2
CODE_VERDE = 3
CODE_PHI = 4
CODE_PSI = 5
CODE_OMEGA = 6

DIR_TERMINA = 0
DIR_DRITTO = 1
DIR_DESTRA = 2
DIR_SINISTRA = 3
DIR_DIETRO = 4

# Soglie rilevazione
MIN_AREA_COLORE = 300
MIN_CONTOUR_AREA = 100

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
        """Imposta connessione seriale"""
        self.ser = serial_connection
    
    def start_monitoring(self):
        """Avvia thread di monitoraggio"""
        if self.monitor_thread is None or not self.monitor_thread.is_alive():
            self.running = True
            self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
            self.monitor_thread.start()
            print("[MONITOR] Thread heartbeat avviato")
    
    def stop_monitoring(self):
        """Ferma thread di monitoraggio"""
        self.running = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=2)
        print("[MONITOR] Thread heartbeat fermato")
    
    def _monitor_loop(self):
        """Loop principale monitoraggio"""
        while self.running:
            # Controlla heartbeat Arduino
            elapsed = time.time() - self.last_arduino_heartbeat
            
            if elapsed > WATCHDOG_TIMEOUT:
                if self.arduino_alive:
                    print(f"[ERRORE CRITICO] Arduino non risponde da {elapsed:.1f}s!")
                    self.arduino_alive = False
            else:
                if not self.arduino_alive:
                    print("[INFO] Arduino riconnesso")
                self.arduino_alive = True
            
            # Invia heartbeat ad Arduino
            try:
                if self.ser and self.ser.is_open:
                    self.ser.write(b"HB\n")
            except Exception as e:
                print(f"[WARN] Errore invio heartbeat: {e}")
            
            time.sleep(HEARTBEAT_INTERVAL)
    
    def update_heartbeat(self):
        """Aggiorna timestamp ultimo heartbeat ricevuto"""
        self.last_arduino_heartbeat = time.time()
        self.arduino_alive = True
    
    def is_arduino_alive(self):
        """Ritorna stato Arduino"""
        return self.arduino_alive

# Istanza globale monitor
system_monitor = SystemMonitor()

# =================== CLASSI ===================

class Cella:
    """Rappresenta una cella del labirinto"""
    
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def getX(self):
        return self.x
    
    def getY(self):
        return self.y
    
    def __eq__(self, other):
        if not isinstance(other, Cella):
            return False
        return self.x == other.x and self.y == other.y
    
    def __hash__(self):
        return hash((self.x, self.y))
    
    def __str__(self):
        return f"({self.x}, {self.y})"
    
    def __repr__(self):
        return self.__str__()

class MemoriaCelle:
    """Gestisce le celle già visitate"""
    
    def __init__(self):
        self.memoria = set()
    
    def aggiungi(self, cella):
        self.memoria.add(cella)
    
    def contiene(self, cella):
        return cella in self.memoria
    
    def __len__(self):
        return len(self.memoria)

# =================== VARIABILI GLOBALI ===================

ser = None
cap = None

cellaCorrente = Cella(0, 0)
direzione = 0

memoriaCelle = MemoriaCelle()
listaCelleNonVisitate = []
listaCelleNonPercorribili = []

# =================== FUNZIONI SERIALE ===================

def inizializzaSeriale():
    """Inizializza connessione seriale con Arduino"""
    global ser
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=SERIAL_TIMEOUT)
        time.sleep(2)
        
        # Configura monitor
        system_monitor.set_serial(ser)
        system_monitor.start_monitoring()
        
        print(f"[INFO] Connessione seriale OK: {SERIAL_PORT} @ {BAUDRATE}")
        print(f"[INFO] Heartbeat attivo (intervallo: {HEARTBEAT_INTERVAL}s)")
        return True
        
    except serial.SerialException as e:
        print(f"[ERRORE] Impossibile connettersi ad Arduino: {e}")
        return False

def inviaAdArduino(valore):
    """Invia valore intero ad Arduino"""
    global ser
    
    if ser is None:
        print("[ERRORE] Seriale non inizializzata")
        return False
    
    # Controlla stato Arduino
    if not system_monitor.is_arduino_alive():
        print("[ERRORE] Arduino non risponde, invio annullato")
        return False
    
    try:
        messaggio = str(valore) + "\n"
        ser.write(messaggio.encode('utf-8'))
        ser.flush()
        
        print(f"[→ Arduino] {valore}")
        return True
        
    except Exception as e:
        print(f"[ERRORE] Invio fallito: {e}")
        return False

def riceviDaArduino():
    """Riceve dato da Arduino"""
    global ser
    
    if ser is None:
        print("[ERRORE] Seriale non inizializzata")
        return None
    
    try:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        
        if line:
            # Controlla heartbeat
            if line == "HB":
                system_monitor.update_heartbeat()
                return riceviDaArduino()  # Riprova per dato vero
            
            # Aggiorna heartbeat
            system_monitor.update_heartbeat()
            
            # Log messaggi
            if line.startswith("["):
                print(f"[Arduino] {line}")
                return riceviDaArduino()
            
            try:
                valore = int(line)
                print(f"[← Arduino] {valore}")
                return valore
            except ValueError:
                print(f"[WARN] Dato non numerico: '{line}'")
                return None
        
        return None
        
    except Exception as e:
        print(f"[ERRORE] Ricezione fallita: {e}")
        return None

def chiudiSeriale():
    """Chiude connessione seriale"""
    global ser
    
    system_monitor.stop_monitoring()
    
    if ser:
        ser.close()
        print("[INFO] Connessione seriale chiusa")

# =================== FUNZIONI CAMERA ===================

def inizializzaCamera():
    """Inizializza camera USB"""
    global cap
    
    try:
        cap = cv2.VideoCapture(CAMERA_INDEX)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAMERA_WIDTH)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAMERA_HEIGHT)
        
        ret, frame = cap.read()
        if not ret:
            print("[ERRORE] Impossibile leggere dalla camera")
            return False
        
        print(f"[INFO] Camera OK: {frame.shape[1]}x{frame.shape[0]}")
        return True
        
    except Exception as e:
        print(f"[ERRORE] Inizializzazione camera fallita: {e}")
        return False

def rilasciaCamera():
    """Rilascia camera"""
    global cap
    
    if cap:
        cap.release()
        print("[INFO] Camera rilasciata")

# =================== RILEVAZIONE COLORI ===================

def rilevaColore(frame):
    """Rileva colori su frame"""
    
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    
    # Range HSV
    lower_red1 = np.array([0, 100, 100])
    upper_red1 = np.array([10, 255, 255])
    lower_red2 = np.array([170, 100, 100])
    upper_red2 = np.array([180, 255, 255])
    
    mask_red1 = cv2.inRange(hsv, lower_red1, upper_red1)
    mask_red2 = cv2.inRange(hsv, lower_red2, upper_red2)
    mask_red = cv2.bitwise_or(mask_red1, mask_red2)
    
    lower_yellow = np.array([20, 100, 100])
    upper_yellow = np.array([30, 255, 255])
    mask_yellow = cv2.inRange(hsv, lower_yellow, upper_yellow)
    
    lower_green = np.array([40, 50, 50])
    upper_green = np.array([80, 255, 255])
    mask_green = cv2.inRange(hsv, lower_green, upper_green)
    
    red_pixels = cv2.countNonZero(mask_red)
    yellow_pixels = cv2.countNonZero(mask_yellow)
    green_pixels = cv2.countNonZero(mask_green)
    
    print(f"[DEBUG] Pixel - R:{red_pixels} Y:{yellow_pixels} G:{green_pixels}")
    
    colori_rilevati = 0
    if red_pixels > MIN_AREA_COLORE:
        colori_rilevati += 1
    if yellow_pixels > MIN_AREA_COLORE:
        colori_rilevati += 1
    if green_pixels > MIN_AREA_COLORE:
        colori_rilevati += 1
    
    if colori_rilevati > 1:
        print("[INFO] Più colori rilevati → CODE_NESSUNA")
        return CODE_NESSUNA_RILEVAZIONE
    
    if red_pixels > MIN_AREA_COLORE:
        print("[INFO] Rilevato ROSSO")
        return CODE_ROSSO
    elif yellow_pixels > MIN_AREA_COLORE:
        print("[INFO] Rilevato GIALLO")
        return CODE_GIALLO
    elif green_pixels > MIN_AREA_COLORE:
        print("[INFO] Rilevato VERDE")
        return CODE_VERDE
    
    return CODE_NESSUNA_RILEVAZIONE

# =================== RILEVAZIONE LETTERE ===================

def rilevaLettera(frame):
    """Rileva lettere greche"""
    
    try:
        import pytesseract
        
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        _, thresh = cv2.threshold(gray, 127, 255, cv2.THRESH_BINARY_INV)
        
        config = '--psm 10 -l ell'
        text = pytesseract.image_to_string(thresh, config=config).strip()
        
        print(f"[DEBUG] OCR: '{text}'")
        
        if 'Φ' in text or 'φ' in text:
            print("[INFO] Rilevata lettera Φ (PHI)")
            return CODE_PHI
        elif 'Ψ' in text or 'ψ' in text:
            print("[INFO] Rilevata lettera Ψ (PSI)")
            return CODE_PSI
        elif 'Ω' in text or 'ω' in text:
            print("[INFO] Rilevata lettera Ω (OMEGA)")
            return CODE_OMEGA
        
    except Exception as e:
        print(f"[WARN] OCR fallito: {e}")
    
    return CODE_NESSUNA_RILEVAZIONE

# =================== ANALISI FOTO ===================

def scattaFotoEAnalizza():
    """Scatta foto e rileva colori/lettere"""
    global cap
    
    if cap is None:
        print("[ERRORE] Camera non inizializzata")
        return CODE_NESSUNA_RILEVAZIONE
    
    ret, frame = cap.read()
    if not ret:
        print("[ERRORE] Impossibile catturare frame")
        return CODE_NESSUNA_RILEVAZIONE
    
    print("[INFO] Frame acquisito")
    
    codice_colore = rilevaColore(frame)
    if codice_colore != CODE_NESSUNA_RILEVAZIONE:
        return codice_colore
    
    codice_lettera = rilevaLettera(frame)
    return codice_lettera

# =================== NAVIGAZIONE ===================

def calcolaCoordinataCellaFrontale():
    """Calcola coordinate cella davanti"""
    global cellaCorrente, direzione
    
    x = cellaCorrente.getX()
    y = cellaCorrente.getY()
    
    dir_mod = direzione % 4
    
    if dir_mod == 0:
        return Cella(x, y + 1)
    elif dir_mod == 1:
        return Cella(x + 1, y)
    elif dir_mod == 2:
        return Cella(x, y - 1)
    else:
        return Cella(x - 1, y)

def calcolaCoordinataCellaDestra():
    """Calcola coordinate cella a destra"""
    global direzione
    
    direzione_temp = direzione + 1
    return calcolaCoordinataCellaByDir(direzione_temp)

def calcolaCoordinataCellaSinistra():
    """Calcola coordinate cella a sinistra"""
    global direzione
    
    direzione_temp = direzione - 1
    return calcolaCoordinataCellaByDir(direzione_temp)

def calcolaCoordinataCellaByDir(dir_value):
    """Calcola cella data una direzione"""
    global cellaCorrente
    
    x = cellaCorrente.getX()
    y = cellaCorrente.getY()
    
    dir_mod = dir_value % 4
    
    if dir_mod == 0:
        return Cella(x, y + 1)
    elif dir_mod == 1:
        return Cella(x + 1, y)
    elif dir_mod == 2:
        return Cella(x, y - 1)
    else:
        return Cella(x - 1, y)

def scegliDirezione():
    """Sceglie prossima direzione"""
    global listaCelleNonVisitate
    
    if not listaCelleNonVisitate:
        print("[INFO] Nessuna cella da visitare → TERMINA")
        return DIR_TERMINA
    
    prossima = listaCelleNonVisitate.pop(0)
    
    print(f"[INFO] Prossima cella target: {prossima}")
    
    dx = prossima.getX() - cellaCorrente.getX()
    dy = prossima.getY() - cellaCorrente.getY()
    
    if dx == 0 and dy == 1:
        dir_target = 0
    elif dx == 1 and dy == 0:
        dir_target = 1
    elif dx == 0 and dy == -1:
        dir_target = 2
    elif dx == -1 and dy == 0:
        dir_target = 3
    else:
        print(f"[WARN] Cella non adiacente: {prossima}")
        return DIR_TERMINA
    
    diff = (dir_target - direzione) % 4
    
    if diff == 0:
        return DIR_DRITTO
    elif diff == 1:
        return DIR_DESTRA
    elif diff == 3:
        return DIR_SINISTRA
    else:
        return DIR_DIETRO

def eseguiMovimento(dir_scelta):
    """Aggiorna posizione dopo movimento"""
    global cellaCorrente, direzione
    
    if dir_scelta == DIR_DRITTO:
        cellaCorrente = calcolaCoordinataCellaFrontale()
    elif dir_scelta == DIR_DESTRA:
        direzione += 1
        cellaCorrente = calcolaCoordinataCellaFrontale()
    elif dir_scelta == DIR_SINISTRA:
        direzione -= 1
        cellaCorrente = calcolaCoordinataCellaFrontale()
    elif dir_scelta == DIR_DIETRO:
        direzione += 2
        cellaCorrente = calcolaCoordinataCellaFrontale()
    
    print(f"[INFO] Nuova posizione: {cellaCorrente}, direzione: {direzione % 4}")

# =================== SCANSIONE AMBIENTE ===================

def guardaIntorno():
    """Scansiona fronte, destra, sinistra"""
    global listaCelleNonVisitate, listaCelleNonPercorribili
    
    print("\n[===== SCANSIONE AMBIENTE =====]")
    
    # FRONTE
    print("[1/3] Scansione FRONTALE...")
    muro_fronte = riceviDaArduino()
    
    if muro_fronte == 1:
        print("  Muro presente → rilevo colori/lettere")
        codice = scattaFotoEAnalizza()
        inviaAdArduino(codice)
    else:
        print("  Muro assente → aggiungo cella")
        cella_fronte = calcolaCoordinataCellaFrontale()
        
        if not memoriaCelle.contiene(cella_fronte) and \
           cella_fronte not in listaCelleNonVisitate and \
           cella_fronte not in listaCelleNonPercorribili:
            listaCelleNonVisitate.append(cella_fronte)
            print(f"  + Aggiunta cella: {cella_fronte}")
        
        inviaAdArduino(CODE_NESSUNA_RILEVAZIONE)
    
    # DESTRA
    print("[2/3] Scansione DESTRA...")
    muro_destra = riceviDaArduino()
    
    if muro_destra == 1:
        print("  Muro presente → rilevo colori/lettere")
        codice = scattaFotoEAnalizza()
        inviaAdArduino(codice)
    else:
        print("  Muro assente → aggiungo cella")
        cella_destra = calcolaCoordinataCellaDestra()
        
        if not memoriaCelle.contiene(cella_destra) and \
           cella_destra not in listaCelleNonVisitate and \
           cella_destra not in listaCelleNonPercorribili:
            listaCelleNonVisitate.append(cella_destra)
            print(f"  + Aggiunta cella: {cella_destra}")
        
        inviaAdArduino(CODE_NESSUNA_RILEVAZIONE)
    
    # SINISTRA
    print("[3/3] Scansione SINISTRA...")
    muro_sinistra = riceviDaArduino()
    
    if muro_sinistra == 1:
        print("  Muro presente → rilevo colori/lettere")
        codice = scattaFotoEAnalizza()
        inviaAdArduino(codice)
    else:
        print("  Muro assente → aggiungo cella")
        cella_sinistra = calcolaCoordinataCellaSinistra()
        
        if not memoriaCelle.contiene(cella_sinistra) and \
           cella_sinistra not in listaCelleNonVisitate and \
           cella_sinistra not in listaCelleNonPercorribili:
            listaCelleNonVisitate.append(cella_sinistra)
            print(f"  + Aggiunta cella: {cella_sinistra}")
        
        inviaAdArduino(CODE_NESSUNA_RILEVAZIONE)
    
    print("[===== SCANSIONE COMPLETATA =====]\n")
    
    conferma = riceviDaArduino()
    if conferma != 1:
        print(f"[WARN] Conferma attesa=1, ricevuto={conferma}")

# =================== CICLO PRINCIPALE ===================

def main():
    """Ciclo principale navigazione"""
    global cellaCorrente, memoriaCelle, listaCelleNonPercorribili
    
    print("=" * 70)
    print("  ROBOCUP RESCUE MAZE 2026 - RASPBERRY PI V2")
    print("  Sistema Heartbeat/Watchdog attivo")
    print("=" * 70)
    print()
    
    if not inizializzaSeriale():
        return False
    
    if not inizializzaCamera():
        chiudiSeriale()
        return False
    
    memoriaCelle.aggiungi(cellaCorrente)
    print(f"[INFO] Posizione iniziale: {cellaCorrente}")
    print()
    
    try:
        ciclo = 0
        
        while True:
            # Controlla stato Arduino
            if not system_monitor.is_arduino_alive():
                print("[ERRORE] Arduino disconnesso, attendo riconnessione...")
                time.sleep(2)
                continue
            
            ciclo += 1
            print(f"\n{'='*70}")
            print(f"  CICLO {ciclo}")
            print(f"  Posizione: {cellaCorrente} | Direzione: {direzione % 4}")
            print(f"  Visitate: {len(memoriaCelle)} | Da visitare: {len(listaCelleNonVisitate)}")
            print(f"  Arduino: {'✓ ALIVE' if system_monitor.is_arduino_alive() else '✗ TIMEOUT'}")
            print(f"{'='*70}\n")
            
            guardaIntorno()
            
            direzione_scelta = scegliDirezione()
            
            if direzione_scelta == DIR_TERMINA:
                print("\n[INFO] Esplorazione completata!")
                break
            
            print(f"[INFO] Direzione scelta: {direzione_scelta}")
            inviaAdArduino(direzione_scelta)
            
            esito_movimento = riceviDaArduino()
            
            if esito_movimento == 1:
                print("[INFO] Movimento completato con successo")
                
                eseguiMovimento(direzione_scelta)
                memoriaCelle.aggiungi(cellaCorrente)
                
                inviaAdArduino(1)
                
            else:
                print("[WARN] Movimento fallito (ostacolo rilevato)")
                
                cella_bloccata = calcolaCoordinataCellaFrontale()
                
                if cella_bloccata not in listaCelleNonPercorribili:
                    listaCelleNonPercorribili.append(cella_bloccata)
                    print(f"  + Cella non percorribile: {cella_bloccata}")
                
                if cella_bloccata in listaCelleNonVisitate:
                    listaCelleNonVisitate.remove(cella_bloccata)
            
            time.sleep(0.5)
    
    except KeyboardInterrupt:
        print("\n\n[INFO] Interruzione manuale")
    
    finally:
        rilasciaCamera()
        chiudiSeriale()
        print("\n[INFO] Sistema terminato correttamente")
    
    return True

if __name__ == "__main__":
    try:
        success = main()
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"\n[ERRORE CRITICO] {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
