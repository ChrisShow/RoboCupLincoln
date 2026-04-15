import serial
import time
import collections
import cv2
import numpy as np
from ultralytics import YOLO
import os
import heapq

# --- CONFIGURAZIONE ---
SERIAL_PORT = '/dev/ttyACM0' 
BAUD_RATE = 115200

# Range HSV per Cognitive Targets (V5 Strict)
COLORI_HSV = {
    "VERDE":  [(np.array([35, 50, 40]),   np.array([85, 255, 255]))],
    "ROSSO":  [(np.array([0, 70, 50]),    np.array([10, 255, 255])),
               (np.array([160, 70, 50]),  np.array([180, 255, 255]))],
    "NERO":   [(np.array([0, 0, 0]),      np.array([180, 255, 55]))], 
    "GIALLO": [(np.array([18, 90, 100]),  np.array([35, 255, 255]))],
    "BLU":    [(np.array([95, 80, 80]),   np.array([130, 255, 255]))]
}
VALORI_MATEMATICI = {"NERO": -2, "ROSSO": -1, "GIALLO": 0, "VERDE": 1, "BLU": 2}
MAPPATURA_SOMMA = {2: 3, 1: 2, 0: 1}

class Cell:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.walls = [None, None, None, None] # 0:N, 1:E, 2:S, 3:O
        self.visited = False
        self.color = 0

class Robot:
    def __init__(self):
        self.x = 0
        self.y = 0
        self.orientation = 0 # 0:N, 1:E, 2:S, 3:O
        self.map = {}
        self.stack = []
        self.visited = set()
        self.last_move = 0 # Memorizza l'ultimo <GO:X> inviato
        self.returning_home = False
        self.dijkstra_path = []

    def update_position(self):
        """Aggiorna x, y e orientamento in base all'ultima mossa riuscita."""
        self.orientation = (self.orientation + self.last_move) % 4
        if self.orientation == 0: self.y += 1
        elif self.orientation == 1: self.x += 1
        elif self.orientation == 2: self.y -= 1
        elif self.orientation == 3: self.x -= 1

    def get_shortest_path(self, start, end):
        queue = [(0, start, [])]
        seen = set()
        while queue:
            (cost, curr, path) = heapq.heappop(queue)
            if curr not in seen:
                seen.add(curr)
                path = path + [curr]
                if curr == end: return path
                cell = self.map.get(curr)
                if not cell: continue
                for direction, is_wall in enumerate(cell.walls):
                    if is_wall is False:
                        nx, ny = curr
                        if direction == 0: ny += 1
                        elif direction == 1: nx += 1
                        elif direction == 2: ny -= 1
                        elif direction == 3: nx -= 1
                        if (nx, ny) in self.map:
                            heapq.heappush(queue, (cost + 1, (nx, ny), path))
        return []

