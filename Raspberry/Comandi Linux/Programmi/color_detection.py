#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from picamera2 import Picamera2
import cv2
import numpy as np
import time

# Inizializza la Picamera2
picam2 = Picamera2()

# Configurazione della fotocamera
config = picam2.create_preview_configuration(
    main={"format": 'BGR888', "size": (640, 480)}
)
picam2.configure(config)
picam2.start()

# Attendi che la fotocamera sia pronta
time.sleep(2)

# ============================================
# DEFINIZIONE RANGE HSV PER I COLORI
# ============================================

# ROSSO (in HSV)
# Il rosso ha due range perché è a cavallo dello 0
lower_red1 = np.array([0, 100, 100])
upper_red1 = np.array([10, 255, 255])
lower_red2 = np.array([170, 100, 100])
upper_red2 = np.array([180, 255, 255])

# VERDE (in HSV)
lower_green = np.array([40, 100, 100])
upper_green = np.array([80, 255, 255])

# BLU (in HSV)
lower_blue = np.array([100, 100, 100])
upper_blue = np.array([130, 255, 255])

# ============================================
# FUNZIONE PER RILEVARE COLORE
# ============================================
def detect_color(frame, lower, upper, color_name, min_pixels=500):
    """
    Rileva un colore nell'immagine
    
    Args:
        frame: frame dell'immagine
        lower: limite inferiore HSV
        upper: limite superiore HSV
        color_name: nome del colore
        min_pixels: numero minimo di pixel per riconoscere il colore
    
    Returns:
        bool: True se il colore è rilevato
    """
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, lower, upper)
    pixel_count = cv2.countNonZero(mask)
    
    if pixel_count > min_pixels:
        return True, pixel_count
    return False, pixel_count

# ============================================
# LOOP PRINCIPALE
# ============================================
print("🔴 Rilevatore di colori avviato!")
print("Premi CTRL+C per terminare\n")

try:
    while True:
        # Cattura frame dalla fotocamera
        frame = picam2.capture_array()
        
        # Rileva rosso (gestisci i due range)
        red_detected1, red_pixels1 = detect_color(frame, lower_red1, upper_red1, "Rosso")
        red_detected2, red_pixels2 = detect_color(frame, lower_red2, upper_red2, "Rosso")
        red_detected = red_detected1 or red_detected2
        red_total_pixels = red_pixels1 + red_pixels2
        
        # Rileva verde
        green_detected, green_pixels = detect_color(frame, lower_green, upper_green, "Verde")
        
        # Rileva blu
        blue_detected, blue_pixels = detect_color(frame, lower_blue, upper_blue, "Blu")
        
        # ============================================
        # STAMPA RISULTATI
        # ============================================
        print("\n" + "="*50)
        print(f"Timestamp: {time.strftime('%H:%M:%S')}")
        print("="*50)
        
        if red_detected:
            print(f"🔴 ROSSO RILEVATO! ({red_total_pixels} pixel)")
        else:
            print(f"⚫ Rosso non rilevato ({red_total_pixels} pixel)")
        
        if green_detected:
            print(f"🟢 VERDE RILEVATO! ({green_pixels} pixel)")
        else:
            print(f"⚫ Verde non rilevato ({green_pixels} pixel)")
        
        if blue_detected:
            print(f"🔵 BLU RILEVATO! ({blue_pixels} pixel)")
        else:
            print(f"⚫ Blu non rilevato ({blue_pixels} pixel)")
        
        # Riepilogo
        colors_found = sum([red_detected, green_detected, blue_detected])
        if colors_found > 0:
            detected_colors = []
            if red_detected:
                detected_colors.append("ROSSO")
            if green_detected:
                detected_colors.append("VERDE")
            if blue_detected:
                detected_colors.append("BLU")
            print(f"\n✅ Colori trovati: {', '.join(detected_colors)}")
        else:
            print("\n❌ Nessun colore rilevato")
        
        # Pausa di 1 secondo tra i cicli
        time.sleep(1)

except KeyboardInterrupt:
    print("\n\n⏹️  Programma terminato dall'utente")
finally:
    # Chiudi la fotocamera
    picam2.stop()
    print("📷 Fotocamera chiusa")
