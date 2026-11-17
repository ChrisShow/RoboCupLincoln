"""
Dashboard dimostrativa di mENIAC: camere live + stato di sensori e motori.

    cd raspberry/src
    python3 -m dashboard.standalone              # robot vero (Arduino con dashboard_demo.ino)
    python3 -m dashboard.standalone --demo       # dati finti, gira anche sul PC senza robot

Sull'Arduino va caricato arduino/firmware/src/dashboard_demo/dashboard_demo.ino:
legge i sensori e li invia, ma i motori restano sempre fermi.

Non avviarlo insieme a main.py: seriale e camere possono essere aperte
da un solo programma alla volta.
"""

import argparse
import math
import random
import time

from .camera import FakeCamera, SharedCamera
from .server import Dashboard


def run_serial(dash, port, baud):
    import serial  # pyserial
    while True:
        try:
            ser = serial.Serial(port, baud, timeout=0.5)
            dash.serial_status(port=port, connected=True)
            dash.log("SYS", f"Seriale aperta su {port} @ {baud}")
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="ignore").strip()
                if not line or dash.telemetry(line):
                    continue
                dash.serial_rx(line)
                if line.startswith("[D]"):
                    dash.log("ARD", line[4:])
                else:
                    dash.log("COM", f"RX: {line}")
        except Exception as e:
            dash.serial_status(port=port, connected=False, error=str(e))
            dash.log("SYS", f"Seriale non disponibile ({e}) — riprovo tra 3s")
            time.sleep(3)


# ---------------------------------------------------------------- modalità demo

class _Cell:
    def __init__(self, walls, color=0):
        self.walls, self.color, self.visited = walls, color, True


class _DemoRobot:
    def __init__(self):
        self.x, self.y, self.orientation = 1, 2, 1
        self.visited = {(0, 0), (0, 1), (0, 2), (1, 2)}
        self.returning_home, self.dijkstra_path, self.no_transit = False, [], set()
        self.map = {
            (0, 0): _Cell([False, True, True, True]),
            (0, 1): _Cell([False, True, False, True], 3),
            (0, 2): _Cell([True, False, False, True]),
            (1, 2): _Cell([True, False, True, None]),
            (2, 2): _Cell([None, None, None, None], 4),
        }
        self.map[(2, 2)].visited = False


def run_demo(dash):
    dash.serial_status(port="DEMO", connected=True)
    robot = _DemoRobot()
    dash.update_nav(robot, phase="ESPLORA", last_cmd="<GO:0>",
                    cell_info="F:NO R:SI L:SI — NORMALE")
    dash.add_victim(0, 2, "L", 3, "PHI", 1)
    dash.set_model("model_v3 (demo)", True)
    t0, last_log = time.time(), 0
    servo = 40
    while True:
        t = time.time() - t0
        w = lambda base, amp, ph: int(max(15, (base + amp * math.sin(t * 0.7 + ph)) * 10))
        pwm = int(60 + 25 * math.sin(t * 0.5))
        drift = int(12 * math.sin(t * 1.3))
        line = (f"<TEL:{int(t*1000)},{w(18, 10, 0)},{w(35, 5, 1)},{w(9, 2, 2)},{w(10, 2, 2.3)},"
                f"{65535 if int(t) % 20 == 7 else w(26, 12, 3)},{w(27, 12, 3.2)},"
                f"{120 + random.randint(-6, 6)},{140 + random.randint(-6, 6)},{110 + random.randint(-6, 6)},0,"
                f"{pwm + drift},{pwm - drift},{servo},1,0,{int(int(t) % 4 == 0)},"
                f"{(t * 15) % 360 - 180:.1f},{3 * math.sin(t):.1f}>")
        dash.telemetry(line)
        if t - last_log > 2.5:
            last_log = t
            dash.log(random.choice(["NAV", "VIS", "ARD", "COM"]),
                     random.choice(["SCAN F:18.2cm R:9.1cm L:26.0cm Col:0", "Avanzamento OK  F:4.1cm",
                                    "TX: <GO:0>", "Scansione parete DESTRA (3 frame)...",
                                    "Nessuna vittima rilevata"]))
            for cam, wall in (("sx", "L"), ("dx", "R")):
                ok, frame = dash.cameras[cam].read()
                dash.vision_result(cam, wall, random.choice(["PHI", "PSI", None]),
                                   random.uniform(0.66, 0.97), frame)
        time.sleep(0.1)


def main():
    ap = argparse.ArgumentParser(description="Dashboard mENIAC senza programma di gara")
    ap.add_argument("--port", type=int, default=8080, help="porta web (default 8080)")
    ap.add_argument("--serial", default="/dev/ttyACM0", help="porta seriale Arduino")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--cam-sx", type=int, default=0, help="indice camera sinistra (default 0)")
    ap.add_argument("--cam-dx", type=int, default=2, help="indice camera destra (default 2)")
    ap.add_argument("--fps", type=int, default=12, help="fps massimi dello stream video")
    ap.add_argument("--token", default=None, help="password opzionale (?token=...)")
    ap.add_argument("--demo", action="store_true", help="dati e camere finti, senza hardware")
    args = ap.parse_args()

    dash = Dashboard.start(port=args.port, token=args.token,
                           mode="demo" if args.demo else "standalone")
    for line in list(dash._logs):
        print(f"[{line['cat']}] {line['msg']}")

    if args.demo:
        dash.register_camera(FakeCamera("sx", stream_fps=args.fps))
        dash.register_camera(FakeCamera("dx", stream_fps=args.fps))
        run_demo(dash)
    else:
        for name, idx in (("sx", args.cam_sx), ("dx", args.cam_dx)):
            cam = dash.register_camera(SharedCamera(idx, name, stream_fps=args.fps))
            dash.log("SYS", f"Camera {name.upper()} (/dev/video{idx}): {'OK' if cam.isOpened() else 'ERRORE'}")
        run_serial(dash, args.serial, args.baud)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
