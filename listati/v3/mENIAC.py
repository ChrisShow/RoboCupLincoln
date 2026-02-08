#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
RoboCup Junior 2026 - Rescue Maze
Sistema di visione e navigazione per Raspberry Pi 4B
Comunicazione seriale sequenziale con Arduino Mega
"""

import cv2
import os
import sys
import numpy as np
import pytesseract
import serial
import time
from datetime import datetime


# =================== CONFIGURAZIONE ===================

# Configurazione seriale
SERIAL_PORT = '/dev/ttyACM0'  # Modifica se necessario (ttyUSB0, ttyACM1, ecc.)
SERIAL_BAUDRATE = 9600
SERIAL_TIMEOUT = 5  # Aumentato per attendere movimento

# Configurazione camera
CAMERA_INDEX = 0
CAMERA_WIDTH = 1920
CAMERA_HEIGHT = 1080
CAMERA_FPS = 30
WARMUP_FRAMES = 10

# Configurazione rilevazione
COLOR_THRESHOLD = 300  # Numero minimo di pixel per rilevare un colore
TEXT_AREA_MIN = 1000   # Area minima per ROI di testo
SAVE_PATH = "/home/robottino/Desktop/venv/colorLetters/images"

# Codici di risposta per rilevazione colori/lettere
CODE_NESSUNA_RILEVAZIONE = 0
CODE_ROSSO = 1
CODE_GIALLO = 2
CODE_VERDE = 3
CODE_PHI = 4      # Φ
CODE_PSI = 5      # Ψ
CODE_OMEGA = 6    # Ω

# Codici direzione da inviare ad Arduino
DIR_TERMINA = 0
DIR_DRITTO = 1
DIR_DESTRA = 2
DIR_SINISTRA = 3
DIR_DIETRO = 4

# Codici esito movimento (da Arduino)
MOVIMENTO_FALLITO = 0
MOVIMENTO_RIUSCITO = 1


# =================== CLASSI ===================

class Cella:
    """Rappresenta una tessera del labirinto con coordinate cartesiane x e y"""
    
    def __init__(self, x, y=None):
        # Supporta anche Cella(altraCella)
        if isinstance(x, Cella):
            self.x = x.getX()
            self.y = x.getY()
        else:
            self.x = x
            self.y = y
    
    def confronto(self, x, y):
        """Confronta le coordinate con quelle passate come parametro"""
        return self.x == x and self.y == y
    
    def __eq__(self, altraCella):
        """Overload operatore == per confrontare due celle"""
        if not isinstance(altraCella, Cella):
            return False
        return self.getX() == altraCella.getX() and self.getY() == altraCella.getY()
    
    def __repr__(self):
        return f"Cella({self.x}, {self.y})"
    
    # Getters e setters
    def setX(self, x):
        self.x = x
    
    def setY(self, y):
        self.y = y
    
    def getX(self):
        return self.x
    
    def getY(self):
        return self.y


class MemoriaCelle:
    """Lista di celle visitate dalla più recente alla meno recente"""
    
    def __init__(self):
        self.memoria = []
    
    def push(self, elemento):
        """Inserisce un elemento in cima alla lista"""
        if elemento is None:
            return
        self.memoria.insert(0, elemento)
    
    def storicoCella(self, elemento):
        """
        Restituisce quante mosse fa è stata visitata la cella
        -1 se non visitata, -2 se elemento None
        """
        if elemento is None:
            return -2
        for i, cella in enumerate(self.memoria):
            if cella.confronto(elemento.getX(), elemento.getY()):
                return i
        return -1
    
    def cellaGiaVisitata(self, elemento):
        """Verifica se la cella è già stata visitata"""
        if elemento is None:
            return False
        for cella in self.memoria:
            if cella.confronto(elemento.getX(), elemento.getY()):
                return True
        return False
    
    def scegliDirezione(self, dritto, destra, sinistra, lista_non_percorribili):
        """
        Riceve 3 celle e restituisce la direzione da prendere
        Esclude a priori le celle non percorribili (celle nere, muri già rilevati)
        
        Parametri:
        - dritto, destra, sinistra: celle adiacenti (None se muro)
        - lista_non_percorribili: lista delle celle da non visitare
        
        Ritorna:
        1 = DRITTO rispetto alla direzione corrente
        2 = DESTRA rispetto alla direzione corrente
        3 = SINISTRA rispetto alla direzione corrente
        4 = DIETRO rispetto alla direzione corrente
        """
        
        # Filtra le celle che sono nella lista non percorribili
        if dritto is not None and dritto in lista_non_percorribili:
            print(f"[FILTRO] Dritto escluso: cella {dritto} non percorribile")
            dritto = None
        
        if destra is not None and destra in lista_non_percorribili:
            print(f"[FILTRO] Destra escluso: cella {destra} non percorribile")
            destra = None
        
        if sinistra is not None and sinistra in lista_non_percorribili:
            print(f"[FILTRO] Sinistra escluso: cella {sinistra} non percorribile")
            sinistra = None
        
        # Ora applica la logica normale
        storico_dritto = -1
        storico_destra = -1
        storico_sinistra = -1
        
        trovata_dritto = dritto is None
        trovata_destra = destra is None
        trovata_sinistra = sinistra is None
        
        # Scansiona la memoria
        for i, cella in enumerate(self.memoria):
            if trovata_dritto and trovata_sinistra and trovata_destra:
                break
            
            if not trovata_dritto and cella.confronto(dritto.getX(), dritto.getY()):
                trovata_dritto = True
                storico_dritto = i
            
            if not trovata_destra and cella.confronto(destra.getX(), destra.getY()):
                trovata_destra = True
                storico_destra = i
            
            if not trovata_sinistra and cella.confronto(sinistra.getX(), sinistra.getY()):
                trovata_sinistra = True
                storico_sinistra = i
        
        # Restituisce la direzione (codici 1-4 per Arduino)
        if not trovata_dritto:
            return DIR_DRITTO  # 1 = Vai dritto
        if not trovata_destra:
            return DIR_DESTRA  # 2 = Vai a dx
        if not trovata_sinistra:
            return DIR_SINISTRA  # 3 = Vai a sx
        
        # Tutte le celle circostanti già visitate: vai verso la meno recente
        if storico_dritto > storico_destra and storico_dritto > storico_sinistra:
            return DIR_DRITTO  # 1
        elif storico_destra > storico_dritto and storico_destra > storico_sinistra:
            return DIR_DESTRA  # 2
        elif storico_sinistra > storico_dritto and storico_sinistra > storico_destra:
            return DIR_SINISTRA  # 3
        elif storico_dritto == -1 and storico_destra == -1 and storico_sinistra == -1:
            return DIR_DIETRO  # 4 = Vai dietro


# =================== VARIABILI GLOBALI ===================

memoriaCelle = MemoriaCelle()
listaCelleNonVisitate = []
listaCelleNonPercorribili = []
cellaCorrente = Cella(0, 0)
cellaDritto = None
cellaDestra = None
cellaSinistra = None
cellaDietro = None
direzione = 1  # 0=SOPRA, 1=DESTRA, 2=SINISTRA, 3=SOTTO
codRilevazione = 0

# Oggetto seriale globale
ser = None
cap = None


# =================== COMUNICAZIONE SERIALE ===================

def inizializzaSeriale():
    """Inizializza la comunicazione seriale con Arduino"""
    global ser
    print(f"[INFO] Inizializzazione porta seriale {SERIAL_PORT} @ {SERIAL_BAUDRATE} baud...")
    
    try:
        ser = serial.Serial(
            port=SERIAL_PORT,
            baudrate=SERIAL_BAUDRATE,
            timeout=SERIAL_TIMEOUT,
            write_timeout=SERIAL_TIMEOUT
        )
        time.sleep(2)  # Attendi reset Arduino
        
        # Svuota buffer
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        print("[OK] Porta seriale aperta con successo")
        return True
    except serial.SerialException as e:
        print(f"[ERRORE] Impossibile aprire porta seriale: {e}")
        return False


def riceviDaArduino():
    """
    Riceve un singolo valore da Arduino (bloccante)
    Ritorna: 0 o 1, oppure None in caso di errore
    """
    global ser
    
    try:
        if ser:
            # Lettura bloccante con timeout
            line = ser.readline().decode('utf-8').strip()
            
            if line:
                print(f"[SERIAL RX] '{line}'")
                
                # Converti in intero
                valore = int(line)
                
                if valore in [0, 1]:
                    return valore
                else:
                    print(f"[AVVISO] Valore non valido ricevuto: {valore}")
                    return None
            else:
                print(f"[AVVISO] Timeout ricezione da Arduino")
                return None
    
    except ValueError as e:
        print(f"[ERRORE] Formato dati non valido: {e}")
        return None
    except Exception as e:
        print(f"[ERRORE] Ricezione seriale fallita: {e}")
        return None


def inviaAdArduino(codice):
    """
    Invia un codice ad Arduino
    codice: intero 0-6 (per rilevazione) o 0-4 (per direzione)
    """
    global ser
    
    try:
        if ser:
            messaggio = f"{codice}\n"
            ser.write(messaggio.encode('utf-8'))
            ser.flush()  # Forza invio immediato
            print(f"[SERIAL TX] {codice}")
            return True
        else:
            print("[ERRORE] Porta seriale non inizializzata")
            return False
    except Exception as e:
        print(f"[ERRORE] Invio seriale fallito: {e}")
        return False


def inviaDirezioneAdArduino(direzione_codice):
    """
    Invia la direzione calcolata ad Arduino
    
    Parametri:
    - direzione_codice: intero 0-4
      0 = TERMINA (nessuna cella da visitare)
      1 = DRITTO
      2 = DESTRA
      3 = SINISTRA
      4 = DIETRO
    
    Ritorna: True se invio riuscito, False altrimenti
    """
    direzioni_nomi = {
        DIR_TERMINA: "TERMINA",
        DIR_DRITTO: "DRITTO",
        DIR_DESTRA: "DESTRA",
        DIR_SINISTRA: "SINISTRA",
        DIR_DIETRO: "DIETRO"
    }
    
    nome_direzione = direzioni_nomi.get(direzione_codice, "SCONOSCIUTA")
    
    print(f"\n[INVIO DIREZIONE] Codice {direzione_codice} = {nome_direzione}")
    
    return inviaAdArduino(direzione_codice)


def riceviConfermaMovimento():
    """
    Riceve la conferma del movimento da Arduino
    
    Ritorna:
    - 1 se movimento riuscito
    - 0 se movimento fallito (cella nera rilevata)
    - None se errore di comunicazione
    """
    print("\n[INFO] In attesa conferma movimento da Arduino...")
    
    esito = None
    tentativi = 0
    max_tentativi = 3
    
    while esito is None and tentativi < max_tentativi:
        esito = riceviDaArduino()
        if esito is None:
            tentativi += 1
            print(f"[AVVISO] Tentativo {tentativi}/{max_tentativi} per conferma movimento...")
            time.sleep(0.5)
    
    if esito == MOVIMENTO_RIUSCITO:
        print("[OK] ✓ Movimento RIUSCITO")
        return MOVIMENTO_RIUSCITO
    elif esito == MOVIMENTO_FALLITO:
        print("[AVVISO] ✗ Movimento FALLITO (cella nera o ostacolo)")
        return MOVIMENTO_FALLITO
    else:
        print("[ERRORE] Nessuna conferma ricevuta da Arduino")
        return None


# =================== INIZIALIZZAZIONE CAMERA ===================

def inizializzaCamera():
    """Inizializza la camera USB"""
    global cap
    
    print("[INFO] Inizializzazione camera USB (V4L2)...")
    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_V4L2)
    
    if not cap.isOpened():
        print("[ERRORE] Impossibile accedere alla camera USB!")
        return False
    
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAMERA_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAMERA_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS, CAMERA_FPS)
    
    # Riscaldamento camera
    print("[INFO] Riscaldamento camera...")
    for _ in range(WARMUP_FRAMES):
        ret, _ = cap.read()
    
    print("[OK] Camera inizializzata")
    return True


def rilasciaCamera():
    """Rilascia la camera"""
    global cap
    if cap:
        cap.release()
        print("[INFO] Camera rilasciata")


# =================== RILEVAZIONE COLORI ===================

def detect_colors_in_image(image):
    """Rileva rosso, giallo, verde nell'immagine"""
    hsv_image = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    
    # Range HSV per i colori
    lower_red1 = np.array([0, 100, 100])
    upper_red1 = np.array([10, 255, 255])
    lower_red2 = np.array([170, 100, 100])
    upper_red2 = np.array([179, 255, 255])
    
    lower_yellow = np.array([20, 100, 100])
    upper_yellow = np.array([40, 255, 255])
    
    lower_green = np.array([40, 100, 100])
    upper_green = np.array([80, 255, 255])
    
    # Maschere
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
    
    print(f"[DEBUG] Pixel - Rosso: {red_pixels}, Giallo: {yellow_pixels}, Verde: {green_pixels}")
    
    return {
        "rosso": has_red,
        "giallo": has_yellow,
        "verde": has_green,
        "red_pixels": red_pixels,
        "yellow_pixels": yellow_pixels,
        "green_pixels": green_pixels,
    }


