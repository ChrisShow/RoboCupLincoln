"""
Progetto: RoboCup Junior Rescue Maze 2026 - mENIAC Team
File:     tech_challenge.py
Scopo:    Technical Challenge — Zona A + Zona B

  Zona A: tile BLU → GO:3 (sinistra)  |  tile ROSSA → GO:1 (destra)
          tile NORM → GO:0 (dritto)    |  tile GRIGIO → ingresso Zona B

  Zona B:
    Lap 1: right-hand wall follower, scansione bandiere su ogni muro
    Checkpoint: lampeggio N volte (N = occorrenze bandiera target)
    Lap 2: left-hand wall follower, drop kit per frequenza (3/2/1)

  Colori tile da Arduino <DATA:c,FRL>:
    c=0: NORMALE    c=1: GRIGIO(checkpoint)
    c=2: ROSSA(dx)  c=3: BLU(sx)

  Pool bandiere: ITALIA, ROMANIA, GERMANIA, AUSTRIA, PAESI_BASSI
"""

import serial
import time
import cv2
import numpy as np
import os

# --- CONFIGURAZIONE ---
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE   = 115200
CAMERA_SETTLE_TIME = 0.4

# --- COSTANTI COLORI TILE ---
TILE_NORM  = 0
TILE_GREY  = 1   # checkpoint
TILE_ROSSA = 2   # gira destra
TILE_BLU   = 3   # gira sinistra

TILE_NOMI = {0: 'NORM', 1: 'GRIGIO(CP)', 2: 'ROSSA(dx)', 3: 'BLU(sx)'}

# --- BANDIERE ---
BANDIERE = ['ITALIA', 'ROMANIA', 'GERMANIA', 'AUSTRIA', 'PAESI_BASSI']

# ---------------------------------------------------------------
# Soglie rilevamento colori bandiere (pixel su immagine 120x120)
# Aumenta se troppi falsi positivi, diminuisci se non rileva.
# ---------------------------------------------------------------
# Range HSV: H in [0,179], S in [0,255], V in [0,255]

# VERDE — presente solo in Italia → identificazione immediata
C_VERDE_H_MIN  =  38;  C_VERDE_H_MAX  =  85
C_VERDE_S_MIN  =  70;  C_VERDE_V_MIN  =  50
C_VERDE_SOGLIA = 200   # pixel minimi per confermare Italia

# GIALLO/ORO — presente solo in Romania e Germania
C_GIALLO_H_MIN  =  18;  C_GIALLO_H_MAX  =  38
C_GIALLO_S_MIN  = 100;  C_GIALLO_V_MIN  = 100
C_GIALLO_SOGLIA = 150   # pixel minimi per confermare giallo presente

# ROSSO (doppio range HSV)
C_ROSSO_H_MAX1 =  12   # range basso  [0, H_MAX1]
C_ROSSO_H_MIN2 = 168   # range alto   [H_MIN2, 179]
C_ROSSO_S_MIN  =  80;  C_ROSSO_V_MIN  =  60
C_ROSSO_SOGLIA = 150

# BLU — presente in Romania e Paesi Bassi
C_BLU_H_MIN  =  95;  C_BLU_H_MAX  = 140
C_BLU_S_MIN  =  70;  C_BLU_V_MIN  =  50
C_BLU_SOGLIA = 150

# NERO — presente in Germania
C_NERO_V_MAX  =  55
C_NERO_SOGLIA = 200

# BIANCO — presente in Austria e Paesi Bassi
C_BIANCO_S_MAX =  50;  C_BIANCO_V_MIN = 160
C_BIANCO_SOGLIA = 150

# ============================================================
#  LOGGING
# ============================================================

def log(cat, msg):
    ts = time.strftime('%H:%M:%S')
    print(f"[{ts}][{cat}] {msg}")


# ============================================================
#  COMUNICAZIONE SERIALE
# ============================================================

