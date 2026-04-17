"""
File:  flag_test.py
Scopo: Test rilevamento bandiere con camera destra

  Arduino preme BTN_START → invia <SCAN_FLAG>
  Pi scatta foto con cam destra → classifica → risponde <FLAG:NOME>
  Risultato mostrato su terminale Pi e LCD Arduino
"""

import serial
import time
import cv2
import numpy as np

SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE   = 115200

CAMERA_ID_DX       = 2      # indice camera destra
CAMERA_SETTLE_TIME = 0.4    # secondi stabilizzazione AWB
FLUSH_FRAMES       = 8      # frame da scartare prima dello scatto

# ---------------------------------------------------------------
# Soglie rilevamento colori bandiere (pixel su immagine 120x120)
# Aumenta se troppi falsi positivi, diminuisci se non rileva.
# ---------------------------------------------------------------
C_VERDE_H_MIN  =  38;  C_VERDE_H_MAX  =  85
C_VERDE_S_MIN  =  70;  C_VERDE_V_MIN  =  50
C_VERDE_SOGLIA = 200

C_GIALLO_H_MIN  =  18;  C_GIALLO_H_MAX  =  38
C_GIALLO_S_MIN  = 100;  C_GIALLO_V_MIN  = 100
C_GIALLO_SOGLIA = 150

C_ROSSO_H_MAX1 =  12
C_ROSSO_H_MIN2 = 168
C_ROSSO_S_MIN  =  80;  C_ROSSO_V_MIN  =  60

C_BLU_H_MIN  =  95;  C_BLU_H_MAX  = 140
C_BLU_S_MIN  =  70;  C_BLU_V_MIN  =  50
C_BLU_SOGLIA = 150

C_NERO_V_MAX   =  55
C_NERO_SOGLIA  = 200

C_BIANCO_S_MAX  =  50;  C_BIANCO_V_MIN  = 160
C_BIANCO_SOGLIA = 150

# ---------------------------------------------------------------

def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}")


def classify_flag(frame):
    if frame is None or frame.size == 0:
        return None

    frame = cv2.resize(frame, (120, 120))
    hsv   = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    h = hsv[:, :, 0].astype(int)
    s = hsv[:, :, 1].astype(int)
    v = hsv[:, :, 2].astype(int)

    n_verde  = int(((h >= C_VERDE_H_MIN)  & (h <= C_VERDE_H_MAX)  & (s >= C_VERDE_S_MIN)  & (v >= C_VERDE_V_MIN)).sum())
    n_giallo = int(((h >= C_GIALLO_H_MIN) & (h <= C_GIALLO_H_MAX) & (s >= C_GIALLO_S_MIN) & (v >= C_GIALLO_V_MIN)).sum())
    n_blu    = int(((h >= C_BLU_H_MIN)    & (h <= C_BLU_H_MAX)    & (s >= C_BLU_S_MIN)    & (v >= C_BLU_V_MIN)).sum())
    n_nero   = int((v <= C_NERO_V_MAX).sum())
    n_bianco = int(((s <= C_BIANCO_S_MAX) & (v >= C_BIANCO_V_MIN)).sum())

    ha_verde  = n_verde  >= C_VERDE_SOGLIA
    ha_giallo = n_giallo >= C_GIALLO_SOGLIA
    ha_blu    = n_blu    >= C_BLU_SOGLIA
    ha_nero   = n_nero   >= C_NERO_SOGLIA
    ha_bianco = n_bianco >= C_BIANCO_SOGLIA

    log(f"  px  VERDE:{n_verde:4d}  GIALLO:{n_giallo:4d}  BLU:{n_blu:4d}  NERO:{n_nero:4d}  BIANCO:{n_bianco:4d}")
    log(f"  ok  V:{int(ha_verde)} G:{int(ha_giallo)} B:{int(ha_blu)} K:{int(ha_nero)} W:{int(ha_bianco)}")

    if ha_verde:                    return 'ITALIA'
    if ha_giallo and ha_blu:        return 'ROMANIA'
    if ha_giallo and ha_nero:       return 'GERMANIA'
    if ha_blu    and ha_bianco:     return 'PAESI_BASSI'
    if ha_bianco:                   return 'AUSTRIA'
    return None


def main():
    # Seriale
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        log(f"Seriale aperta: {SERIAL_PORT}")
    except Exception as e:
        log(f"ERRORE seriale: {e}")
        return

    # Camera destra
    cap = cv2.VideoCapture(CAMERA_ID_DX)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    if not cap.isOpened():
        log("ERRORE: camera destra non disponibile")
        return

    time.sleep(2)
    # Svuota buffer seriale
    while ser.in_waiting:
        ser.readline()

    log("Pronto. In attesa di <SCAN_FLAG> da Arduino...")

    while True:
        if ser.in_waiting == 0:
            time.sleep(0.01)
            continue

        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            continue

        if line.startswith('[D]'):
            log(f"[ARD] {line[4:]}")
            continue

        log(f"RX: {line}")

        if line == '<SCAN_FLAG>':
            log("Scansione bandiera con camera destra...")

            # Flush buffer camera + stabilizzazione
            for _ in range(FLUSH_FRAMES):
                cap.read()
            time.sleep(CAMERA_SETTLE_TIME)

            # Scatta 3 frame, usa la classificazione più comune
            risultati = []
            for i in range(3):
                cap.read()   # scarta frame stantio
                ret, frame = cap.read()
                if not ret or frame is None:
                    continue
                nome = classify_flag(frame)
                risultati.append(nome)
                log(f"  Frame {i+1}: {nome}")

            # Voto a maggioranza
            from collections import Counter
            validi = [r for r in risultati if r is not None]
            if validi:
                bandiera = Counter(validi).most_common(1)[0][0]
            else:
                bandiera = 'UNKNOWN'

            log(f">>> BANDIERA RILEVATA: {bandiera} <<<")
            risposta = f"<FLAG:{bandiera}>"
            ser.write(f"{risposta}\n".encode())
            log(f"TX: {risposta}")


if __name__ == '__main__':
    main()