# =================== RILEVAZIONE LETTERE ===================

def extract_text_regions(image):
    """Estrae le regioni dove sono presenti caratteri neri"""
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    gray_inv = cv2.bitwise_not(gray)
    
    _, thresh = cv2.threshold(gray_inv, 150, 255, cv2.THRESH_BINARY)
    
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    rois = []
    for contour in contours:
        area = cv2.contourArea(contour)
        if area > TEXT_AREA_MIN:
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


def preprocess_for_ocr_large(image):
    """Preprocessing per lettere GRANDI (Φ, Ψ, Ω)"""
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


def detect_greek_letters(image):
    """
    Rileva le lettere greche Φ, Ψ, Ω
    Ritorna un set con le lettere trovate
    """
    found_letters = set()
    
    # Configurazione Tesseract per lettere greche
    custom_config = (
        r"--psm 11 "  # Sparse text
        r"--oem 3 "
        r"-c tessedit_char_whitelist=ΦΨΩ "
        r"-c load_system_dawg=0 "
        r"-c load_freq_dawg=0"
    )
    
    rois = extract_text_regions(image)
    
    if not rois:
        print("[AVVISO] Nessuna regione di testo trovata")
        rois = [image]
    
    for idx, roi in enumerate(rois):
        angles = [0, 90, 180, 270]
        
        for angle in angles:
            proc = preprocess_for_ocr_large(roi)
            
            if angle == 0:
                rotated = proc
            elif angle == 90:
                rotated = cv2.rotate(proc, cv2.ROTATE_90_CLOCKWISE)
            elif angle == 180:
                rotated = cv2.rotate(proc, cv2.ROTATE_180)
            elif angle == 270:
                rotated = cv2.rotate(proc, cv2.ROTATE_90_COUNTERCLOCKWISE)
            
            try:
                text = pytesseract.image_to_string(rotated, lang='ell', config=custom_config)
                text = text.strip()
                
                if text:
                    print(f"[DEBUG] ROI {idx}, Rot {angle}°: '{text}'")
                
                for ch in text:
                    if ch in ["Φ", "Ψ", "Ω"]:
                        found_letters.add(ch)
                        print(f"[OK] Trovata lettera: {ch}")
            
            except Exception as e:
                print(f"[AVVISO] Errore OCR: {e}")
            
            if found_letters:
                break
        
        if found_letters:
            break
    
    return found_letters


