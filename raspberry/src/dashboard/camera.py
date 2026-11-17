"""
Camere per la dashboard.

`SharedCamera` possiede la cv2.VideoCapture e la rende disponibile sia allo
stream MJPEG della dashboard sia, volendo, a codice che chiama .read() come
con una VideoCapture normale.

Un thread in background chiama continuamente cap.grab() (economico: non decodifica)
così il buffer della camera è sempre fresco. La decodifica (retrieve) avviene solo
quando qualcuno ha davvero bisogno di un frame: il programma di gara, oppure un
browser collegato allo stream (limitato a `stream_fps`).
"""

import threading
import time

import cv2
import numpy as np


class _StreamSource:
    """Parte comune: pubblica JPEG per lo stream e conta i client collegati."""

    def __init__(self, name, stream_fps=10, jpeg_quality=70):
        self.name = name
        self.stream_fps = stream_fps
        self.jpeg_quality = jpeg_quality
        self._jpeg = None
        self._jpeg_seq = 0
        self._jpeg_cond = threading.Condition()
        self._clients = 0
        self._clients_lock = threading.Lock()
        self._last_stream_t = 0.0
        self._fps = 0.0
        self._fps_count = 0
        self._fps_t0 = time.time()

    # --- client dello stream -------------------------------------------------
    def add_stream_client(self):
        with self._clients_lock:
            self._clients += 1

    def remove_stream_client(self):
        with self._clients_lock:
            self._clients = max(0, self._clients - 1)

    @property
    def stream_clients(self):
        return self._clients

    def _stream_due(self, now):
        return self._clients > 0 and (now - self._last_stream_t) >= 1.0 / self.stream_fps

    def _publish_jpeg(self, frame, now):
        ok, buf = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, self.jpeg_quality])
        if not ok:
            return
        self._last_stream_t = now
        with self._jpeg_cond:
            self._jpeg = buf.tobytes()
            self._jpeg_seq += 1
            self._jpeg_cond.notify_all()

    def wait_jpeg(self, last_seq, timeout=2.0):
        """Attende un JPEG più nuovo di last_seq. Ritorna (bytes|None, seq)."""
        with self._jpeg_cond:
            self._jpeg_cond.wait_for(lambda: self._jpeg_seq != last_seq, timeout=timeout)
            if self._jpeg_seq == last_seq:
                return None, last_seq
            return self._jpeg, self._jpeg_seq

    def _count_frame(self, now):
        self._fps_count += 1
        dt = now - self._fps_t0
        if dt >= 1.0:
            self._fps = self._fps_count / dt
            self._fps_count = 0
            self._fps_t0 = now


