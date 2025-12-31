#!/usr/bin/env python3
"""
Versione ottimizzata per lettere GRANDI (4cm x 4cm su immagine 1920x1080)
Estrae le regioni di interesse e normalizza prima di OCR
"""

import cv2
import os
import sys
import numpy as np
import pytesseract
from datetime import datetime

COLOR_THRESHOLD = 300

def detect_colors_in_image(image):
    """Rileva rosso, giallo, verde."""
    hsv_image = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)

    lower_red1 = np.array([0, 100, 100])
    upper_red1 = np.array([10, 255, 255])
    lower_red2 = np.array([170, 100, 100])
    upper_red2 = np.array([179, 255, 255])

    lower_yellow = np.array([20, 100, 100])
    upper_yellow = np.array([40, 255, 255])

    lower_green = np.array([40, 100, 100])
    upper_green = np.array([80, 255, 255])

    mask_red1 = cv2.inRange(hsv_image, lower_red1, upper_red1)
    mask_red2 = cv2.inRange(hsv_image, lower_red2, upper_red2)
    mask_red = cv2.bitwise_or(mask_red1, mask_red2)

    mask_yellow = cv2.inRange(hsv_image, lower_yellow, upper_yellow)
    mask_green = cv2.inRange(hsv_image, lower_green, upper_green)

    mask_red = cv2.GaussianBlur(mask_red, (5, 5), 0)
    mask_yellow = cv2.GaussianBlur(mask_yellow, (5, 5), 0)
    mask_green = cv2.GaussianBlur(mask_green, (5, 5), 0)

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

def extract_text_regions(image):
    """
    Estrae le regioni dove sono i caratteri neri usando edge detection.
    Restituisce una lista di ROI (region of interest) ritagliati.
    """
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    
    # Inverti (sfondo bianco -> nero, testo nero -> bianco)
    gray_inv = cv2.bitwise_not(gray)
    
    # Thresholding per isolare il testo nero
    _, thresh = cv2.threshold(gray_inv, 150, 255, cv2.THRESH_BINARY)
    
    # Trova contorni
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    rois = []
    for contour in contours:
        area = cv2.contourArea(contour)
        # Filtra per dimensione ragionevole (evita rumore piccolo)
        if area > 1000:  # Minimo 1000 pixel di area
            x, y, w, h = cv2.boundingRect(contour)
            # Padding attorno al carattere
            padding = 20
            x_start = max(0, x - padding)
            y_start = max(0, y - padding)
            x_end = min(image.shape[1], x + w + padding)
            y_end = min(image.shape[0], y + h + padding)
            
            roi = image[y_start:y_end, x_start:x_end]
            if roi.size > 0:
                rois.append(roi)
                print(f"[DEBUG] Estratto ROI: {roi.shape}")
    
    return rois