# =================== ACQUISIZIONE E ANALISI ===================

def scattaFotoEAnalizza():
    """
    Scatta una foto, la analizza e restituisce il codice da inviare ad Arduino
    Codici: 0=niente, 1=rosso, 2=giallo, 3=verde, 4=Φ, 5=Ψ, 6=Ω
    """
    global cap, codRilevazione
    
    # Verifica directory
    if not os.path.exists(SAVE_PATH):
        print(f"[INFO] Creazione directory: {SAVE_PATH}")
        os.makedirs(SAVE_PATH, exist_ok=True)
    
    # Acquisizione frame
    print("[INFO] Acquisizione foto...")
    ret, frame = cap.read()
    
    if not ret:
        print("[ERRORE] Impossibile acquisire frame!")
        return CODE_NESSUNA_RILEVAZIONE
    
    # Salvataggio
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"img{codRilevazione:04d}_{timestamp}.jpg"
    filepath = os.path.join(SAVE_PATH, filename)
    
    cv2.imwrite(filepath, frame)
    codRilevazione += 1
    
    h, w, c = frame.shape
    print(f"[OK] Foto salvata: {filename} ({w}x{h})")
    
    # =================== ANALISI ===================
    
    print("\n===== ANALISI IMMAGINE =====")
    
    # 1. Rilevazione colori (priorità)
    color_res = detect_colors_in_image(frame)
    
    if color_res["rosso"]:
        print("[DECISION] Codice 1: ROSSO rilevato")
        return CODE_ROSSO
    elif color_res["giallo"]:
        print("[DECISION] Codice 2: GIALLO rilevato")
        return CODE_GIALLO
    elif color_res["verde"]:
        print("[DECISION] Codice 3: VERDE rilevato")
        return CODE_VERDE
    
    # 2. Rilevazione lettere greche
    print("[INFO] Nessun colore rilevato. Analizzando lettere greche...")
    letters_found = detect_greek_letters(frame)
    
    if "Φ" in letters_found:
        print("[DECISION] Codice 4: Lettera Φ (Phi) rilevata")
        return CODE_PHI
    elif "Ψ" in letters_found:
        print("[DECISION] Codice 5: Lettera Ψ (Psi) rilevata")
        return CODE_PSI
    elif "Ω" in letters_found:
        print("[DECISION] Codice 6: Lettera Ω (Omega) rilevata")
        return CODE_OMEGA
    
    # 3. Nessuna rilevazione
    print("[DECISION] Codice 0: Nessuna condizione soddisfatta")
    return CODE_NESSUNA_RILEVAZIONE


