"""
Server web della dashboard mENIAC.

Usa solo la libreria standard di Python (+ OpenCV per comprimere le immagini):
niente Flask, niente CDN → funziona anche senza internet, ad es. con il PC
collegato all'hotspot del Raspberry durante la gara.

Endpoint:
  GET /                 pagina della dashboard
  GET /events           Server-Sent Events: stato completo ~5 volte al secondo + nuove righe di log
  GET /api/state        stato completo in JSON (utile per script / debug)
  GET /stream/<cam>     video MJPEG live della camera (sx | dx)  — ?fps=N per limitarlo
  GET /snapshot/<cam>   ultimo frame analizzato da YOLO, con i box disegnati
"""

import collections
import json
import os
import shutil
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import cv2

HERE = os.path.dirname(os.path.abspath(__file__))

# Ordine dei campi della riga <TEL:...> inviata da Arduino (vedi README.md).
# I campi mancanti in coda (firmware vecchio) restano None.
TEL_FIELDS = [
    "ms",
    "tof_front", "tof_back", "tof_ra", "tof_rp", "tof_la", "tof_lp",
    "col_r", "col_g", "col_b", "color",
    "pwm_l", "pwm_r",
    "servo",
    "running", "button", "led",
    "yaw", "pitch",
]
TOF_KEYS = ["front", "back", "ra", "rp", "la", "lp"]
COLOR_NAMES = {-1: "NON CALIBRATO", 0: "NORMALE", 1: "GRIGIO", 2: "ROSSO", 3: "BLU", 4: "NERO"}
ORI_NAMES = ["N", "E", "S", "O"]

ARDUINO_STALE_S = 2.0


def _now():
    return time.time()


def _num(s):
    s = s.strip()
    if s in ("", "-", "nan", "NaN"):
        return None
    try:
        v = float(s)
    except ValueError:
        return None
    return int(v) if v.is_integer() else v


def _tof_entry(mm):
    """mm grezzi del VL53L0X → dict con cm e stato.
    65535 = timeout I2C, >= 8190 = fuori portata, 0 = errore/sensore non inizializzato."""
    if mm is None:
        return {"mm": None, "cm": None, "state": "n/d"}
    if mm >= 65535 or mm < 0:
        return {"mm": mm, "cm": None, "state": "timeout"}
    if mm >= 8190 or mm > 2000:
        return {"mm": mm, "cm": None, "state": "fuori"}
    if mm == 0:
        return {"mm": mm, "cm": None, "state": "errore"}
    return {"mm": mm, "cm": round(mm / 10.0, 1), "state": "ok"}


# ============================================================================
#  Statistiche Raspberry Pi
# ============================================================================

THROTTLE_BITS = {
    0: "Sottotensione ADESSO",
    1: "Frequenza limitata ADESSO",
    2: "Throttling ADESSO",
    3: "Limite temperatura ADESSO",
    16: "Sottotensione avvenuta",
    17: "Limitazione frequenza avvenuta",
    18: "Throttling avvenuto",
    19: "Limite temperatura avvenuto",
}


def _read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return None


def _local_ips():
    ips = set()
    try:
        out = subprocess.run(["hostname", "-I"], capture_output=True, text=True, timeout=1)
        ips.update(out.stdout.split())
    except Exception:
        pass
    if not ips:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("10.255.255.255", 1))   # nessun pacchetto viene inviato
            ips.add(s.getsockname()[0])
            s.close()
        except Exception:
            pass
    return sorted(ip for ip in ips if ":" not in ip)


