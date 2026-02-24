#!/usr/bin/env python3
"""
RoboCup Rescue Maze 2026 - Raspberry Pi V1
Versione base senza Heartbeat/Watchdog
"""

import cv2
import numpy as np
import serial
import time
import sys
import os
from collections import deque

# =================== CONFIGURAZIONE ===================

# Seriale
SERIAL_PORT = "/dev/ttyACM0"
BAUDRATE = 115200
SERIAL_TIMEOUT = 5

# Camera
CAMERA_INDEX = 0
CAMERA_WIDTH = 640
CAMERA_HEIGHT = 480

# Codici comunicazione Arduino
CODE_NESSUNA_RILEVAZIONE = 0
CODE_ROSSO = 1
CODE_GIALLO = 2
CODE_VERDE = 3
CODE_PHI = 4      # Φ - Harmed
CODE_PSI = 5      # Ψ - Secured
CODE_OMEGA = 6    # Ω - Unharmed

# Codici direzione
DIR_TERMINA = 0
DIR_DRITTO = 1
DIR_DESTRA = 2
DIR_SINISTRA = 3
DIR_DIETRO = 4

# Soglie rilevazione
MIN_AREA_COLORE = 300  # pixel minimi per colore valido
MIN_CONTOUR_AREA = 100

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

# Seriale
ser = None

# Camera
cap = None

# Stato robot
cellaCorrente = Cella(0, 0)
direzione = 0  # 0=iniziale, poi cambia con rotazioni

# Memoria labirinto
memoriaCelle = MemoriaCelle()
listaCelleNonVisitate = []
listaCelleNonPercorribili = []

# =================== FUNZIONI SERIALE ===================

def inizializzaSeriale():
    """Inizializza connessione seriale con Arduino"""
    global ser
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=SERIAL_TIMEOUT)
        time.sleep(2)  # Attendi reset Arduino
        
        print(f"[INFO] Connessione seriale OK: {SERIAL_PORT} @ {BAUDRATE}")
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
        # Svuota buffer vecchio
        ser.reset_input_buffer()
        
        # Leggi linea
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        
        if line:
            # Gestisci log da Arduino
            if line.startswith("["):
                print(f"[Arduino] {line}")
                return riceviDaArduino()  # Riprova per dato vero
            
            try:
                valore = int(line)
                print(f"[← Arduino] {valore}")
                return valore
            except ValueError:
                print(f"[WARN] Dato non numerico ricevuto: '{line}'")
                return None
        
        return None
        
    except Exception as e:
        print(f"[ERRORE] Ricezione fallita: {e}")
        return None