# =================== NAVIGAZIONE ===================

def calcolaCellaDritto():
    """Calcola le coordinate della cella di fronte in base alla direzione corrente"""
    x = cellaCorrente.getX()
    y = cellaCorrente.getY()
    
    if direzione == 0:  # SOPRA
        return Cella(x, y + 1)
    elif direzione == 1:  # DESTRA
        return Cella(x + 1, y)
    elif direzione == 2:  # SINISTRA
        return Cella(x - 1, y)
    elif direzione == 3:  # SOTTO
        return Cella(x, y - 1)


def calcolaCellaDestra():
    """Calcola le coordinate della cella a destra in base alla direzione corrente"""
    x = cellaCorrente.getX()
    y = cellaCorrente.getY()
    
    if direzione == 0:  # SOPRA
        return Cella(x + 1, y)
    elif direzione == 1:  # DESTRA
        return Cella(x, y - 1)
    elif direzione == 2:  # SINISTRA
        return Cella(x, y + 1)
    elif direzione == 3:  # SOTTO
        return Cella(x - 1, y)


def calcolaCellaSinistra():
    """Calcola le coordinate della cella a sinistra in base alla direzione corrente"""
    x = cellaCorrente.getX()
    y = cellaCorrente.getY()
    
    if direzione == 0:  # SOPRA
        return Cella(x - 1, y)
    elif direzione == 1:  # DESTRA
        return Cella(x, y + 1)
    elif direzione == 2:  # SINISTRA
        return Cella(x, y - 1)
    elif direzione == 3:  # SOTTO
        return Cella(x + 1, y)