class Comm:
    def __init__(self):
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
            log('COMM', f"Seriale aperta su {SERIAL_PORT}")
        except Exception as e:
            log('COMM', f"ERRORE apertura seriale: {e}")
            self.ser = None

    def send(self, msg):
        if self.ser:
            self.ser.write(f"{msg}\n".encode())
            log('TX', msg)

    def receive(self):
        if self.ser and self.ser.in_waiting > 0:
            return self.ser.readline().decode('utf-8', errors='ignore').strip()
        return None

    def wait_for(self, expected, timeout=10.0):
        """Aspetta un messaggio specifico da Arduino entro timeout secondi."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            msg = self.receive()
            if msg:
                if msg.startswith('[D]'):
                    log('ARD', msg[4:])
                    continue
                log('COM', f"RX: {msg}")
                if msg == expected:
                    return True
        log('ERR', f"Timeout attesa {expected}")
        return False


# ============================================================
#  CLASSIFICATORE BANDIERE (OpenCV — no YOLO)
#
#  Logica: conta pixel per colore sull'intera immagine.
#  La combinazione di colori presenti identifica la bandiera.
#
#  Italia:      VERDE presente → fine, nessun altro controllo
#  Romania:     GIALLO + BLU
#  Germania:    GIALLO + NERO  (giallo/oro + nero)
#  Paesi Bassi: BLU + BIANCO + ROSSO  (no giallo, no nero)
#  Austria:     ROSSO + BIANCO        (solo questi due)
# ============================================================

def flush_cameras(*caps, n=6):
    for _ in range(n):
        for c in caps:
            c.read()


def classify_flag(frame):
    """
    Classifica la bandiera nell'immagine usando conteggio colori HSV.
    Restituisce: 'ITALIA' | 'ROMANIA' | 'GERMANIA' | 'AUSTRIA' | 'PAESI_BASSI' | None
    """
    if frame is None or frame.size == 0:
        return None

    frame = cv2.resize(frame, (120, 120))
    hsv   = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    h = hsv[:, :, 0].astype(int)
    s = hsv[:, :, 1].astype(int)
    v = hsv[:, :, 2].astype(int)

    # Conteggio pixel per colore
    n_verde  = int(((h >= C_VERDE_H_MIN)  & (h <= C_VERDE_H_MAX)  & (s >= C_VERDE_S_MIN)  & (v >= C_VERDE_V_MIN)).sum())
    n_giallo = int(((h >= C_GIALLO_H_MIN) & (h <= C_GIALLO_H_MAX) & (s >= C_GIALLO_S_MIN) & (v >= C_GIALLO_V_MIN)).sum())
    n_blu    = int(((h >= C_BLU_H_MIN)    & (h <= C_BLU_H_MAX)    & (s >= C_BLU_S_MIN)    & (v >= C_BLU_V_MIN)).sum())
    n_rosso  = int((((h <= C_ROSSO_H_MAX1) | (h >= C_ROSSO_H_MIN2)) & (s >= C_ROSSO_S_MIN) & (v >= C_ROSSO_V_MIN)).sum())
    n_nero   = int((v <= C_NERO_V_MAX).sum())
    n_bianco = int(((s <= C_BIANCO_S_MAX) & (v >= C_BIANCO_V_MIN)).sum())

    ha_verde  = n_verde  >= C_VERDE_SOGLIA
    ha_giallo = n_giallo >= C_GIALLO_SOGLIA
    ha_blu    = n_blu    >= C_BLU_SOGLIA
    ha_nero   = n_nero   >= C_NERO_SOGLIA
    ha_bianco = n_bianco >= C_BIANCO_SOGLIA

    log('VIS', f"px V:{n_verde} G:{n_giallo} B:{n_blu} R:{n_rosso} K:{n_nero} W:{n_bianco}")

    # Italia: verde presente → identificazione immediata
    if ha_verde:
        return 'ITALIA'

    # Romania: giallo + blu
    if ha_giallo and ha_blu:
        return 'ROMANIA'

    # Germania: giallo/oro + nero
    if ha_giallo and ha_nero:
        return 'GERMANIA'

    # Paesi Bassi: blu + bianco (no giallo, no nero)
    if ha_blu and ha_bianco:
        return 'PAESI_BASSI'

    # Austria: rosso + bianco (unica rimasta)
    if ha_bianco:
        return 'AUSTRIA'

    return None   # non identificata


def scan_wall(cap, n_frames=3):
    """
    Scansiona una parete con la camera, classifica la bandiera.
    Ritorna il nome della bandiera o None.
    """
    best = None
    counts = {}

    for _ in range(n_frames):
        cap.read()   # scarta frame stantio
        ret, frame = cap.read()
        if not ret or frame is None:
            continue
        name = classify_flag(frame)
        if name:
            counts[name] = counts.get(name, 0) + 1

    if counts:
        best = max(counts, key=counts.get)

    return best


# ============================================================
#  NAVIGAZIONE — WALL FOLLOWER
#
#  right_hand=True  → right-hand rule (Lap 1, orario)
#  right_hand=False → left-hand rule  (Lap 2, antiorario)
#
#  Ritorna la direzione relativa (0=dritto, 1=dx, 3=sx)
#  basandosi su F, R, L (muri rilevati).
# ============================================================

def wall_follower_dir(f, r, l, right_hand=True):
    if right_hand:
        # Preferisci destra > dritto > sinistra > dietro
        if r == 0:   return 1   # destra libera → gira destra
        if f == 0:   return 0   # avanti libero → dritto
        if l == 0:   return 3   # sinistra libera → gira sinistra
        return 2                # vicolo cieco → dietro
    else:
        # Preferisci sinistra > dritto > destra > dietro
        if l == 0:   return 3
        if f == 0:   return 0
        if r == 0:   return 1
        return 2


# ============================================================
#  STATE MACHINE
# ============================================================

STATE_ZONA_A        = 'ZONA_A'
STATE_ZONA_B_ENTER  = 'ZONA_B_ENTER'
STATE_ZONA_B_LAP1   = 'ZONA_B_LAP1'
STATE_ZONA_B_BLINK  = 'ZONA_B_BLINK'
STATE_ZONA_B_LAP2   = 'ZONA_B_LAP2'
STATE_DONE          = 'DONE'


# ============================================================
#  MAIN
# ============================================================

def main():
    comm  = Comm()
    cap_sx = cv2.VideoCapture(0)
    cap_dx = cv2.VideoCapture(2)

    for cap in [cap_sx, cap_dx]:
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    time.sleep(2)
    while comm.receive():
        pass

    log('SYS', 'Tech Challenge pronto. Premi BTN_START su Arduino.')
    log('SYS', f"Camera SX: {'OK' if cap_sx.isOpened() else 'ERR'}")
    log('SYS', f"Camera DX: {'OK' if cap_dx.isOpened() else 'ERR'}")

    state       = STATE_ZONA_A
    target_flag = None     # bandiera target (determinata all'ingresso Zona B)
    flag_counts = {}       # {nome_bandiera: conteggio} — rilevate nel lap 1
    lap1_done   = False
    left_checkpoint = False   # flag: il bot ha lasciato il checkpoint iniziale di Zona B
    drop_done   = set()    # bandiere già droppate nel lap 2

    while True:
        msg = comm.receive()
        if not msg:
            time.sleep(0.01)
            continue

        if msg.startswith('[D]'):
            log('ARD', msg[4:])
            continue

        log('COM', f"RX: {msg}")

        # ----------------------------------------------------------------
        #  INIT
        # ----------------------------------------------------------------
        if msg == '<INIT>':
            state       = STATE_ZONA_A
            target_flag = None
            flag_counts = {}
            lap1_done   = False
            left_checkpoint = False
            drop_done   = set()
            log('NAV', '=== TECH CHALLENGE INIZIATA ===')
            log('NAV', f"Stato: {state}")

        # ----------------------------------------------------------------
        #  DATA — ricezione muri + colore tile
        # ----------------------------------------------------------------
        elif msg.startswith('<DATA:'):
            inner = msg[6:-1]
            c_str, frl_str = inner.split(',')
            c = int(c_str)
            f, r, l = int(frl_str[0]), int(frl_str[1]), int(frl_str[2])

            log('NAV', f"[{state}] Tile:{TILE_NOMI.get(c,'?')} F:{f} R:{r} L:{l}")

            # ---- ZONA A ----
            if state == STATE_ZONA_A:
                if c == TILE_GREY:
                    # Checkpoint raggiunto → transizione Zona B
                    log('NAV', 'CHECKPOINT raggiunto! Ingresso Zona B.')
                    state = STATE_ZONA_B_ENTER
                    # Prima cosa in Zona B: scansiona muro frontale (bandiera target)
                    # Chiedi ad Arduino di fare SCAN:FRONT se c'è muro frontale
                    if f == 1:
                        comm.send('<SCAN:FRONT>')
                        comm.wait_for('<SCAN:OK>', timeout=8)
                        flush_cameras(cap_dx, n=5)
                        time.sleep(CAMERA_SETTLE_TIME)
                        target_flag = scan_wall(cap_dx)
                        comm.send('<SCAN:DONE>')
                        log('NAV', f"Bandiera TARGET rilevata: {target_flag}")
                    else:
                        log('NAV', 'Nessun muro frontale in checkpoint — target non rilevato automaticamente')
                    # Avvia Lap 1
                    state = STATE_ZONA_B_LAP1
                    left_checkpoint = False
                    log('NAV', 'Stato → ZONA_B_LAP1 (right-hand wall follower)')
                    # Invia GO per iniziare traversata
                    rel = wall_follower_dir(f, r, l, right_hand=True)
                    comm.send(f'<GO:{rel}>')

                elif c == TILE_BLU:
                    log('NAV', 'Tile BLU → gira sinistra')
                    comm.send('<GO:3>')

                elif c == TILE_ROSSA:
                    log('NAV', 'Tile ROSSA → gira destra')
                    comm.send('<GO:1>')

                else:   # NORM
                    log('NAV', 'Tile NORM → dritto')
                    comm.send('<GO:0>')

            # ---- ZONA B LAP 1 ----
            elif state == STATE_ZONA_B_LAP1:
                # Scansiona muri adiacenti per bandiere
                flush_cameras(cap_sx, cap_dx, n=4)
                time.sleep(CAMERA_SETTLE_TIME)

                walls_to_scan = []
                if r == 1: walls_to_scan.append(('R', cap_dx))
                if l == 1: walls_to_scan.append(('L', cap_sx))

                for pos, cam in walls_to_scan:
                    name = scan_wall(cam)
                    if name:
                        flag_counts[name] = flag_counts.get(name, 0) + 1
                        log('VIS', f"Bandiera {name} su parete {pos} — totale: {flag_counts[name]}")

                if f == 1:
                    comm.send('<SCAN:FRONT>')
                    comm.wait_for('<SCAN:OK>', timeout=8)
                    flush_cameras(cap_dx, n=4)
                    time.sleep(CAMERA_SETTLE_TIME)
                    name = scan_wall(cap_dx)
                    if name:
                        flag_counts[name] = flag_counts.get(name, 0) + 1
                        log('VIS', f"Bandiera {name} su muro FRONT — totale: {flag_counts[name]}")
                    comm.send('<SCAN:DONE>')

                log('VIS', f"Conteggio bandiere: {flag_counts}")

                # Checkpoint di uscita = grey tile dopo aver lasciato il primo
                if not left_checkpoint:
                    if c != TILE_GREY:
                        left_checkpoint = True
                else:
                    if c == TILE_GREY:
                        # Fine Lap 1
                        log('NAV', '--- LAP 1 COMPLETATO ---')
                        log('NAV', f"Conteggio finale: {flag_counts}")
                        state = STATE_ZONA_B_BLINK
                        n_target = flag_counts.get(target_flag, 0) if target_flag else 0
                        log('NAV', f"Bandiera target '{target_flag}' contata {n_target} volte → BLINK x{n_target}")
                        comm.send(f'<BLINK:{n_target}>')
                        # Aspetta conferma lampeggio
                        comm.wait_for('<BLINK:DONE>', timeout=30)
                        log('NAV', 'Lampeggio completato. Avvio LAP 2 (180° + left-hand)')
                        state = STATE_ZONA_B_LAP2
                        drop_done = set()
                        # Rotate 180° e inizia Lap 2
                        comm.send('<GO:2>')
                        continue

                # Continua Lap 1 con right-hand
                rel = wall_follower_dir(f, r, l, right_hand=True)
                comm.send(f'<GO:{rel}>')

            # ---- ZONA B LAP 2 ----
            elif state == STATE_ZONA_B_LAP2:
                # Calcola ranking frequenza bandiere
                ranking = sorted(flag_counts.items(), key=lambda x: x[1], reverse=True)
                kits_map = {}
                for i, (name, _) in enumerate(ranking[:3]):
                    kits_map[name] = [3, 2, 1][i]

                log('NAV', f"Ranking: {ranking[:3]}  Kit: {kits_map}")

                # Controlla se c'è una bandiera da droppare su questo muro
                flush_cameras(cap_sx, cap_dx, n=4)
                time.sleep(CAMERA_SETTLE_TIME)

                walls_check = []
                if r == 1: walls_check.append(('R', cap_dx))
                if l == 1: walls_check.append(('L', cap_sx))
                if f == 1: walls_check.append(('F', None))

                for pos, cam in walls_check:
                    if pos == 'F':
                        comm.send('<SCAN:FRONT>')
                        comm.wait_for('<SCAN:OK>', timeout=8)
                        flush_cameras(cap_dx, n=4)
                        time.sleep(CAMERA_SETTLE_TIME)
                        name = scan_wall(cap_dx)
                        comm.send('<SCAN:DONE>')
                    else:
                        name = scan_wall(cam)

                    if name and name in kits_map and name not in drop_done:
                        n_kits = kits_map[name]
                        log('NAV', f"DROP: {name} → {n_kits} kit (rank #{list(kits_map.keys()).index(name)+1})")
                        comm.send(f'<DROP:{n_kits}>')
                        comm.wait_for('<DROP:DONE>', timeout=20)
                        drop_done.add(name)
                        log('NAV', f"Drop completato. Droppati: {drop_done}")

                # Tutti i drop completati?
                if len(drop_done) >= min(3, len(kits_map)):
                    log('NAV', '=== ZONA B COMPLETATA — EXIT BONUS! ===')
                    state = STATE_DONE
                    continue

                # Continua Lap 2 con left-hand
                rel = wall_follower_dir(f, r, l, right_hand=False)
                comm.send(f'<GO:{rel}>')

            elif state == STATE_DONE:
                log('NAV', 'Missione completata. Nessun altro comando inviato.')

        # ----------------------------------------------------------------
        #  MOVE:OK / MOVE:STUCK
        # ----------------------------------------------------------------
        elif msg == '<MOVE:OK>':
            log('NAV', f"[{state}] Avanzamento OK")

        elif msg == '<MOVE:STUCK>':
            log('NAV', f"[{state}] STUCK — Arduino ha fatto backup. Attesa prossimo DATA.")


if __name__ == '__main__':
    main()