def pi_stats():
    st = {"hostname": socket.gethostname(), "ips": _local_ips()}

    t = _read("/sys/class/thermal/thermal_zone0/temp")
    st["cpu_temp"] = round(int(t) / 1000.0, 1) if t else None

    try:
        st["load"] = [round(x, 2) for x in os.getloadavg()]
    except OSError:
        st["load"] = None
    st["cpus"] = os.cpu_count()

    mem = _read("/proc/meminfo")
    if mem:
        kv = {}
        for line in mem.splitlines():
            k, _, v = line.partition(":")
            kv[k] = int(v.split()[0]) if v.split() else 0
        total, avail = kv.get("MemTotal", 0), kv.get("MemAvailable", 0)
        st["mem"] = {"total_mb": total // 1024, "used_mb": (total - avail) // 1024}
    else:
        st["mem"] = None

    try:
        du = shutil.disk_usage("/")
        st["disk"] = {"total_gb": round(du.total / 1e9, 1), "used_gb": round(du.used / 1e9, 1)}
    except OSError:
        st["disk"] = None

    up = _read("/proc/uptime")
    st["uptime_s"] = int(float(up.split()[0])) if up else None

    st["throttled"] = None
    if shutil.which("vcgencmd"):
        try:
            out = subprocess.run(["vcgencmd", "get_throttled"], capture_output=True,
                                 text=True, timeout=1).stdout
            val = int(out.strip().split("=")[1], 16)
            st["throttled"] = {"raw": hex(val),
                               "flags": [msg for bit, msg in THROTTLE_BITS.items() if val & (1 << bit)]}
        except Exception:
            pass

    st["wifi"] = None
    wl = _read("/proc/net/wireless")
    if wl:
        for line in wl.splitlines()[2:]:
            parts = line.split()
            if len(parts) >= 4:
                try:
                    st["wifi"] = {"iface": parts[0].rstrip(":"),
                                  "quality": float(parts[2].rstrip(".")),
                                  "dbm": float(parts[3].rstrip("."))}
                except ValueError:
                    pass
                break
    return st


# ============================================================================
#  Dashboard: stato condiviso + server HTTP
# ============================================================================

class Dashboard:
    """Stato del robot condiviso tra il programma di gara e il server web.

    Tutti i metodi pubblici sono "a prova di gara": un errore nella dashboard
    viene registrato ma non viene MAI propagato al chiamante.
    """

    _instance = None

    def __init__(self, port=8080, host="0.0.0.0", token=None, mode="gara"):
        self.port = port
        self.host = host
        self.token = token or os.environ.get("DASH_TOKEN") or None
        self.started_at = _now()
        self.mode = mode
        self._lock = threading.Lock()
        self._logs = collections.deque(maxlen=600)
        self._log_seq = 0
        self.cameras = {}
        self._snapshots = {}           # cam -> (jpeg bytes, seq)
        self._snap_seq = 0
        self._tel_times = collections.deque(maxlen=50)
        self.server = None

        self.state = {
            "arduino": {"last_seen": None, "tel": None, "tof": {k: _tof_entry(None) for k in TOF_KEYS},
                        "color": None, "motors": None, "servo": None, "io": None, "imu": None},
            "serial": {"port": None, "connected": False, "rx": 0, "tx": 0, "tel": 0,
                       "last_rx": None, "last_tx": None, "error": None},
            "nav": None,
            "victims": [],
            "vision": {"model": None, "scans": {}},
            "pi": None,
        }
        threading.Thread(target=self._pi_loop, name="dash-pi", daemon=True).start()

    # ------------------------------------------------------------------ avvio
    @classmethod
    def start(cls, port=8080, host="0.0.0.0", token=None, mode="gara"):
        """Crea (una sola volta) la dashboard e avvia il server web in background."""
        if cls._instance is not None:
            return cls._instance
        dash = cls(port=port, host=host, token=token, mode=mode)
        cls._instance = dash
        try:
            handler = type("H", (_Handler,), {"dash": dash})
            dash.server = ThreadingHTTPServer((host, port), handler)
            dash.server.daemon_threads = True
            threading.Thread(target=dash.server.serve_forever, name="dash-http", daemon=True).start()
            ips = _local_ips() or ["<ip-del-raspberry>"]
            urls = ", ".join(f"http://{ip}:{port}" for ip in ips)
            dash.log("DASH", f"Dashboard attiva su {urls}" + ("  (token richiesto)" if dash.token else ""))
        except Exception as e:
            dash.log("DASH", f"Impossibile avviare il server web sulla porta {port}: {e}")
        return dash

    def _safe(fn):
        def wrapper(self, *a, **kw):
            try:
                return fn(self, *a, **kw)
            except Exception as e:  # la dashboard non deve mai fermare la gara
                try:
                    self._append_log("DASH", f"errore interno in {fn.__name__}: {e!r}")
                except Exception:
                    pass
                return None
        wrapper.__name__ = fn.__name__
        return wrapper

    # ------------------------------------------------------------------ log
    def _append_log(self, cat, msg):
        with self._lock:
            self._log_seq += 1
            self._logs.append({"seq": self._log_seq, "t": _now(), "cat": cat, "msg": str(msg)})

    @_safe
    def log(self, cat, msg):
        self._append_log(cat, msg)

    # ------------------------------------------------------------------ seriale
    @_safe
    def serial_status(self, port=None, connected=None, error=None):
        with self._lock:
            s = self.state["serial"]
            if port is not None:
                s["port"] = port
            if connected is not None:
                s["connected"] = connected
            s["error"] = error

    @_safe
    def serial_rx(self, line):
        with self._lock:
            s = self.state["serial"]
            s["rx"] += 1
            s["last_rx"] = {"t": _now(), "msg": line}
            self.state["arduino"]["last_seen"] = _now()

    @_safe
    def serial_tx(self, msg):
        with self._lock:
            s = self.state["serial"]
            s["tx"] += 1
            s["last_tx"] = {"t": _now(), "msg": msg}

    @_safe
    def telemetry(self, line):
        """Analizza una riga <TEL:...>. Ritorna True se era telemetria."""
        if not line.startswith("<TEL:"):
            return False
        parts = line[5:].rstrip(">").split(",")
        v = {name: (_num(parts[i]) if i < len(parts) else None) for i, name in enumerate(TEL_FIELDS)}
        now = _now()
        with self._lock:
            self._tel_times.append(now)
            a = self.state["arduino"]
            a["last_seen"] = now
            a["tel"] = {"ms": v["ms"]}
            a["tof"] = {k: _tof_entry(v["tof_" + k]) for k in TOF_KEYS}
            a["color"] = {"r": v["col_r"], "g": v["col_g"], "b": v["col_b"], "cls": v["color"],
                          "name": COLOR_NAMES.get(v["color"], "?")}
            a["motors"] = {"l": v["pwm_l"], "r": v["pwm_r"]}
            a["servo"] = v["servo"]
            a["io"] = {"running": bool(v["running"]), "button": bool(v["button"]), "led": bool(v["led"])}
            a["imu"] = {"yaw": v["yaw"], "pitch": v["pitch"]} if v["yaw"] is not None else None
            self.state["serial"]["tel"] += 1
        return True

    # ------------------------------------------------------------------ camere / visione
    def register_camera(self, cam):
        self.cameras[cam.name] = cam
        return cam

    @_safe
    def set_model(self, name, loaded):
        with self._lock:
            self.state["vision"]["model"] = {"name": name, "loaded": loaded}

    @_safe
    def vision_result(self, cam, wall, label, conf, frame=None, fake_conf=0.0):
        """Risultato di una scansione YOLO. `frame` (opzionale) è l'immagine annotata."""
        if frame is not None:
            ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
            if ok:
                with self._lock:
                    self._snap_seq += 1
                    self._snapshots[cam] = (buf.tobytes(), self._snap_seq)
        with self._lock:
            self.state["vision"]["scans"][cam] = {
                "t": _now(), "wall": wall, "label": label,
                "conf": round(conf, 3) if conf else None,
                "fake_conf": round(fake_conf, 3) if fake_conf else None,
                "snap": self._snapshots.get(cam, (None, None))[1],
            }

    def snapshot_jpeg(self, cam):
        with self._lock:
            s = self._snapshots.get(cam)
        return s[0] if s else None

    # ------------------------------------------------------------------ navigazione
    @_safe
    def update_nav(self, robot, phase=None, last_cmd=None, cell_info=None):
        """Copia lo stato della mappa da un oggetto con x, y, orientation, map, visited (duck typing)."""
        cells = []
        for (x, y), c in list(robot.map.items()):
            cells.append({"x": x, "y": y, "w": list(c.walls), "c": c.color, "v": bool(c.visited)})
        nav = {
            "x": robot.x, "y": robot.y,
            "ori": robot.orientation, "ori_name": ORI_NAMES[robot.orientation % 4],
            "visited": len(robot.visited),
            "returning": bool(getattr(robot, "returning_home", False)),
            "path": [list(p) for p in getattr(robot, "dijkstra_path", [])],
            "blocked": [list(p) for p in getattr(robot, "no_transit", set())],
            "cells": cells,
            "t": _now(),
        }
        with self._lock:
            prev = self.state["nav"] or {}
            nav["phase"] = phase or prev.get("phase")
            nav["last_cmd"] = last_cmd or prev.get("last_cmd")
            nav["cell_info"] = cell_info or prev.get("cell_info")
            self.state["nav"] = nav

    @_safe
    def add_victim(self, x, y, wall, abs_dir, label, kits):
        """wall = parete relativa (F/R/L), abs_dir = direzione assoluta 0..3 (N/E/S/O)."""
        with self._lock:
            self.state["victims"].append({"x": x, "y": y, "wall": wall, "dir": abs_dir, "label": label,
                                          "kits": kits, "t": _now()})

    @_safe
    def reset_run(self):
        with self._lock:
            self.state["victims"] = []
            self.state["nav"] = None

    # ------------------------------------------------------------------ snapshot stato
    def _pi_loop(self):
        while True:
            try:
                st = pi_stats()
                with self._lock:
                    self.state["pi"] = st
            except Exception:
                pass
            time.sleep(2.0)

    def snapshot(self, log_cursor=None):
        now = _now()
        with self._lock:
            state = json.loads(json.dumps(self.state))   # copia profonda "sicura"
            times = list(self._tel_times)
            if log_cursor is None:
                logs = list(self._logs)
            else:
                logs = [l for l in self._logs if l["seq"] > log_cursor]
            cursor = self._log_seq
        last = state["arduino"]["last_seen"]
        state["arduino"]["age"] = round(now - last, 2) if last else None
        state["arduino"]["online"] = bool(last and now - last < ARDUINO_STALE_S)
        recent = [t for t in times if now - t < 2.0]
        state["serial"]["tel_hz"] = round(len(recent) / 2.0, 1)
        state["cameras"] = {n: c.info() for n, c in self.cameras.items()}
        state["server"] = {"t": now, "uptime_s": int(now - self.started_at), "mode": self.mode}
        return state, logs, cursor


# ============================================================================
#  Handler HTTP
# ============================================================================

class _Handler(BaseHTTPRequestHandler):
    dash: Dashboard = None
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):   # silenzia il log di ogni richiesta
        pass

    def _send(self, code, body, ctype="text/plain; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _authorized(self, qs):
        tok = self.dash.token
        return not tok or qs.get("token", [None])[0] == tok

    def do_GET(self):
        url = urlparse(self.path)
        qs = parse_qs(url.query)
        path = url.path.rstrip("/") or "/"
        try:
            if not self._authorized(qs):
                return self._send(401, "Token mancante o errato: aggiungi ?token=... all'indirizzo")
            if path in ("/", "/index.html"):
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    return self._send(200, f.read(), "text/html; charset=utf-8")
            if path == "/api/state":
                state, logs, _ = self.dash.snapshot()
                state["logs"] = logs[-100:]
                return self._send(200, json.dumps(state), "application/json")
            if path == "/events":
                return self._events()
            if path.startswith("/stream/"):
                return self._mjpeg(path.split("/")[2], qs)
            if path.startswith("/snapshot/"):
                jpg = self.dash.snapshot_jpeg(path.split("/")[2].replace(".jpg", ""))
                if jpg is None:
                    return self._send(404, "nessuna scansione ancora")
                return self._send(200, jpg, "image/jpeg")
            return self._send(404, "non trovato")
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def _events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        cursor = None
        while True:
            state, logs, cursor = self.dash.snapshot(cursor)
            state["logs"] = logs[-300:]
            self.wfile.write(b"data: " + json.dumps(state).encode() + b"\n\n")
            self.wfile.flush()
            time.sleep(0.2)

    def _mjpeg(self, name, qs):
        cam = self.dash.cameras.get(name)
        if cam is None or not cam.isOpened():
            return self._send(503, f"camera '{name}' non disponibile")
        try:
            max_fps = float(qs.get("fps", ["0"])[0])
        except ValueError:
            max_fps = 0
        boundary = "frame"
        self.send_response(200)
        self.send_header("Content-Type", f"multipart/x-mixed-replace; boundary={boundary}")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        cam.add_stream_client()
        try:
            seq, last_sent = -1, 0.0
            while True:
                jpg, seq = cam.wait_jpeg(seq, timeout=2.0)
                if jpg is None:
                    continue
                now = time.time()
                if max_fps > 0 and now - last_sent < 1.0 / max_fps:
                    continue
                last_sent = now
                self.wfile.write(
                    f"--{boundary}\r\nContent-Type: image/jpeg\r\nContent-Length: {len(jpg)}\r\n\r\n".encode()
                    + jpg + b"\r\n")
                self.wfile.flush()
        finally:
            cam.remove_stream_client()