def calcolaCellaDietro():
    """Calcola le coordinate della cella dietro in base alla direzione corrente"""
    x = cellaCorrente.getX()
    y = cellaCorrente.getY()
    
    if direzione == 0:  # SOPRA
        return Cella(x, y - 1)
    elif direzione == 1:  # DESTRA
        return Cella(x - 1, y)
    elif direzione == 2:  # SINISTRA
        return Cella(x + 1, y)
    elif direzione == 3:  # SOTTO
        return Cella(x, y + 1)


def getCellaByDirezione(direzione_codice):
    """
    Restituisce la cella corrispondente al codice direzione
    
    Parametri:
    - direzione_codice: 1=DRITTO, 2=DESTRA, 3=SINISTRA, 4=DIETRO
    
    Ritorna: oggetto Cella o None
    """
    if direzione_codice == DIR_DRITTO:
        return cellaDritto
    elif direzione_codice == DIR_DESTRA:
        return cellaDestra
    elif direzione_codice == DIR_SINISTRA:
        return cellaSinistra
    elif direzione_codice == DIR_DIETRO:
        return calcolaCellaDietro()
    else:
        return None


def scansionaDirezione(nomeDirezione):
    """
    Gestisce la scansione di una singola direzione (fronte, destra, sinistra)
    
    Flusso:
    1. Riceve 0 o 1 da Arduino
    2. Se 0: calcola cella e aggiunge a lista non visitate
    3. Se 1: scatta foto, analizza, invia codice ad Arduino
    
    Parametri:
    - nomeDirezione: "DRITTO", "DESTRA", "SINISTRA"
    
    Ritorna:
    - Cella calcolata se no muro (0 ricevuto)
    - None se c'è muro (1 ricevuto)
    """
    global cellaDritto, cellaDestra, cellaSinistra
    
    print(f"\n[INFO] Scansione direzione: {nomeDirezione}")
    
    # 1. Ricevi da Arduino (bloccante)
    rilevamento = None
    tentativi = 0
    max_tentativi = 5
    
    while rilevamento is None and tentativi < max_tentativi:
        rilevamento = riceviDaArduino()
        if rilevamento is None:
            tentativi += 1
            print(f"[AVVISO] Tentativo {tentativi}/{max_tentativi} per {nomeDirezione}...")
            time.sleep(0.2)
    
    if rilevamento is None:
        print(f"[ERRORE] Impossibile ricevere dati per {nomeDirezione}. Assumo muro presente.")
        rilevamento = 1
    
    # 2. Calcola cella in base alla direzione
    if nomeDirezione == "DRITTO":
        cella_calcolata = calcolaCellaDritto()
    elif nomeDirezione == "DESTRA":
        cella_calcolata = calcolaCellaDestra()
    elif nomeDirezione == "SINISTRA":
        cella_calcolata = calcolaCellaSinistra()
    else:
        print(f"[ERRORE] Direzione non valida: {nomeDirezione}")
        return None
    
    # 3. Gestisci in base al valore ricevuto
    if rilevamento == 0:  # Nessun muro
        print(f"[INFO] {nomeDirezione}: Nessun muro rilevato -> Cella {cella_calcolata}")
        
        # Verifica se già non percorribile
        if cella_calcolata in listaCelleNonPercorribili:
            print(f"[INFO] Cella {cella_calcolata} già marcata come non percorribile")
            return None
        
        # Verifica se già visitata
        if memoriaCelle.cellaGiaVisitata(cella_calcolata):
            print(f"[INFO] Cella {cella_calcolata} già visitata in passato")
            return cella_calcolata
        
        # Aggiungi a lista celle non visitate se non presente
        if cella_calcolata not in listaCelleNonVisitate:
            listaCelleNonVisitate.insert(0, Cella(cella_calcolata))
            print(f"[OK] Cella {cella_calcolata} aggiunta a liste non visitate")
        
        return cella_calcolata
    
    elif rilevamento == 1:  # Muro presente
        print(f"[INFO] {nomeDirezione}: Muro rilevato -> Avvio rilevazione camera")
        
        # Scatta foto e analizza
        codice = scattaFotoEAnalizza()
        
        # Invia codice ad Arduino
        inviaAdArduino(codice)
        
        # Marca cella come non percorribile
        if cella_calcolata not in listaCelleNonPercorribili:
            listaCelleNonPercorribili.append(Cella(cella_calcolata))
            print(f"[OK] Cella {cella_calcolata} marcata come non percorribile")
        
        # Rimuovi da liste non visitate se presente
        if cella_calcolata in listaCelleNonVisitate:
            listaCelleNonVisitate.remove(cella_calcolata)
        
        return None