def chiudiSeriale():
    """Chiude connessione seriale"""
    global ser
    
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
        
        # Test lettura
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
    """
    Rileva colori su frame
    Ritorna: CODE_ROSSO, CODE_GIALLO, CODE_VERDE o CODE_NESSUNA_RILEVAZIONE
    """
    
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    
    # Range HSV per colori
    # ROSSO (due range per hue wrap-around)
    lower_red1 = np.array([0, 100, 100])
    upper_red1 = np.array([10, 255, 255])
    lower_red2 = np.array([170, 100, 100])
    upper_red2 = np.array([180, 255, 255])
    
    mask_red1 = cv2.inRange(hsv, lower_red1, upper_red1)
    mask_red2 = cv2.inRange(hsv, lower_red2, upper_red2)
    mask_red = cv2.bitwise_or(mask_red1, mask_red2)
    
    # GIALLO
    lower_yellow = np.array([20, 100, 100])
    upper_yellow = np.array([30, 255, 255])
    mask_yellow = cv2.inRange(hsv, lower_yellow, upper_yellow)
    
    # VERDE
    lower_green = np.array([40, 50, 50])
    upper_green = np.array([80, 255, 255])
    mask_green = cv2.inRange(hsv, lower_green, upper_green)
    
    # Conta pixel
    red_pixels = cv2.countNonZero(mask_red)
    yellow_pixels = cv2.countNonZero(mask_yellow)
    green_pixels = cv2.countNonZero(mask_green)
    
    print(f"[DEBUG] Pixel - R:{red_pixels} Y:{yellow_pixels} G:{green_pixels}")
    
    # Conta colori sopra soglia
    colori_rilevati = 0
    if red_pixels > MIN_AREA_COLORE:
        colori_rilevati += 1
    if yellow_pixels > MIN_AREA_COLORE:
        colori_rilevati += 1
    if green_pixels > MIN_AREA_COLORE:
        colori_rilevati += 1
    
    # Se più colori → nessuna rilevazione
    if colori_rilevati > 1:
        print("[INFO] Più colori rilevati → CODE_NESSUNA")
        return CODE_NESSUNA_RILEVAZIONE
    
    # Ritorna colore dominante
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
    """
    Rileva lettere greche Φ, Ψ, Ω
    Ritorna: CODE_PHI, CODE_PSI, CODE_OMEGA o CODE_NESSUNA_RILEVAZIONE
    """
    
    try:
        import pytesseract
        
        # Preprocessing
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        # Threshold per isolare nero su bianco
        _, thresh = cv2.threshold(gray, 127, 255, cv2.THRESH_BINARY_INV)
        
        # OCR con configurazione greco
        config = '--psm 10 -l ell'  # Single character, greco
        text = pytesseract.image_to_string(thresh, config=config).strip()
        
        print(f"[DEBUG] OCR: '{text}'")
        
        # Riconosci lettere
        if 'Φ' in text or 'φ' in text:
            print("[INFO] Rilevata lettera Φ (PHI)")
            return CODE_PHI
        elif 'Ψ' in text or 'ψ' in text:
            print("[INFO] Rilevata lettera Ψ (PSI)")
            return CODE_PSI
        elif 'Ω' in text or 'ω' in text:
            print("[INFO] Rilevata lettera Ω (OMEGA)")
            return CODE_OMEGA
        
        # Pattern matching alternativo (contorni)
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        for cnt in contours:
            area = cv2.contourArea(cnt)
            if area > MIN_CONTOUR_AREA:
                x, y, w, h = cv2.boundingRect(cnt)
                aspect_ratio = w / float(h)
                
                # Φ: circa tondo con barra verticale
                if 0.6 < aspect_ratio < 1.4:
                    print("[INFO] Possibile Φ da forma")
                    return CODE_PHI
        
    except Exception as e:
        print(f"[WARN] OCR fallito: {e}")
    
    return CODE_NESSUNA_RILEVAZIONE

# =================== ANALISI FOTO ===================

def scattaFotoEAnalizza():
    """
    Scatta foto e rileva colori/lettere
    Ritorna codice rilevazione
    """
    global cap
    
    if cap is None:
        print("[ERRORE] Camera non inizializzata")
        return CODE_NESSUNA_RILEVAZIONE
    
    # Cattura frame
    ret, frame = cap.read()
    if not ret:
        print("[ERRORE] Impossibile catturare frame")
        return CODE_NESSUNA_RILEVAZIONE
    
    print("[INFO] Frame acquisito")
    
    # Prima rileva colori
    codice_colore = rilevaColore(frame)
    if codice_colore != CODE_NESSUNA_RILEVAZIONE:
        return codice_colore
    
    # Poi lettere
    codice_lettera = rilevaLettera(frame)
    return codice_lettera

# =================== NAVIGAZIONE ===================

def calcolaCoordinataCellaFrontale():
    """Calcola coordinate cella davanti al robot"""
    global cellaCorrente, direzione
    
    x = cellaCorrente.getX()
    y = cellaCorrente.getY()
    
    # Direzione: 0=N, 1=E, 2=S, 3=W
    dir_mod = direzione % 4
    
    if dir_mod == 0:  # Nord
        return Cella(x, y + 1)
    elif dir_mod == 1:  # Est
        return Cella(x + 1, y)
    elif dir_mod == 2:  # Sud
        return Cella(x, y - 1)
    else:  # Ovest (3)
        return Cella(x - 1, y)

def calcolaCoordinataCellaDestra():
    """Calcola coordinate cella a destra"""
    global direzione
    
    # Ruota direzione di +90°
    direzione_temp = direzione + 1
    return calcolaCoordinataCellaByDir(direzione_temp)