class VisionSystem:
    def __init__(self):
        model_path = os.path.join(os.path.dirname(__file__), 'vision', 'model_v2', 'runs', 'detect', 'train', 'weights', 'best.pt')
        self.model = None
        if os.path.exists(model_path):
            self.model = YOLO(model_path)
            print(f"[VISION] YOLO Modello V2 caricato.")
        self.cl_map = {0:(3,10), 1:(1,10), 2:(2,10), 3:("COG",5), 4:(0,0), 5:(0,0)}

    def scan_wall(self, side, frame):
        if frame is None or self.model is None: return []
        results = self.model.predict(frame, imgsz=640, conf=0.5, verbose=False)
        candidati = []
        for r in results:
            for box in r.boxes:
                cls_id = int(box.cls[0])
                conf = float(box.conf[0])
                res_tipo, priorita = self.cl_map.get(cls_id, (0, 0))
                if priorita == 10: candidati.append({'tipo': res_tipo, 'prio': priorita, 'conf': conf})
                elif res_tipo == "COG":
                    x1, y1, x2, y2 = map(int, box.xyxy[0])
                    crop = frame[y1:y2, x1:x2]
                    tipo_mat = self._analizza_cognitive(crop)
                    if tipo_mat > 0: candidati.append({'tipo': tipo_mat, 'prio': priorita, 'conf': conf})
        if not candidati: return []
        candidati.sort(key=lambda x: (x['prio'], x['conf']), reverse=True)
        return [(candidati[0]['tipo'], side)]

    def _analizza_cognitive(self, crop):
        if crop.size == 0: return 0
        h, w = crop.shape[:2]
        hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
        cx, cy = w // 2, h // 2
        r_w, r_h = (w//2)*0.9, (h//2)*0.9
        somma, trovati = 0, 0
        for i in range(5):
            scale = (i + 0.5) / 5.0
            voti = []
            for a in range(0, 360, 20):
                px, py = int(cx + (r_w*scale)*np.cos(np.radians(a))), int(cy + (r_h*scale)*np.sin(np.radians(a)))
                if 0<=py<h and 0<=px<w:
                    h_val, s_val, v_val = hsv[py, px]
                    for n, rs in COLORI_HSV.items():
                        for (l, u) in rs:
                            if l[0]<=h_val<=u[0] and l[1]<=s_val<=u[1] and l[2]<=v_val<=u[2]:
                                voti.append(n); break
            if voti:
                c_dom = max(set(voti), key=voti.count)
                if voti.count(c_dom) > (len(voti)*0.6): somma += VALORI_MATEMATICI[c_dom]; trovati += 1
        return MAPPATURA_SOMMA.get(somma, 0) if trovati == 5 else 0

class Comm:
    def __init__(self):
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        except:
            self.ser = None

    def send(self, msg):
        if self.ser: self.ser.write(f"{msg}\n".encode())

    def receive(self):
        if self.ser and self.ser.in_waiting > 0:
            return self.ser.readline().decode('utf-8', errors='ignore').strip()
        return None

def get_next_move(robot):
    curr = (robot.x, robot.y)
    cell = robot.map[curr]
    if robot.returning_home:
        if not robot.dijkstra_path: return None, None
        target = robot.dijkstra_path.pop(0)
        dx, dy = target[0] - robot.x, target[1] - robot.y
        abs_dir = 0
        if dx == 1: abs_dir = 1
        elif dx == -1: abs_dir = 3
        elif dy == 1: abs_dir = 0
        elif dy == -1: abs_dir = 2
        return (abs_dir - robot.orientation) % 4, target
    for rel in [0, 1, 3]:
        abs_d = (robot.orientation + rel) % 4
        if cell.walls[abs_d] is False:
            nx, ny = robot.x, robot.y
            if abs_d == 0: ny += 1
            elif abs_d == 1: nx += 1
            elif abs_d == 2: ny -= 1
            elif abs_d == 3: nx -= 1
            if (nx, ny) not in robot.visited:
                robot.stack.append(curr)
                return rel, (nx, ny)
    if robot.stack:
        target = robot.stack.pop()
        dx, dy = target[0] - robot.x, target[1] - robot.y
        abs_d = 0
        if dx == 1: abs_d = 1
        elif dx == -1: abs_d = 3
        elif dy == 1: abs_d = 0
        elif dy == -1: abs_d = 2
        return (abs_d - robot.orientation) % 4, target
    path = robot.get_shortest_path(curr, (0,0))
    if path:
        robot.returning_home = True
        robot.dijkstra_path = path[1:]
        return get_next_move(robot)
    return None, None

def main():
    comm = Comm()
    vision = VisionSystem()
    robot = Robot()
    
    # Inizializzazione Cam con MJPG per evitare timeout su RPi
    cap_dx = cv2.VideoCapture(0)
    cap_sx = cv2.VideoCapture(2)
    for cap in [cap_dx, cap_sx]:
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)

    time.sleep(2)
    while comm.receive(): pass 

    while True:
        msg = comm.receive()
        if not msg: 
            time.sleep(0.01)
            continue
        
        print(f"RX: {msg}")

        if msg == "<INIT>":
            robot.x, robot.y, robot.orientation = 0, 0, 0
            robot.visited = set()
            robot.map = {}
            print("Gara Iniziata.")

        elif msg.startswith("<DATA:"):
            c, frl = msg[6:-1].split(',')
            f, r, l = [int(x) for x in frl]
            curr = (robot.x, robot.y)
            if curr not in robot.map: robot.map[curr] = Cell(robot.x, robot.y)
            cell = robot.map[curr]
            cell.visited = True
            robot.visited.add(curr)
            cell.walls[robot.orientation] = (f == 1)
            cell.walls[(robot.orientation + 1) % 4] = (r == 1)
            cell.walls[(robot.orientation + 3) % 4] = (l == 1)
            cell.walls[(robot.orientation + 2) % 4] = False

            vittime = []
            for _ in range(5): cap_dx.read(); cap_sx.read()
            _, frame_r = cap_dx.read()
            _, frame_l = cap_sx.read()
            if r == 1: vittime.extend(vision.scan_wall('R', frame_r))
            if l == 1: vittime.extend(vision.scan_wall('L', frame_l))
            if f == 1:
                comm.send("<SCAN:FRONT>")
                while True:
                    m = comm.receive()
                    if m == "<SCAN:OK>": break
                    time.sleep(0.01)
                for _ in range(5): cap_dx.read()
                _, frame_f = cap_dx.read()
                vittime.extend(vision.scan_wall('F', frame_f))
                comm.send("<SCAN:DONE>")

            for v_t, v_p in vittime: comm.send(f"<VIC:{v_p},{v_t}>")
            comm.send("<VIC:DONE>")
            while True:
                m = comm.receive()
                if m == "<VIC:DONE>": break
                time.sleep(0.01)

            rel_dir, _ = get_next_move(robot)
            if rel_dir is not None:
                robot.last_move = rel_dir
                comm.send(f"<GO:{rel_dir}>")
            else: print("Missione Compiuta.")

        elif msg == "<MOVE:OK>":
            robot.update_position()
            print(f"Pos: ({robot.x}, {robot.y}) Ori: {robot.orientation}")

        elif msg == "<MOVE:RAMP>":
            robot.update_position()
            robot.update_position()
            print(f"RAMPA! Pos: ({robot.x}, {robot.y})")

if __name__ == "__main__":
    main()