def guardaIntorno():
    """
    Scansiona le tre direzioni (dritto, destra, sinistra) in sequenza
    Aggiorna le variabili globali cellaDritto, cellaDestra, cellaSinistra
    """
    global cellaDritto, cellaDestra, cellaSinistra
    
    print("\n" + "="*60)
    print("[INFO] GUARDANDO INTORNO - Scansione sequenziale")
    print("="*60)
    
    # Scansione FRONTE
    cellaDritto = scansionaDirezione("DRITTO")
    time.sleep(0.1)  # Piccola pausa tra scansioni
    
    # Scansione DESTRA
    cellaDestra = scansionaDirezione("DESTRA")
    time.sleep(0.1)
    
    # Scansione SINISTRA
    cellaSinistra = scansionaDirezione("SINISTRA")
    
    print("\n[RIEPILOGO SCANSIONE]")
    print(f"  Dritto:   {cellaDritto if cellaDritto else 'MURO'}")
    print(f"  Destra:   {cellaDestra if cellaDestra else 'MURO'}")
    print(f"  Sinistra: {cellaSinistra if cellaSinistra else 'MURO'}")
    print(f"  Celle non visitate: {len(listaCelleNonVisitate)}")
    print(f"  Celle non percorribili: {len(listaCelleNonPercorribili)}")
    print("="*60 + "\n")