def preprocess_for_ocr_large(image):
    """
    Preprocessing per lettere GRANDI.
    Ridimensiona a dimensione ottimale per Tesseract (~300-400 pixel di altezza).
    """
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    
    # Normalizza l'immagine
    clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
    gray = clahe.apply(gray)
    
    # Ridimensiona se l'immagine è troppo grande
    h, w = gray.shape
    if h > 400:
        scale = 400 / h
        gray = cv2.resize(gray, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
    
    # Thresholding adattivo
    thresh = cv2.adaptiveThreshold(
        gray, 255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY,
        blockSize=21,
        C=5
    )
    
    # Dilatazione leggera per connettere i pixel
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    thresh = cv2.morphologyEx(thresh, cv2.MORPH_CLOSE, kernel, iterations=1)
    
    # Erosione leggera per pulire
    thresh = cv2.morphologyEx(thresh, cv2.MORPH_OPEN, kernel, iterations=1)
    
    # Blur finale
    thresh = cv2.GaussianBlur(thresh, (3, 3), 0)
    
    return thresh

def detect_letters_HSU_optimized(image):
    """
    Ottimizzato per lettere grandi.
    Estrae le regioni di testo e le elabora singolarmente.
    """
    found_letters = set()
    
    # Config Tesseract aggressivo
    custom_config = (
        r"--psm 11 "  # PSM 11: sparse text
        r"--oem 3 "
        r"-c tessedit_char_whitelist=HSU "
        r"-c load_system_dawg=0 "
        r"-c load_freq_dawg=0 "
        r"-c classify_bln_numeric_mode=0"
    )
    
    # Estrai le regioni di interesse
    rois = extract_text_regions(image)
    
    if not rois:
        print("[AVVISO] Nessuna regione di testo trovata con edge detection")
        # Fallback: prova su tutta l'immagine
        rois = [image]
    
    # Processa ogni ROI
    for idx, roi in enumerate(rois):
        angles = [0, 90, 180, 270]
        
        for angle in angles:
            # Preprocess ROI
            proc = preprocess_for_ocr_large(roi)
            
            # Ruota
            if angle == 0:
                rotated = proc
            elif angle == 90:
                rotated = cv2.rotate(proc, cv2.ROTATE_90_CLOCKWISE)
            elif angle == 180:
                rotated = cv2.rotate(proc, cv2.ROTATE_180)
            elif angle == 270:
                rotated = cv2.rotate(proc, cv2.ROTATE_90_COUNTERCLOCKWISE)
            
            # OCR
            try:
                text = pytesseract.image_to_string(rotated, config=custom_config)
                text = text.upper().strip()
                
                if text:
                    print(f"[DEBUG] ROI {idx}, Rotazione {angle}°: '{text}'")
                
                # Filtra
                for ch in text:
                    if ch in ["H", "S", "U"]:
                        found_letters.add(ch)
                        print(f"[OK] Trovata lettera: {ch}")
            
            except Exception as e:
                print(f"[AVVISO] Errore OCR ROI {idx}, rot {angle}°: {e}")
            
            if found_letters == {"H", "S", "U"}:
                break
        
        if found_letters == {"H", "S", "U"}:
            break
    
    print(f"[DEBUG] Lettere trovate totali: {found_letters if found_letters else 'nessuna'}")
    return found_letters

def main():
    SAVE_PATH = "/home/robottino/Desktop/venv/colorLetters/images"

    if not os.path.exists(SAVE_PATH):
        print(f"[INFO] Directory '{SAVE_PATH}' non trovata. Creazione in corso...")
        try:
            os.makedirs(SAVE_PATH, exist_ok=True)
            print(f"[OK] Directory creata: {SAVE_PATH}")
        except Exception as e:
            print(f"[ERRORE] Impossibile creare la directory: {e}")
            sys.exit(1)

    if not os.access(SAVE_PATH, os.W_OK):
        print(f"[ERRORE] Nessun permesso di scrittura sulla directory: {SAVE_PATH}")
        sys.exit(1)

    print(f"[INFO] Directory di salvataggio: {SAVE_PATH}")

    # Inizializza camera
    print("[INFO] Inizializzazione della camera (V4L2)...")
    cap = cv2.VideoCapture(0, cv2.CAP_V4L2)
    if not cap.isOpened():
        print("[ERRORE] Impossibile accedere alla camera USB!")
        sys.exit(1)

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
    cap.set(cv2.CAP_PROP_FPS, 30)

    print("[INFO] Riscaldamento della camera...")
    for i in range(10):
        ret, _ = cap.read()

    print("[INFO] Acquisizione della foto...")
    ret, frame = cap.read()
    if not ret:
        print("[ERRORE] Impossibile acquisire il frame dalla camera!")
        cap.release()
        sys.exit(1)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"foto_{timestamp}.jpg"
    filepath = os.path.join(SAVE_PATH, filename)

    if not cv2.imwrite(filepath, frame):
        print(f"[ERRORE] Impossibile salvare l'immagine in: {filepath}")
        cap.release()
        sys.exit(1)

    h, w, c = frame.shape
    print(f"[OK] Foto salvata: {filepath}")
    print(f"[INFO] Risoluzione: {w}x{h}, canali: {c}")

    cap.release()

    # =================== ANALISI ===================

    print("\n===== ANALISI IMMAGINE =====")

    # Colori
    color_res = detect_colors_in_image(frame)

    code = None

    if color_res["rosso"]:
        code = 1
        print("[DECISION] Codice 1: ROSSO rilevato")
    elif color_res["giallo"]:
        code = 2
        print("[DECISION] Codice 2: GIALLO rilevato")
    elif color_res["verde"]:
        code = 3
        print("[DECISION] Codice 3: VERDE rilevato")

    # Lettere
    letters_found = set()
    if code is None:
        print("[INFO] Nessun colore rilevato. Analizzando lettere...")
        letters_found = detect_letters_HSU_optimized(frame)

        if "H" in letters_found:
            code = 4
            print("[DECISION] Codice 4: Lettera H rilevata")
        elif "S" in letters_found:
            code = 5
            print("[DECISION] Codice 5: Lettera S rilevata")
        elif "U" in letters_found:
            code = 6
            print("[DECISION] Codice 6: Lettera U rilevata")

    if code is None:
        code = 0
        print("[DECISION] Codice 0: Nessuna condizione soddisfatta")

    # Output
    print("\n" + "="*50)
    print("--- RISULTATI FINALI ---")
    print("="*50)
    print(f"Rosso presente (>= {COLOR_THRESHOLD} px): {'si' if color_res['rosso'] else 'no'}")
    print(f"Giallo presente (>= {COLOR_THRESHOLD} px): {'si' if color_res['giallo'] else 'no'}")
    print(f"Verde presente (>= {COLOR_THRESHOLD} px): {'si' if color_res['verde'] else 'no'}")
    print(f"Lettere trovate (H/S/U): {', '.join(sorted(letters_found)) if letters_found else 'nessuna'}")
    print(f"\n🎯 CODICE RESTITUITO: {code}")
    print("="*50)

    return code

if __name__ == "__main__":
    main()
