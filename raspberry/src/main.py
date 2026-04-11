import serial
import time
import collections

# --- CONFIGURAZIONE ---
SERIAL_PORT = '/dev/ttyACM0' # TODO: Verificare porta corretta
BAUD_RATE = 115200

class Robot:
    def __init__(self):
        self.x = 0
        self.y = 0
        self.orientation = 0 # 0: Nord, 1: Est, 2: Sud, 3: Ovest
        self.map = {} # (x,y) -> Cell object
        self.stack = [] # LIFO per backtracking
        self.visited = set()
        self.checkpoints = []

class Cell:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.walls = [False, False, False, False] # N, E, S, O
        self.visited = False
        self.color = 0 # 0: Normale, 1: Grigia, 2: Rossa, 3: Blu, 4: Nera

class VisionSystem:
    def __init__(self):
        # TODO: Inizializzare 2x Innomaker U20CAM con OpenCV
        pass

    def scan_wall(self, side):
        """ side: 'R' (Destra), 'L' (Sinistra), 'F' (Fronte) """
        print(f"[VISION] Scansione muro {side}...")
        # TODO: Implementare Pipeline da rilevazioneTargets.md
        # 1. Arresto Assoluto (già garantito dal protocollo)
        # 2. Svuotamento buffer
        # 3. Scatto 1080p
        # 4. ROI e Biforcazione (Lettere / Cognitive)
        
        # Esempio ritorno (Tipo 0=Fake, 1=Omega, 2=Psi, 3=Phi)
        # Ritorna lista di (tipo, pos)
        return [] 

class Comm:
    def __init__(self):
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            print(f"Connesso ad Arduino su {SERIAL_PORT}")
        except:
            print("ERRORE: Impossibile connettersi ad Arduino")
            self.ser = None

    def send(self, msg):
        if self.ser:
            self.ser.write(msg.encode())
            print(f"PI -> ARDUINO: {msg}")

    def receive(self):
        if self.ser and self.ser.in_waiting > 0:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"ARDUINO -> PI: {line}")
                return line
        return None

def main():
    comm = Comm()
    vision = VisionSystem()
    robot = Robot()
    
    state = "WAIT_INIT"
    
    while True:
        msg = comm.receive()
        
        if msg == "<INIT>":
            state = "SCAN"
            print("Gara Iniziata!")
            
        elif msg == "<LOP>":
            state = "WAIT_INIT"
            # TODO: Reset logico alle coordinate dell'ultimo checkpoint
            print("Lack of Progress ricevuto.")

        if state == "SCAN" and msg and msg.startswith("<DATA:"):
            # Esempio: <DATA:C,FRL> -> <DATA:0,101>
            content = msg[6:-1].split(',')
            color = int(content[0])
            walls_bin = content[1] # "101" (Front, Right, Left)
            
            # 1. Aggiornamento Mappa (Punto 3 schema)
            curr_pos = (robot.x, robot.y)
            if curr_pos not in robot.map:
                robot.map[curr_pos] = Cell(robot.x, robot.y)
            
            cell = robot.map[curr_pos]
            cell.color = color
            cell.visited = True
            robot.visited.add(curr_pos)

            if color == 1: # Grigia (Checkpoint)
                robot.checkpoints.append(curr_pos)
            elif color == 3: # Blu (Delay 5s)
                time.sleep(5)

            # 2. Analisi Vittime (Punto 4 schema)
            F, R, L = [int(x) for x in walls_bin]
            vittime = []

            if R == 1:
                vittime.extend(vision.scan_wall('R'))
            if L == 1:
                vittime.extend(vision.scan_wall('L'))
            if F == 1:
                comm.send("<SCAN:FRONT>")
                # Attesa SCAN:OK
                while True:
                    resp = comm.receive()
                    if resp == "<SCAN:OK>":
                        vittime.extend(vision.scan_wall('F'))
                        comm.send("<SCAN:DONE>")
                        break
                    time.sleep(0.1)

            # 3. Invio Esiti Soccorso (Punto 8 schema)
            if vittime:
                for v_tipo, v_pos in vittime:
                    comm.send(f"<VIC:{v_pos},{v_tipo}>")
                
                # Attesa fine soccorso
                while True:
                    resp = comm.receive()
                    if resp == "<VIC:DONE>":
                        break
                    time.sleep(0.1)

            # 4. Pathfinding (Punto 10 schema)
            # TODO: Implementare algoritmo di esplorazione (es. DFS con priorità Dritto, DX, SX)
            # Per ora mandiamo un comando dummy
            next_dir = 0 # 0=Dritto
            comm.send(f"<GO:{next_dir}>")
            state = "MOVING"

        elif state == "MOVING" and msg:
            if msg == "<MOVE:OK>":
                # Aggiorna coordinate in base all'orientamento attuale
                if robot.orientation == 0: robot.y += 1
                elif robot.orientation == 1: robot.x += 1
                elif robot.orientation == 2: robot.y -= 1
                elif robot.orientation == 3: robot.x -= 1
                state = "SCAN"
            elif msg == "<MOVE:BLACK>":
                # TODO: Segna cella come inaccessibile e ricalcola
                state = "SCAN"

        time.sleep(0.05)

if __name__ == "__main__":
    main()