def eseguiMovimento(direzione_codice):
    """
    Aggiorna la posizione del robot dopo il movimento CONFERMATO
    
    Parametri:
    - direzione_codice: 1=DRITTO, 2=DESTRA, 3=SINISTRA, 4=DIETRO
    
    Ritorna: True se aggiornamento riuscito, False altrimenti
    """
    global cellaCorrente, direzione
    
    # Determina la cella di destinazione
    cella_destinazione = getCellaByDirezione(direzione_codice)
    
    # Verifica validità cella destinazione
    if cella_destinazione is None:
        print("[ERRORE] Cella di destinazione non valida!")
        return False
    
    # Rimuovi da lista celle non visitate
    if cella_destinazione in listaCelleNonVisitate:
        listaCelleNonVisitate.remove(cella_destinazione)
    
    # Aggiungi alla memoria
    memoriaCelle.push(Cella(cella_destinazione))
    
    # Aggiorna posizione corrente
    cellaCorrente = Cella(cella_destinazione)
    
    # Aggiorna direzione globale in base alla rotazione
    if direzione_codice == DIR_DRITTO:  # 1 = Dritto
        # Direzione rimane invariata
        pass
    
    elif direzione_codice == DIR_DESTRA:  # 2 = Destra
        # Aggiorna direzione (ruota orario)
        direzione = (direzione + 1) % 4
        print(f"[INFO] Rotazione destra: nuova direzione = {direzione}")
    
    elif direzione_codice == DIR_SINISTRA:  # 3 = Sinistra
        # Aggiorna direzione (ruota antiorario)
        direzione = (direzione - 1) % 4
        print(f"[INFO] Rotazione sinistra: nuova direzione = {direzione}")
    
    elif direzione_codice == DIR_DIETRO:  # 4 = Dietro
        # Aggiorna direzione (180°)
        direzione = (direzione + 2) % 4
        print(f"[INFO] Rotazione 180°: nuova direzione = {direzione}")
    
    print(f"[OK] ✓ Movimento completato. Nuova posizione: {cellaCorrente}")
    
    return True


def marcaCellaComeBloccata(direzione_codice):
    """
    Marca la cella di destinazione come non percorribile (cella nera)
    
    Parametri:
    - direzione_codice: 1=DRITTO, 2=DESTRA, 3=SINISTRA, 4=DIETRO
    """
    cella_bloccata = getCellaByDirezione(direzione_codice)
    
    if cella_bloccata is None:
        print("[AVVISO] Impossibile identificare cella bloccata")
        return
    
    # Aggiungi a lista non percorribili se non già presente
    if cella_bloccata not in listaCelleNonPercorribili:
        listaCelleNonPercorribili.append(Cella(cella_bloccata))
        print(f"[OK] Cella {cella_bloccata} marcata come NERA/NON PERCORRIBILE")
    
    # Rimuovi da lista celle non visitate se presente
    if cella_bloccata in listaCelleNonVisitate:
        listaCelleNonVisitate.remove(cella_bloccata)
        print(f"[OK] Cella {cella_bloccata} rimossa da liste non visitate")


# =================== MAIN ===================

