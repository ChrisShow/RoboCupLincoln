#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RoboCup Junior 2026 - Rescue Maze
Raspberry Pi 4B - Sistema Navigazione + Visione
Comunicazione USB Serial (115200 baud) con Arduino Mega
"""

import cv2
import os
import sys
import numpy as np
import pytesseract
import serial
import threading
import time
from datetime import datetime
from queue import Queue

# ============================================================================
# CONFIGURAZIONE GLOBALE
# ============================================================================

SERIAL_PORT = "/dev/ttyUSB0"  # TODO: Verificare porta (potrebbe essere ttyACM0, ttyUSB0, etc)
SERIAL_BAUD = 115200
SERIAL_TIMEOUT = 2.0  # secondi

MOVEMENT_TIMEOUT = 3.0  # secondi, attesa risposta movimento da Arduino
COLOR_THRESHOLD = 300  # pixel minimi per rilevare colore

IMG_SAVE_PATH = "/home/robottino/Desktop/venv/colorLetters/images"
CAMERA_DEVICE = 0  # /dev/video0

# Direzioni logiche
DIR_NORTH = 0
DIR_EAST = 1
DIR_WEST = 2
DIR_SOUTH = 3

# Direzioni di movimento relative alla posa corrente
MOVE_FORWARD = 0
MOVE_RIGHT = 1
MOVE_LEFT = 2
MOVE_BACKWARD = 3

# ============================================================================
# CLASSE CELLA
# ============================================================================

class Cella:
    """Rappresenta una cella del labirinto con coordinate (x, y)"""
    
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def confronto(self, x, y):
        """Confronta coordinate"""
        return self.x == x and self.y == y
    
    def __eq__(self, other):
        """Uguaglianza tra oggetti Cella"""
        if not isinstance(other, Cella):
            return False
        return self.x == other.x and self.y == other.y
    
    def __hash__(self):
        """Hash per uso in set/dict"""
        return hash((self.x, self.y))
    
    def __repr__(self):
        return f"Cella({self.x}, {self.y})"
    
    def getX(self):
        return self.x
    
    def getY(self):
        return self.y
    
    def setX(self, x):
        self.x = x
    
    def setY(self, y):
        self.y = y


# ============================================================================
# CLASSE MEMORIA CELLE
# ============================================================================

class MemoriaCelle:
    """Traccia celle visitate (ordine: più recente in testa)"""
    
    def __init__(self):
        self.memoria = []
    
    def push(self, elemento):
        """Inserisce cella in testa (ultima visitata)"""
        if elemento is None:
            return
        self.memoria.insert(0, elemento)
    
    def storicoCella(self, elemento):
        """Ritorna posizione cella nella memoria (-1 se non visitata, -2 se None)"""
        if elemento is None:
            return -2
        for i, cella in enumerate(self.memoria):
            if cella == elemento:
                return i
        return -1
    
    def cellaGiaVisitata(self, elemento):
        """Verifica se cella è stata visitata"""
        if elemento is None:
            return False
        return any(cella == elemento for cella in self.memoria)
    
    def __contains__(self, item):
        """Supporta 'if cella in memoria'"""
        return self.cellaGiaVisitata(item)
    
    def __len__(self):
        return len(self.memoria)
    
    def scegliDirezione(self, dritto, destra, sinistra):
        """
        Sceglie direzione basata su celle non visitate + recency.
        Ritorna: 0=DRITTO, 1=DESTRA, 2=SINISTRA, 3=DIETRO
        """
        # Controlla se celle non visitate
        trovata_dritto = self.cellaGiaVisitata(dritto)
        trovata_destra = self.cellaGiaVisitata(destra)
        trovata_sinistra = self.cellaGiaVisitata(sinistra)
        
        # Priorità 1: celle non ancora visitate
        if not trovata_dritto and dritto is not None:
            return MOVE_FORWARD
        if not trovata_destra and destra is not None:
            return MOVE_RIGHT
        if not trovata_sinistra and sinistra is not None:
            return MOVE_LEFT
        
        # Priorità 2: cella meno recentemente visitata (più lontana in memoria)
        storico_dritto = self.storicoCella(dritto) if dritto is not None else -1
        storico_destra = self.storicoCella(destra) if destra is not None else -1
        storico_sinistra = self.storicoCella(sinistra) if sinistra is not None else -1
        
        # Esclude -1 (non visitata) dal confronto
        score_dritto = storico_dritto if storico_dritto >= 0 else -999
        score_destra = storico_destra if storico_destra >= 0 else -999
        score_sinistra = storico_sinistra if storico_sinistra >= 0 else -999
        
        # Ritorna direzione con score massimo (= meno recentemente visitata)
        max_score = max(score_dritto, score_destra, score_sinistra)
        if max_score == -999:  # Nessuna cella disponibile
            return MOVE_BACKWARD
        
        if score_dritto == max_score:
            return MOVE_FORWARD
        if score_destra == max_score:
            return MOVE_RIGHT
        if score_sinistra == max_score:
            return MOVE_LEFT
        return MOVE_BACKWARD


# ============================================================================
# CLASSE COMUNICAZIONE ARDUINO
# ============================================================================

class ComunicazioneArduino:
    """Gestisce comunicazione seriale con Arduino Mega"""
    
    def __init__(self, port=SERIAL_PORT, baud=SERIAL_BAUD, timeout=SERIAL_TIMEOUT):
        try:
            self.ser = serial.Serial(port, baud, timeout=timeout)
            time.sleep(2)  # Reset Arduino
            print(f"[OK] Connessione seriale aperta: {port} @ {baud} baud")
        except Exception as e:
            print(f"[ERRORE] Impossibile aprire seriale: {e}")
            sys.exit(1)
        
        self.lock = threading.Lock()
    
    def invia_comando_movimento(self, direzione):
        """Invia direzione movimento (0-3) ad Arduino"""
        try:
            with self.lock:
                msg = f"{direzione}\n"
                self.ser.write(msg.encode())
                print(f"[INFO] Comando movimento inviato: {direzione}")
        except Exception as e:
            print(f"[ERRORE] Errore invio comando: {e}")
    
    def leggi_sensori_ir(self):
        """Legge stato sensori IR da Arduino (formato: "1,0,1")"""
        try:
            with self.lock:
                self.ser.reset_input_buffer()
                line = self.ser.readline().decode().strip()
            
            if not line:
                print("[AVVISO] Timeout lettura sensori IR")
                return None
            
            parts = line.split(",")
            if len(parts) != 3:
                print(f"[AVVISO] Formato sensori IR invalido: {line}")
                return None
            
            dritto = int(parts[0])
            destra = int(parts[1])
            sinistra = int(parts[2])
            
            print(f"[INFO] Sensori IR: DRITTO={dritto}, DESTRA={destra}, SINISTRA={sinistra}")
            return (dritto, destra, sinistra)
        
        except Exception as e:
            print(f"[ERRORE] Errore lettura sensori: {e}")
            return None
    
    def leggi_feedback_movimento(self):
        """Legge feedback movimento (1=successo, -1=fallito)"""
        try:
            with self.lock:
                line = self.ser.readline().decode().strip()
            
            if not line:
                print("[AVVISO] Timeout feedback movimento")
                return None
            
            feedback = int(line)
            print(f"[INFO] Feedback movimento: {feedback}")
            return feedback
        
        except Exception as e:
            print(f"[ERRORE] Errore lettura feedback: {e}")
            return None
    
    def invia_comando_medikit(self):
        """Invia comando rilascio medikit"""
        try:
            with self.lock:
                self.ser.write(b"M\n")
                print("[INFO] Comando medikit inviato")
        except Exception as e:
            print(f"[ERRORE] Errore invio comando medikit: {e}")
    
    def chiudi(self):
        """Chiude connessione seriale"""
        if self.ser.is_open:
            self.ser.close()
            print("[OK] Connessione seriale chiusa")


# ============================================================================
# FUNZIONI VISIONE (COLORI E OCR)
# ============================================================================

def detect_colors_in_image(image):
    """Rileva colori rosso, giallo, verde in immagine"""
    hsv_image = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    
    # Rosso (due intervalli per wrapping hue)
    lower_red1 = np.array([0, 100, 100])
    upper_red1 = np.array([10, 255, 255])
    lower_red2 = np.array([170, 100, 100])
    upper_red2 = np.array([179, 255, 255])
    
    # Giallo
    lower_yellow = np.array([20, 100, 100])
    upper_yellow = np.array([40, 255, 255])
    
    # Verde
    lower_green = np.array([40, 100, 100])
    upper_green = np.array([80, 255, 255])
    
    # Masking
    mask_red1 = cv2.inRange(hsv_image, lower_red1, upper_red1)
    mask_red2 = cv2.inRange(hsv_image, lower_red2, upper_red2)
    mask_red = cv2.bitwise_or(mask_red1, mask_red2)
    mask_yellow = cv2.inRange(hsv_image, lower_yellow, upper_yellow)
    mask_green = cv2.inRange(hsv_image, lower_green, upper_green)
    
    # Blur per ridurre rumore
    mask_red = cv2.GaussianBlur(mask_red, (5, 5), 0)
    mask_yellow = cv2.GaussianBlur(mask_yellow, (5, 5), 0)
    mask_green = cv2.GaussianBlur(mask_green, (5, 5), 0)
    
    # Conta pixel
    red_pixels = cv2.countNonZero(mask_red)
    yellow_pixels = cv2.countNonZero(mask_yellow)
    green_pixels = cv2.countNonZero(mask_green)
    
    has_red = red_pixels >= COLOR_THRESHOLD
    has_yellow = yellow_pixels >= COLOR_THRESHOLD
    has_green = green_pixels >= COLOR_THRESHOLD
    
    print(f"[DEBUG] Colori - R:{red_pixels} Y:{yellow_pixels} G:{green_pixels}")
    
    return {
        "rosso": has_red,
        "giallo": has_yellow,
        "verde": has_green,
    }


def extract_text_regions(image):
    """Estrae regioni contesto usando edge detection"""
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    gray_inv = cv2.bitwise_not(gray)
    _, thresh = cv2.threshold(gray_inv, 150, 255, cv2.THRESH_BINARY)
    
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    rois = []
    for contour in contours:
        area = cv2.contourArea(contour)
        if area > 1000:
            x, y, w, h = cv2.boundingRect(contour)
            padding = 20
            x_start = max(0, x - padding)
            y_start = max(0, y - padding)
            x_end = min(image.shape[1], x + w + padding)
            y_end = min(image.shape[0], y + h + padding)
            
            roi = image[y_start:y_end, x_start:x_end]
            if roi.size > 0:
                rois.append(roi)
    
    return rois


def preprocess_for_ocr(image):
    """Preprocessing per OCR lettere grandi"""
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    
    clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
    gray = clahe.apply(gray)
    
    h, w = gray.shape
    if h > 400:
        scale = 400 / h
        gray = cv2.resize(gray, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
    
    thresh = cv2.adaptiveThreshold(
        gray, 255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY,
        blockSize=21,
        C=5
    )
    
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    thresh = cv2.morphologyEx(thresh, cv2.MORPH_CLOSE, kernel, iterations=1)
    thresh = cv2.morphologyEx(thresh, cv2.MORPH_OPEN, kernel, iterations=1)
    thresh = cv2.GaussianBlur(thresh, (3, 3), 0)
    
    return thresh


def detect_letters_HSU(image):
    """Rileva lettere H, S, U con OCR"""
    found_letters = set()
    
    custom_config = (
        r"--psm 11 "
        r"--oem 3 "
        r"-c tessedit_char_whitelist=HSU "
        r"-c load_system_dawg=0 "
        r"-c load_freq_dawg=0"
    )
    
    rois = extract_text_regions(image)
    if not rois:
        rois = [image]
    
    for idx, roi in enumerate(rois):
        for angle in [0, 90, 180, 270]:
            proc = preprocess_for_ocr(roi)
            
            if angle == 0:
                rotated = proc
            elif angle == 90:
                rotated = cv2.rotate(proc, cv2.ROTATE_90_CLOCKWISE)
            elif angle == 180:
                rotated = cv2.rotate(proc, cv2.ROTATE_180)
            else:  # 270
                rotated = cv2.rotate(proc, cv2.ROTATE_90_COUNTERCLOCKWISE)
            
            try:
                text = pytesseract.image_to_string(rotated, config=custom_config)
                text = text.upper().strip()
                
                for ch in text:
                    if ch in ["H", "S", "U"]:
                        found_letters.add(ch)
            except Exception as e:
                pass
    
    return found_letters


# ============================================================================
# FUNZIONE ACQUISIZIONE FOTO
# ============================================================================

def scattaFoto(direzione_label=""):
    """
    Scatta foto, analizza colori e lettere, ritorna codice azione.
    Codici: 0=nulla, 1=rosso, 2=giallo, 3=verde, 4=H, 5=S, 6=U
    """
    if not os.path.exists(IMG_SAVE_PATH):
        try:
            os.makedirs(IMG_SAVE_PATH, exist_ok=True)
        except Exception as e:
            print(f"[ERRORE] Directory: {e}")
            return 0
    
    cap = cv2.VideoCapture(CAMERA_DEVICE, cv2.CAP_V4L2)
    if not cap.isOpened():
        print("[ERRORE] Camera non disponibile")
        return 0
    
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
    cap.set(cv2.CAP_PROP_FPS, 30)
    
    # Riscaldamento camera
    for _ in range(10):
        cap.read()
    
    ret, frame = cap.read()
    if not ret:
        print("[ERRORE] Impossibile acquisire frame")
        cap.release()
        return 0
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"foto_{timestamp}_{direzione_label}.jpg"
    filepath = os.path.join(IMG_SAVE_PATH, filename)
    
    cv2.imwrite(filepath, frame)
    cap.release()
    
    # ANALISI IMMAGINE
    color_res = detect_colors_in_image(frame)
    
    code = 0
    
    if color_res["rosso"]:
        code = 1
        print("[DECISION] Codice 1: ROSSO")
    elif color_res["giallo"]:
        code = 2
        print("[DECISION] Codice 2: GIALLO")
    elif color_res["verde"]:
        code = 3
        print("[DECISION] Codice 3: VERDE")
    else:
        letters = detect_letters_HSU(frame)
        if "H" in letters:
            code = 4
            print("[DECISION] Codice 4: H")
        elif "S" in letters:
            code = 5
            print("[DECISION] Codice 5: S")
        elif "U" in letters:
            code = 6
            print("[DECISION] Codice 6: U")
    
    return code


# ============================================================================
# FUNZIONE PRINCIPALE
# ============================================================================

def main():
    """Loop principale: esplorazione labirinto"""
    
    # Inizializzazione
    arduino = ComunicazioneArduino()
    memoria_celle = MemoriaCelle()
    lista_celle_non_visitate = []
    lista_celle_non_percorribili = set()
    
    cella_corrente = Cella(0, 0)
    memoria_celle.push(cella_corrente)
    
    direzione = DIR_NORTH  # Direzione iniziale (0)
    medikit_rilasciati = 0
    
    print("[INFO] Inizio esplorazione labirinto")
    
    # ========== LOOP PRINCIPALE ==========
    iteration = 0
    while lista_celle_non_visitate:
        iteration += 1
        print(f"\n{'='*60}")
        print(f"ITERAZIONE {iteration}")
        print(f"Cella corrente: {cella_corrente}")
        print(f"Direzione: {direzione}")
        print(f"Celle non visitate: {len(lista_celle_non_visitate)}")
        print(f"{'='*60}")
        
        # STEP 1: Leggi sensori infrarossi
        sensori = arduino.leggi_sensori_ir()
        if sensori is None:
            print("[ERRORE] Impossibile leggere sensori, aborto")
            break
        
        dritto_libero, destra_libera, sinistra_libera = sensori
        
        # STEP 2: Calcola celle adiacenti basato su direzione corrente
        # Nota: dritto_libero=1 significa NO MURO, dritto_libero=0 significa MURO
        
        if dritto_libero == 1:  # No muro davanti
            if direzione == DIR_NORTH:
                cella_dritto = Cella(cella_corrente.getX(), cella_corrente.getY() + 1)
            elif direzione == DIR_EAST:
                cella_dritto = Cella(cella_corrente.getX() + 1, cella_corrente.getY())
            elif direzione == DIR_WEST:
                cella_dritto = Cella(cella_corrente.getX() - 1, cella_corrente.getY())
            else:  # SOUTH
                cella_dritto = Cella(cella_corrente.getX(), cella_corrente.getY() - 1)
        else:
            cella_dritto = None
        
        if destra_libera == 1:  # No muro a destra
            if direzione == DIR_NORTH:
                cella_destra = Cella(cella_corrente.getX() + 1, cella_corrente.getY())
            elif direzione == DIR_EAST:
                cella_destra = Cella(cella_corrente.getX(), cella_corrente.getY() - 1)
            elif direzione == DIR_WEST:
                cella_destra = Cella(cella_corrente.getX(), cella_corrente.getY() + 1)
            else:  # SOUTH
                cella_destra = Cella(cella_corrente.getX() - 1, cella_corrente.getY())
        else:
            cella_destra = None
        
        if sinistra_libera == 1:  # No muro a sinistra
            if direzione == DIR_NORTH:
                cella_sinistra = Cella(cella_corrente.getX() - 1, cella_corrente.getY())
            elif direzione == DIR_EAST:
                cella_sinistra = Cella(cella_corrente.getX(), cella_corrente.getY() + 1)
            elif direzione == DIR_WEST:
                cella_sinistra = Cella(cella_corrente.getX(), cella_corrente.getY() - 1)
            else:  # SOUTH
                cella_sinistra = Cella(cella_corrente.getX() + 1, cella_corrente.getY())
        else:
            cella_sinistra = None
        
        # STEP 3: Aggiorna lista celle non visitate
        for cella in [cella_dritto, cella_destra, cella_sinistra]:
            if cella is not None and cella not in lista_celle_non_percorribili:
                if cella not in lista_celle_non_visitate:
                    lista_celle_non_visitate.append(cella)
        
        # STEP 4: Scatta foto se muro presente
        # Muro davanti
        if dritto_libero == 0:
            code = scattaFoto("fronte")
            if code in [1, 2, 3]:  # Colore
                print(f"[AZIONE] Colore rilevato: {code}, mostra su LCD")
            elif code in [4, 5, 6]:  # Lettera
                print(f"[AZIONE] Lettera rilevata: {code}, rilascia medikit")
                arduino.invia_comando_medikit()
                medikit_rilasciati += 1
        
        # Muro a destra
        if destra_libera == 0:
            code = scattaFoto("destra")
            if code in [1, 2, 3]:
                print(f"[AZIONE] Colore rilevato DESTRA: {code}")
            elif code in [4, 5, 6]:
                print(f"[AZIONE] Lettera rilevata DESTRA: {code}, rilascia medikit")
                arduino.invia_comando_medikit()
                medikit_rilasciati += 1
        
        # Muro a sinistra
        if sinistra_libera == 0:
            code = scattaFoto("sinistra")
            if code in [1, 2, 3]:
                print(f"[AZIONE] Colore rilevato SINISTRA: {code}")
            elif code in [4, 5, 6]:
                print(f"[AZIONE] Lettera rilevata SINISTRA: {code}, rilascia medikit")
                arduino.invia_comando_medikit()
                medikit_rilasciati += 1
        
        # STEP 5: Scegli prossima mossa
        if not lista_celle_non_visitate:
            print("[INFO] Nessuna cella non visitata, esplorazione completata")
            break
        
        prossima_direzione = memoria_celle.scegliDirezione(cella_dritto, cella_destra, cella_sinistra)
        print(f"[INFO] Prossima direzione: {prossima_direzione}")
        
        # STEP 6: Invia comando movimento ad Arduino
        arduino.invia_comando_movimento(prossima_direzione)
        
        # STEP 7: Aspetta feedback movimento
        feedback = arduino.leggi_feedback_movimento()
        if feedback is None:
            print("[ERRORE] Timeout feedback movimento")
            break
        
        if feedback == -1:
            # Movimento fallito (cella nera o ostacolo)
            print("[AVVISO] Movimento fallito, cella probabilmente nera")
            
            # Aggiungi cella alla lista non percorribili
            if prossima_direzione == MOVE_FORWARD and cella_dritto is not None:
                lista_celle_non_percorribili.add(cella_dritto)
                if cella_dritto in lista_celle_non_visitate:
                    lista_celle_non_visitate.remove(cella_dritto)
            elif prossima_direzione == MOVE_RIGHT and cella_destra is not None:
                lista_celle_non_percorribili.add(cella_destra)
                if cella_destra in lista_celle_non_visitate:
                    lista_celle_non_visitate.remove(cella_destra)
            elif prossima_direzione == MOVE_LEFT and cella_sinistra is not None:
                lista_celle_non_percorribili.add(cella_sinistra)
                if cella_sinistra in lista_celle_non_visitate:
                    lista_celle_non_visitate.remove(cella_sinistra)
        
        elif feedback == 1:
            # Movimento riuscito
            print("[OK] Movimento completato")
            
            # Aggiorna cella corrente e direzione
            if prossima_direzione == MOVE_FORWARD:
                cella_corrente = Cella(cella_dritto.getX(), cella_dritto.getY())
                # Direzione non cambia
            elif prossima_direzione == MOVE_RIGHT:
                cella_corrente = Cella(cella_destra.getX(), cella_destra.getY())
                direzione = (direzione + 1) % 4
            elif prossima_direzione == MOVE_LEFT:
                cella_corrente = Cella(cella_sinistra.getX(), cella_sinistra.getY())
                direzione = (direzione + 3) % 4  # -1 mod 4
            elif prossima_direzione == MOVE_BACKWARD:
                if prossima_direzione == MOVE_FORWARD:
                    cella_corrente = Cella(cella_dritto.getX(), cella_dritto.getY())
                else:
                    # TODO: Implementare logica MOVE_BACKWARD se necessario
                    pass
                direzione = (direzione + 2) % 4
            
            # Rimuovi dalla lista non visitate e aggiungi a memoria
            if cella_corrente in lista_celle_non_visitate:
                lista_celle_non_visitate.remove(cella_corrente)
            memoria_celle.push(Cella(cella_corrente.getX(), cella_corrente.getY()))
            
            print(f"[INFO] Cella corrente aggiornata: {cella_corrente}")
    
    # Fine
    print(f"\n[INFO] Esplorazione terminata")
    print(f"[INFO] Medikit rilasciati: {medikit_rilasciati}")
    arduino.chiudi()


if __name__ == "__main__":
    main()