class SharedCamera(_StreamSource):
    """
    Sostituto drop-in di cv2.VideoCapture.

    read() restituisce sempre un frame acquisito DOPO la chiamata (mai un frame
    vecchio rimasto nel buffer), quindi la semantica di "scatto fresco" del
    codice chiamante è preservata.
    """

    REOPEN_AFTER_FAILS = 50

    def __init__(self, index, name, width=640, height=480, fourcc='MJPG',
                 stream_fps=10, jpeg_quality=70):
        super().__init__(name, stream_fps, jpeg_quality)
        self.index = index
        self.width = width
        self.height = height
        self.fourcc = fourcc
        self._cap_lock = threading.Lock()
        self._frame = None
        self._frame_seq = 0
        self._frame_cond = threading.Condition()
        self._waiters = 0
        self._fails = 0
        self._errors = 0
        self._running = True
        self.cap = None
        self._open()
        self._thread = threading.Thread(target=self._loop, name=f"cam-{name}", daemon=True)
        self._thread.start()

    # --- apertura / configurazione --------------------------------------------
    def _open(self):
        cap = cv2.VideoCapture(self.index)
        if cap.isOpened():
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*self.fourcc))
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        with self._cap_lock:
            if self.cap is not None:
                self.cap.release()
            self.cap = cap

    def isOpened(self):
        with self._cap_lock:
            return self.cap is not None and self.cap.isOpened()

    def set(self, prop, value):
        with self._cap_lock:
            return self.cap.set(prop, value) if self.cap is not None else False

    def get(self, prop):
        with self._cap_lock:
            return self.cap.get(prop) if self.cap is not None else 0

    def release(self):
        self._running = False
        with self._cap_lock:
            if self.cap is not None:
                self.cap.release()

    # --- thread di acquisizione ---------------------------------------------
    def _loop(self):
        while self._running:
            if not self.isOpened():
                time.sleep(1.0)
                self._open()      # la camera potrebbe essere stata ricollegata
                continue

            with self._cap_lock:
                ok = self.cap.grab()
            if not ok:
                self._fails += 1
                self._errors += 1
                if self._fails >= self.REOPEN_AFTER_FAILS:
                    self._fails = 0
                    self._open()
                time.sleep(0.02)
                continue
            self._fails = 0

            now = time.time()
            self._count_frame(now)
            need_read = self._waiters > 0
            need_stream = self._stream_due(now)
            if not (need_read or need_stream):
                continue

            with self._cap_lock:
                ok, frame = self.cap.retrieve()
            if not ok or frame is None:
                self._errors += 1
                continue

            with self._frame_cond:
                self._frame = frame
                self._frame_seq += 1
                self._frame_cond.notify_all()
            if need_stream:
                self._publish_jpeg(frame, now)

    # --- API compatibile con cv2.VideoCapture ----------------------------------
    def read(self, timeout=1.0):
        if not self.isOpened():
            return False, None
        with self._frame_cond:
            start_seq = self._frame_seq
            self._waiters += 1
            try:
                got = self._frame_cond.wait_for(lambda: self._frame_seq != start_seq,
                                                timeout=timeout)
                if not got:
                    return False, None
                return True, self._frame.copy()
            finally:
                self._waiters -= 1

    def info(self):
        return {
            "name": self.name,
            "index": self.index,
            "opened": self.isOpened(),
            "fps": round(self._fps, 1),
            "resolution": f"{self.width}x{self.height}",
            "errors": self._errors,
            "clients": self._clients,
        }


class FakeCamera(_StreamSource):
    """Camera finta per la modalità --demo (nessun hardware necessario)."""

    def __init__(self, name, width=640, height=480, stream_fps=10):
        super().__init__(name, stream_fps)
        self.width, self.height = width, height
        self._running = True
        threading.Thread(target=self._loop, name=f"fakecam-{name}", daemon=True).start()

    def _make_frame(self, t):
        h, w = self.height, self.width
        img = np.zeros((h, w, 3), np.uint8)
        img[:] = (40, 46, 52)
        # griglia "muro del labirinto"
        for x in range(0, w, 40):
            cv2.line(img, (x, 0), (x, h), (55, 62, 70), 1)
        for y in range(0, h, 40):
            cv2.line(img, (0, y), (w, y), (55, 62, 70), 1)
        cx = int(w / 2 + np.sin(t * 0.8 + (0 if self.name == 'sx' else 2)) * w * 0.25)
        cy = int(h / 2 + np.cos(t * 0.6) * h * 0.15)
        cv2.rectangle(img, (cx - 60, cy - 60), (cx + 60, cy + 60), (230, 230, 230), -1)
        letter = 'PHI' if self.name == 'sx' else 'PSI'
        cv2.putText(img, letter, (cx - 48, cy + 14), cv2.FONT_HERSHEY_SIMPLEX, 1.3, (30, 30, 30), 3)
        cv2.putText(img, f"DEMO cam {self.name.upper()}  {time.strftime('%H:%M:%S')}",
                    (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (120, 220, 160), 2)
        return img

    def _loop(self):
        while self._running:
            now = time.time()
            self._count_frame(now)
            if self._stream_due(now):
                self._publish_jpeg(self._make_frame(now), now)
            time.sleep(1 / 30)

    def isOpened(self):
        return True

    def read(self, timeout=1.0):
        return True, self._make_frame(time.time())

    def release(self):
        self._running = False

    def info(self):
        return {"name": self.name, "index": "demo", "opened": True,
                "fps": round(self._fps, 1), "resolution": f"{self.width}x{self.height}",
                "errors": 0, "clients": self._clients}