def main():
    """Funzione principale del programma"""
    global cellaCorrente, cellaDritto, cellaDestra, cellaSinistra, cellaDietro
    
    print("="*60)
    print(" RoboCup Junior 2026 - Rescue Maze")
    print(" Sistema di visione e navigazione")
    print(" Protocollo sequenziale Arduino-Raspberry")
    print("="*60)
    
    # Inizializzazione
    if not inizializzaSeriale():
        print("[ERRORE FATALE] Impossibile inizializzare seriale")
        sys.exit(1)
    
    if not inizializzaCamera():
        print("[ERRORE FATALE] Impossibile inizializzare camera")
        sys.exit(1)
    
    print("\n[OK] Inizializzazione completata. Avvio navigazione...\n")
    
    # Aggiungi cella di partenza alla memoria
    memoriaCelle.push(Cella(cellaCorrente))
    
    try:
        # Prima scansione
        guardaIntorno()
        
        # Loop principale di navigazione
        while listaCelleNonVisitate:
            print(f"\n{'='*60}")
            print(f"[STATO] Posizione: {cellaCorrente}, Direzione: {direzione}")
            print(f"[STATO] Celle da visitare: {len(listaCelleNonVisitate)}")
            print(f"[STATO] Celle non percorribili: {len(listaCelleNonPercorribili)}")
            print(f"{'='*60}")
            
            # Scegli prossima direzione (ritorna codice 1-4)
            # Passa anche la lista celle non percorribili per il filtro
            direzione_scelta = memoriaCelle.scegliDirezione(
                cellaDritto, 
                cellaDestra, 
                cellaSinistra,
                listaCelleNonPercorribili
            )
            
            direzioni_nomi = {
                DIR_DRITTO: "DRITTO",
                DIR_DESTRA: "DESTRA",
                DIR_SINISTRA: "SINISTRA",
                DIR_DIETRO: "DIETRO"
            }
            
            print(f"[DECISIONE] Prossima direzione: {direzioni_nomi[direzione_scelta]} (codice {direzione_scelta})")
            
            # Invia direzione ad Arduino
            if not inviaDirezioneAdArduino(direzione_scelta):
                print("[ERRORE] Impossibile inviare direzione ad Arduino!")
                break
            
            # Attendi conferma movimento da Arduino
            esito_movimento = riceviConfermaMovimento()
            
            if esito_movimento == MOVIMENTO_RIUSCITO:
                # Movimento riuscito: aggiorna posizione
                print("\n[STATO] Aggiornamento posizione...")
                
                if not eseguiMovimento(direzione_scelta):
                    print("[ERRORE] Impossibile aggiornare posizione!")
                    break
                
                # Nuova scansione dopo movimento riuscito
                guardaIntorno()
            
            elif esito_movimento == MOVIMENTO_FALLITO:
                # Movimento fallito: cella nera rilevata
                print("\n[AVVISO] ✗ MOVIMENTO FALLITO - Cella nera o ostacolo rilevato")
                
                # Marca la cella come non percorribile
                marcaCellaComeBloccata(direzione_scelta)
                
                # Ricalcola nuova direzione escludendo celle nere
                print("\n[INFO] Ricalcolo nuova direzione...")
                
                nuova_direzione = memoriaCelle.scegliDirezione(
                    cellaDritto, 
                    cellaDestra, 
                    cellaSinistra,
                    listaCelleNonPercorribili
                )
                
                print(f"[DECISIONE] Nuova direzione: {direzioni_nomi[nuova_direzione]} (codice {nuova_direzione})")
                
                # Invia nuova direzione ad Arduino
                if not inviaDirezioneAdArduino(nuova_direzione):
                    print("[ERRORE] Impossibile inviare nuova direzione ad Arduino!")
                    break
                
                # Ricomincia ciclo (attendi nuova conferma)
                continue
            
            else:
                # Errore di comunicazione
                print("[ERRORE CRITICO] Comunicazione con Arduino interrotta!")
                break
            
            time.sleep(0.3)  # Piccola pausa tra iterazioni
        
        # Fine esplorazione: invia codice terminazione
        print("\n" + "="*60)
        print("[COMPLETATO] Tutte le celle visitabili sono state esplorate!")
        print("[INFO] Invio codice terminazione (0) ad Arduino...")
        print("="*60)
        
        inviaDirezioneAdArduino(DIR_TERMINA)  # Invia 0
        
    except KeyboardInterrupt:
        print("\n[INFO] Interruzione da utente (Ctrl+C)")
        print("[INFO] Invio codice terminazione (0) ad Arduino...")
        inviaDirezioneAdArduino(DIR_TERMINA)
    
    except Exception as e:
        print(f"\n[ERRORE] Eccezione non gestita: {e}")
        import traceback
        traceback.print_exc()
        print("[INFO] Invio codice terminazione (0) ad Arduino...")
        inviaDirezioneAdArduino(DIR_TERMINA)
    
    finally:
        # Cleanup
        print("\n[INFO] Chiusura risorse...")
        rilasciaCamera()
        if ser:
            ser.close()
            print("[INFO] Porta seriale chiusa")
        print("[INFO] Programma terminato")


if __name__ == "__main__":
    main()