def calcolaCoordinataCellaSinistra():
    """Calcola coordinate cella a sinistra"""
    global direzione
    
    # Ruota direzione di -90°
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
    """
    Sceglie prossima direzione da percorrere
    Ritorna: DIR_DRITTO, DIR_DESTRA, DIR_SINISTRA, DIR_DIETRO, DIR_TERMINA
    """
    global listaCelleNonVisitate
    
    if not listaCelleNonVisitate:
        print("[INFO] Nessuna cella da visitare → TERMINA")
        return DIR_TERMINA
    
    # Prendi prima cella (BFS)
    prossima = listaCelleNonVisitate.pop(0)
    
    print(f"[INFO] Prossima cella target: {prossima}")
    
    # Calcola direzione relativa
    dx = prossima.getX() - cellaCorrente.getX()
    dy = prossima.getY() - cellaCorrente.getY()
    
    # Determina direzione assoluta target
    if dx == 0 and dy == 1:
        dir_target = 0  # Nord
    elif dx == 1 and dy == 0:
        dir_target = 1  # Est
    elif dx == 0 and dy == -1:
        dir_target = 2  # Sud
    elif dx == -1 and dy == 0:
        dir_target = 3  # Ovest
    else:
        # Cella non adiacente
        print(f"[WARN] Cella non adiacente: {prossima}")
        return DIR_TERMINA
    
    # Calcola rotazione necessaria
    diff = (dir_target - direzione) % 4
    
    if diff == 0:
        return DIR_DRITTO
    elif diff == 1:
        return DIR_DESTRA
    elif diff == 3:
        return DIR_SINISTRA
    else:  # diff == 2
        return DIR_DIETRO

def eseguiMovimento(dir_scelta):
    """Aggiorna posizione e direzione dopo movimento riuscito"""
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
    """
    Scansiona fronte, destra, sinistra
    Aggiorna liste celle
    """
    global listaCelleNonVisitate, listaCelleNonPercorribili
    
    print("\n[===== SCANSIONE AMBIENTE =====]")
    
    # --- FRONTE ---
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
    
    # --- DESTRA ---
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
    
    # --- SINISTRA ---
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
    
    # Arduino deve inviare "1" per confermare fine scansione
    conferma = riceviDaArduino()
    if conferma != 1:
        print(f"[WARN] Conferma attesa=1, ricevuto={conferma}")

# =================== CICLO PRINCIPALE ===================

def main():
    """Ciclo principale navigazione"""
    global cellaCorrente, memoriaCelle, listaCelleNonPercorribili
    
    print("=" * 70)
    print("  ROBOCUP RESCUE MAZE 2026 - RASPBERRY PI V1")
    print("=" * 70)
    print()
    
    # Inizializzazione
    if not inizializzaSeriale():
        return False
    
    if not inizializzaCamera():
        chiudiSeriale()
        return False
    
    # Aggiungi cella iniziale
    memoriaCelle.aggiungi(cellaCorrente)
    print(f"[INFO] Posizione iniziale: {cellaCorrente}")
    print()
    
    try:
        ciclo = 0
        
        while True:
            ciclo += 1
            print(f"\n{'='*70}")
            print(f"  CICLO {ciclo}")
            print(f"  Posizione: {cellaCorrente} | Direzione: {direzione % 4}")
            print(f"  Visitate: {len(memoriaCelle)} | Da visitare: {len(listaCelleNonVisitate)}")
            print(f"{'='*70}\n")
            
            # Scansiona ambiente
            guardaIntorno()
            
            # Scegli prossima direzione
            direzione_scelta = scegliDirezione()
            
            if direzione_scelta == DIR_TERMINA:
                print("\n[INFO] Esplorazione completata!")
                break
            
            # Invia direzione ad Arduino
            print(f"[INFO] Direzione scelta: {direzione_scelta}")
            inviaAdArduino(direzione_scelta)
            
            # Attendi esito movimento
            esito_movimento = riceviDaArduino()
            
            if esito_movimento == 1:
                print("[INFO] Movimento completato con successo")
                
                # Aggiorna stato
                eseguiMovimento(direzione_scelta)
                memoriaCelle.aggiungi(cellaCorrente)
                
                # Conferma ad Arduino
                inviaAdArduino(1)
                
            else:
                print("[WARN] Movimento fallito (ostacolo rilevato)")
                
                # Segna cella come non percorribile
                cella_bloccata = calcolaCoordinataCellaFrontale()
                
                if cella_bloccata not in listaCelleNonPercorribili:
                    listaCelleNonPercorribili.append(cella_bloccata)
                    print(f"  + Cella non percorribile: {cella_bloccata}")
                
                # Rimuovi da non visitate se presente
                if cella_bloccata in listaCelleNonVisitate:
                    listaCelleNonVisitate.remove(cella_bloccata)
            
            time.sleep(0.5)
    
    except KeyboardInterrupt:
        print("\n\n[INFO] Interruzione manuale")
    
    finally:
        # Cleanup
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
