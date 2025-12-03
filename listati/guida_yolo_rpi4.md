# Guida Completa: YOLO + PiCamera su Raspberry Pi 4

Una guida passo-passo per configurare e implementare un programma Python che rileva colori specifici nelle foto usando YOLO di Ultralytics su Raspberry Pi 4.

---

## Indice
1. [Preparazione del Sistema](#preparazione-del-sistema)
2. [Installazione di Python 3 e Dipendenze](#installazione-di-python-3-e-dipendenze)
3. [Installazione di YOLO Ultralytics](#installazione-di-yolo-ultralytics)
4. [Configurazione della PiCamera](#configurazione-della-picamera)
5. [Implementazione del Programma](#implementazione-del-programma)
6. [Rilevamento dei Colori con YOLO](#rilevamento-dei-colori-con-yolo)
7. [Testing e Ottimizzazione](#testing-e-ottimizzazione)
8. [Troubleshooting](#troubleshooting)

---

## 1. Preparazione del Sistema

### 1.1 Aggiornamento del Sistema
Collegati al tuo Raspberry Pi 4 tramite SSH o terminale locale e esegui:

```bash
sudo apt update
sudo apt upgrade -y
sudo apt autoremove -y
```

### 1.2 Verifica della Versione di Raspbian
Assicurati di avere una versione recente di Raspberry Pi OS:

```bash
cat /etc/os-release
# Dovrebbe essere Bookworm (2024+) o Bullseye
```

### 1.3 Abilita la PiCamera nell'interfaccia di configurazione
```bash
sudo raspi-config
```

Naviga a:
- **3 Interface Options**
- **I1 Legacy Camera** (o **I3 Camera** in versioni più recenti)
- Seleziona **Yes** per abilitare
- Riavvia il sistema

Verifica che la camera sia riconosciuta:
```bash
vcgencmd get_camera
# Output atteso: supported=1 detected=1
```

---

## 2. Installazione di Python 3 e Dipendenze

### 2.1 Controlla la versione di Python 3
```bash
python3 --version
```

Se non è installato o è una versione molto vecchia (< 3.8), installa:
```bash
sudo apt install python3 python3-pip python3-dev -y
```

### 2.2 Installa i Pacchetti di Base Necessari
```bash
sudo apt install git wget curl build-essential -y
```

### 2.3 Installa le Librerie per l'Imaging
```bash
sudo apt install libatlas-base-dev libjasper-dev libtiff5 libjasper1 -y
sudo apt install libharfbuzz0b libwebp6 libtiff5 libjasper1 -y
```

### 2.4 Installa pip e Virtual Environment
```bash
sudo apt install python3-pip python3-venv -y
```

### 2.5 Crea una Cartella di Progetto
```bash
mkdir ~/yolo_project
cd ~/yolo_project
python3 -m venv venv
source venv/bin/activate
```

### 2.6 Aggiorna pip e setuptools
```bash
pip install --upgrade pip setuptools wheel
```

---

## 3. Installazione di YOLO Ultralytics

### 3.1 Installa YOLO di Ultralytics
Con l'ambiente virtuale attivato:

```bash
pip install ultralytics
```

**Nota:** L'installazione potrebbe richiedere alcuni minuti su Raspberry Pi 4 a causa della compilazione di dipendenze come NumPy.

### 3.2 Verifica l'Installazione
```python
python3 << 'EOF'
from ultralytics import YOLO
print("YOLO Ultralytics installato correttamente!")
print(YOLO.__doc__)
EOF
```

### 3.3 Installa Dipendenze Aggiuntive Consigliate
```bash
pip install opencv-python pillow numpy matplotlib
```

**opencv-python**: Per l'elaborazione di immagini e video
**pillow**: Per operazioni su immagini
**numpy**: Calcoli numerici
**matplotlib**: Visualizzazione dei risultati

### 3.4 Installa picamera2 (Libreria Moderna per PiCamera)
```bash
sudo apt install -y python3-picamera2 python3-libcamera python3-kms++ libcamera-tools libcamera-dev
```

---

## 4. Configurazione della PiCamera

### 4.1 Test Basico della Camera
```bash
libcamera-hello --timeout 5000
```

Dovresti vedere un'anteprima della camera per 5 secondi.

### 4.2 Scatta una Foto di Test
```bash
libcamera-still -o test_photo.jpg
```

### 4.3 Test con picamera2 in Python
Crea il file `test_camera.py`:

```python
#!/usr/bin/env python3
from picamera2 import Picamera2
from PIL import Image
import time

# Inizializza la camera
picam2 = Picamera2()

# Configura la risoluzione (consigliato per Raspberry Pi 4: 1920x1080 o 1280x720)
config = picam2.create_still_configuration(main={"size": (1280, 720)})
picam2.configure(config)
picam2.start()

# Attendi che la camera sia pronta
time.sleep(2)

# Scatta una foto
picam2.capture_file("test_camera_output.jpg")
print("Foto scattata: test_camera_output.jpg")

picam2.stop()
```

Esegui il test:
```bash
python3 test_camera.py
```

---

## 5. Implementazione del Programma

### 5.1 Struttura del Progetto
```
~/yolo_project/
├── venv/
├── images/                    # Cartella per le foto
├── results/                   # Cartella per i risultati
├── main.py                    # Script principale
├── color_detector.py          # Modulo di rilevamento colori
├── config.py                  # Configurazioni
└── requirements.txt           # Dipendenze
```

Crea le cartelle:
```bash
cd ~/yolo_project
mkdir images results
```

### 5.2 File: config.py (Configurazioni)

```python
"""
Configurazioni per il progetto YOLO + PiCamera
"""

# Parametri Camera
CAMERA_RESOLUTION = (1280, 720)
CAMERA_ROTATION = 0  # 0, 90, 180, 270 gradi
PHOTO_QUALITY = 100

# Parametri YOLO
YOLO_MODEL = "yolo11n.pt"  # Modello nano (più leggero per RPi)
# Altre opzioni: "yolo11s.pt" (small), "yolo11m.pt" (medium)
CONFIDENCE_THRESHOLD = 0.45

# Colori da Rilevare (formato BGR per OpenCV)
COLORS_TO_DETECT = {
    "rosso": {
        "lower_bgr": [0, 0, 100],
        "upper_bgr": [50, 50, 255],
        "lower_hsv": [0, 100, 100],      # Formato HSV (migliore per colori)
        "upper_hsv": [10, 255, 255]
    },
    "verde": {
        "lower_hsv": [40, 40, 40],
        "upper_hsv": [80, 255, 255]
    },
    "blu": {
        "lower_hsv": [100, 100, 100],
        "upper_hsv": [130, 255, 255]
    },
    "giallo": {
        "lower_hsv": [20, 100, 100],
        "upper_hsv": [40, 255, 255]
    },
    "arancione": {
        "lower_hsv": [10, 100, 100],
        "upper_hsv": [25, 255, 255]
    }
}

# Percorsi
IMAGE_DIR = "images"
RESULTS_DIR = "results"

# Logging
LOG_LEVEL = "INFO"  # DEBUG, INFO, WARNING, ERROR
```

### 5.3 File: color_detector.py (Modulo Rilevamento Colori)

```python
"""
Modulo per il rilevamento di colori specifici usando YOLO e OpenCV
"""

import cv2
import numpy as np
from ultralytics import YOLO
from datetime import datetime
import os
import logging

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class ColorDetector:
    """
    Classe per rilevare colori specifici nelle immagini usando YOLO
    """
    
    def __init__(self, model_path="yolo11n.pt", confidence=0.45):
        """
        Inizializza il rilevatore di colori
        
        Args:
            model_path: Percorso al modello YOLO
            confidence: Soglia di confidenza per il rilevamento
        """
        self.model = YOLO(model_path)
        self.confidence = confidence
        logger.info(f"Modello YOLO caricato: {model_path}")
    
    def detect_color_in_image(self, image_path, color_ranges):
        """
        Rileva colori specifici in un'immagine
        
        Args:
            image_path: Percorso dell'immagine
            color_ranges: Dizionario con i range di colori HSV da rilevare
        
        Returns:
            Dizionario con i risultati del rilevamento
        """
        image = cv2.imread(image_path)
        
        if image is None:
            logger.error(f"Errore nel caricare l'immagine: {image_path}")
            return None
        
        height, width = image.shape[:2]
        logger.info(f"Immagine caricata: {width}x{height} px")
        
        # Converti da BGR a HSV (migliore per rilevamento colori)
        hsv_image = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
        
        results = {
            "image_path": image_path,
            "timestamp": datetime.now().isoformat(),
            "image_size": (width, height),
            "colors_detected": {}
        }
        
        # Rileva ogni colore
        for color_name, color_range in color_ranges.items():
            lower = np.array(color_range["lower_hsv"])
            upper = np.array(color_range["upper_hsv"])
            
            # Crea una maschera per il colore
            mask = cv2.inRange(hsv_image, lower, upper)
            
            # Conta i pixel del colore
            pixel_count = cv2.countNonZero(mask)
            percentage = (pixel_count / (width * height)) * 100
            
            results["colors_detected"][color_name] = {
                "pixel_count": int(pixel_count),
                "percentage": round(percentage, 2),
                "detected": pixel_count > 0
            }
            
            logger.info(f"  {color_name}: {pixel_count} px ({percentage:.2f}%)")
        
        return results
    
    def detect_objects_yolo(self, image_path):
        """
        Rileva oggetti nell'immagine usando YOLO
        
        Args:
            image_path: Percorso dell'immagine
        
        Returns:
            Risultati della predizione YOLO
        """
        logger.info(f"Esecuzione YOLO su: {image_path}")
        
        results = self.model.predict(
            source=image_path,
            conf=self.confidence,
            verbose=False
        )
        
        return results[0] if results else None
    
    def analyze_image_complete(self, image_path, color_ranges):
        """
        Analisi completa: YOLO + Rilevamento colori
        
        Args:
            image_path: Percorso dell'immagine
            color_ranges: Range di colori da rilevare
        
        Returns:
            Dizionario con risultati completi
        """
        logger.info(f"Analisi completa immagine: {image_path}")
        
        # Rilevamento colori
        color_results = self.detect_color_in_image(image_path, color_ranges)
        
        # Rilevamento oggetti YOLO
        yolo_results = self.detect_objects_yolo(image_path)
        
        analysis = {
            "color_detection": color_results,
            "yolo_detection": {
                "objects_detected": 0,
                "details": []
            }
        }
        
        if yolo_results:
            # Estrai informazioni da YOLO
            boxes = yolo_results.boxes
            analysis["yolo_detection"]["objects_detected"] = len(boxes)
            
            for idx, box in enumerate(boxes):
                obj_info = {
                    "id": idx,
                    "confidence": float(box.conf[0]),
                    "class": int(box.cls[0]),
                    "class_name": yolo_results.names[int(box.cls[0])],
                    "bbox": {
                        "x1": float(box.xyxy[0][0]),
                        "y1": float(box.xyxy[0][1]),
                        "x2": float(box.xyxy[0][2]),
                        "y2": float(box.xyxy[0][3])
                    }
                }
                analysis["yolo_detection"]["details"].append(obj_info)
                
                logger.info(f"  Oggetto {idx}: {obj_info['class_name']} "
                           f"(confidenza: {obj_info['confidence']:.2f})")
        
        return analysis
    
    def save_annotated_image(self, image_path, yolo_results, output_path):
        """
        Salva l'immagine con annotazioni YOLO
        
        Args:
            image_path: Percorso dell'immagine originale
            yolo_results: Risultati della predizione YOLO
            output_path: Percorso dove salvare l'immagine annotata
        """
        if yolo_results is None:
            logger.warning("Nessun risultato YOLO disponibile")
            return
        
        annotated_frame = yolo_results.plot()
        cv2.imwrite(output_path, annotated_frame)
        logger.info(f"Immagine annotata salvata: {output_path}")


def main_example():
    """
    Esempio di utilizzo del ColorDetector
    """
    from config import YOLO_MODEL, COLORS_TO_DETECT, CONFIDENCE_THRESHOLD
    
    # Inizializza il rilevatore
    detector = ColorDetector(YOLO_MODEL, CONFIDENCE_THRESHOLD)
    
    # Analizza un'immagine
    image_path = "images/test_photo.jpg"
    if os.path.exists(image_path):
        results = detector.analyze_image_complete(image_path, COLORS_TO_DETECT)
        
        print("\n=== RISULTATI ANALISI ===")
        print(f"File: {results['color_detection']['image_path']}")
        print(f"Timestamp: {results['color_detection']['timestamp']}")
        print(f"Risoluzione: {results['color_detection']['image_size']}")
        
        print("\nColori rilevati:")
        for color, data in results['color_detection']['colors_detected'].items():
            print(f"  {color}: {data['pixel_count']} px ({data['percentage']}%)")
        
        print(f"\nOggetti YOLO rilevati: {results['yolo_detection']['objects_detected']}")
        for obj in results['yolo_detection']['details']:
            print(f"  - {obj['class_name']} (confidenza: {obj['confidence']:.2f})")
    else:
        logger.error(f"File non trovato: {image_path}")


if __name__ == "__main__":
    main_example()
```

### 5.4 File: main.py (Script Principale)

```python
#!/usr/bin/env python3
"""
Programma principale: Fotocamera con Rilevamento Colori usando YOLO
per Raspberry Pi 4 con PiCamera
"""

import os
import sys
import logging
import json
from datetime import datetime
from pathlib import Path

# Importa le dipendenze
try:
    from picamera2 import Picamera2
except ImportError:
    print("ERRORE: picamera2 non installato. Installa con:")
    print("  sudo apt install python3-picamera2")
    sys.exit(1)

try:
    import cv2
    import numpy as np
except ImportError:
    print("ERRORE: opencv-python o numpy non installati")
    sys.exit(1)

try:
    from ultralytics import YOLO
except ImportError:
    print("ERRORE: ultralytics non installato. Installa con:")
    print("  pip install ultralytics")
    sys.exit(1)

# Importa moduli del progetto
from config import (
    CAMERA_RESOLUTION, CAMERA_ROTATION, YOLO_MODEL,
    COLORS_TO_DETECT, CONFIDENCE_THRESHOLD, IMAGE_DIR, RESULTS_DIR, LOG_LEVEL
)
from color_detector import ColorDetector

# Configurazione logging
logging.basicConfig(
    level=LOG_LEVEL,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class PiCameraYOLOApp:
    """
    Applicazione principale: PiCamera + YOLO
    """
    
    def __init__(self):
        """Inizializza l'applicazione"""
        logger.info("Inizializzazione applicazione...")
        
        # Crea le cartelle necessarie
        Path(IMAGE_DIR).mkdir(exist_ok=True)
        Path(RESULTS_DIR).mkdir(exist_ok=True)
        
        # Inizializza la camera
        self.picam2 = None
        self._init_camera()
        
        # Inizializza il rilevatore di colori
        self.detector = ColorDetector(YOLO_MODEL, CONFIDENCE_THRESHOLD)
        
        logger.info("Applicazione inizializzata correttamente")
    
    def _init_camera(self):
        """Inizializza la PiCamera"""
        try:
            logger.info("Inizializzazione PiCamera...")
            self.picam2 = Picamera2()
            
            # Configura la camera
            config = self.picam2.create_still_configuration(
                main={"size": CAMERA_RESOLUTION}
            )
            self.picam2.configure(config)
            self.picam2.start()
            
            logger.info(f"Camera inizializzata: {CAMERA_RESOLUTION}")
            logger.info("Attendere 2 secondi per la stabilizzazione...")
            
            import time
            time.sleep(2)
            
        except Exception as e:
            logger.error(f"Errore nell'inizializzazione della camera: {e}")
            raise
    
    def capture_photo(self, custom_name=None):
        """
        Scatta una foto
        
        Args:
            custom_name: Nome personalizzato per la foto (opzionale)
        
        Returns:
            Percorso del file salvato
        """
        try:
            if custom_name:
                filename = f"{custom_name}.jpg"
            else:
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                filename = f"photo_{timestamp}.jpg"
            
            filepath = os.path.join(IMAGE_DIR, filename)
            
            logger.info(f"Scatto foto: {filename}")
            self.picam2.capture_file(filepath)
            logger.info(f"Foto salvata: {filepath}")
            
            return filepath
            
        except Exception as e:
            logger.error(f"Errore nello scatto della foto: {e}")
            return None
    
    def analyze_photo(self, image_path):
        """
        Analizza una foto
        
        Args:
            image_path: Percorso della foto
        
        Returns:
            Risultati dell'analisi
        """
        if not os.path.exists(image_path):
            logger.error(f"File non trovato: {image_path}")
            return None
        
        logger.info(f"Analisi immagine: {image_path}")
        
        # Esegui analisi completa
        results = self.detector.analyze_image_complete(image_path, COLORS_TO_DETECT)
        
        return results
    
    def save_results(self, photo_path, analysis_results):
        """
        Salva i risultati dell'analisi
        
        Args:
            photo_path: Percorso della foto
            analysis_results: Risultati dell'analisi
        """
        if analysis_results is None:
            logger.warning("Nessun risultato da salvare")
            return None
        
        try:
            # Crea nome del file JSON
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            result_file = os.path.join(RESULTS_DIR, f"analysis_{timestamp}.json")
            
            # Aggiungi percorso foto ai risultati
            analysis_results['photo_path'] = photo_path
            
            # Salva in JSON
            with open(result_file, 'w', encoding='utf-8') as f:
                json.dump(analysis_results, f, indent=2, ensure_ascii=False)
            
            logger.info(f"Risultati salvati: {result_file}")
            return result_file
            
        except Exception as e:
            logger.error(f"Errore nel salvataggio dei risultati: {e}")
            return None
    
    def save_annotated_image(self, image_path):
        """
        Salva l'immagine con annotazioni YOLO
        
        Args:
            image_path: Percorso della foto
        
        Returns:
            Percorso dell'immagine annotata
        """
        try:
            yolo_results = self.detector.detect_objects_yolo(image_path)
            
            if yolo_results is None:
                logger.warning("Nessun risultato YOLO")
                return None
            
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_name = f"annotated_{timestamp}.jpg"
            output_path = os.path.join(RESULTS_DIR, output_name)
            
            self.detector.save_annotated_image(image_path, yolo_results, output_path)
            
            return output_path
            
        except Exception as e:
            logger.error(f"Errore nel salvataggio immagine annotata: {e}")
            return None
    
    def run_continuous_monitoring(self, interval_seconds=5, max_iterations=None):
        """
        Esegui monitoraggio continuo
        
        Args:
            interval_seconds: Intervallo tra gli scatti (secondi)
            max_iterations: Numero massimo di iterazioni (None = infinito)
        """
        import time
        
        logger.info(f"Inizio monitoraggio continuo (intervallo: {interval_seconds}s)")
        
        iteration = 0
        try:
            while max_iterations is None or iteration < max_iterations:
                logger.info(f"Iterazione {iteration + 1}")
                
                # Scatta foto
                photo_path = self.capture_photo()
                
                if photo_path:
                    # Analizza foto
                    results = self.analyze_photo(photo_path)
                    
                    if results:
                        # Salva risultati
                        self.save_results(photo_path, results)
                        
                        # Salva immagine annotata
                        self.save_annotated_image(photo_path)
                        
                        # Stampa riepilogo
                        self._print_summary(results)
                
                iteration += 1
                
                if max_iterations is None or iteration < max_iterations:
                    logger.info(f"Attesa {interval_seconds} secondi...")
                    time.sleep(interval_seconds)
        
        except KeyboardInterrupt:
            logger.info("Monitoraggio interrotto dall'utente")
        except Exception as e:
            logger.error(f"Errore nel monitoraggio: {e}")
        finally:
            self.cleanup()
    
    def _print_summary(self, results):
        """Stampa un riepilogo dei risultati"""
        print("\n" + "="*50)
        print("RIEPILOGO ANALISI")
        print("="*50)
        
        # Colori rilevati
        print("\nColori rilevati:")
        for color, data in results['color_detection']['colors_detected'].items():
            status = "✓" if data['detected'] else "✗"
            print(f"  {status} {color}: {data['pixel_count']} px ({data['percentage']}%)")
        
        # Oggetti YOLO
        print(f"\nOggetti YOLO rilevati: {results['yolo_detection']['objects_detected']}")
        for obj in results['yolo_detection']['details']:
            print(f"  - {obj['class_name']} ({obj['confidence']:.2f})")
        
        print("="*50 + "\n")
    
    def cleanup(self):
        """Pulizia e chiusura"""
        logger.info("Chiusura applicazione...")
        if self.picam2:
            self.picam2.stop()
        logger.info("Applicazione chiusa")


def main():
    """Funzione principale"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Applicazione PiCamera + YOLO per Rilevamento Colori"
    )
    parser.add_argument(
        "--mode",
        choices=["single", "continuous"],
        default="single",
        help="Modalità operativa"
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=5,
        help="Intervallo tra gli scatti in modalità continua (secondi)"
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=None,
        help="Numero di iterazioni in modalità continua (default: infinito)"
    )
    parser.add_argument(
        "--name",
        type=str,
        default=None,
        help="Nome personalizzato per la foto in modalità single"
    )
    
    args = parser.parse_args()
    
    # Inizializza app
    app = PiCameraYOLOApp()
    
    try:
        if args.mode == "single":
            logger.info("Modalità: Scatto singolo")
            photo_path = app.capture_photo(args.name)
            
            if photo_path:
                results = app.analyze_photo(photo_path)
                if results:
                    app.save_results(photo_path, results)
                    app.save_annotated_image(photo_path)
                    app._print_summary(results)
        
        elif args.mode == "continuous":
            logger.info("Modalità: Monitoraggio continuo")
            app.run_continuous_monitoring(
                interval_seconds=args.interval,
                max_iterations=args.iterations
            )
    
    finally:
        app.cleanup()


if __name__ == "__main__":
    main()
```

### 5.5 File: requirements.txt

```
ultralytics>=8.0.0
opencv-python
pillow>=8.0.0
numpy
matplotlib
pyyaml
```

---

## 6. Rilevamento dei Colori con YOLO

### 6.1 Comprensione dello Spazio di Colore HSV

Il rilevamento di colori funziona meglio con HSV anziché BGR perché:
- **H (Hue)**: Il colore vero (0-180 in OpenCV)
- **S (Saturation)**: L'intensità del colore (0-255)
- **V (Value)**: La luminosità (0-255)

Intervalli HSV comuni:
```
Rosso:     H: 0-10 o 170-180
Verde:     H: 40-80
Blu:       H: 100-130
Giallo:    H: 20-40
Arancione: H: 10-25
Ciano:     H: 85-95
Magenta:   H: 130-170
```

### 6.2 Personalizzazione dei Range di Colori

Nel file `config.py`, modifica `COLORS_TO_DETECT`:

```python
COLORS_TO_DETECT = {
    "rosso_scuro": {
        "lower_hsv": [0, 50, 50],
        "upper_hsv": [10, 255, 255]
    },
    "verde_chiaro": {
        "lower_hsv": [40, 40, 40],
        "upper_hsv": [90, 255, 255]
    },
    # ... aggiungi altri colori
}
```

### 6.3 Integrazione YOLO per Rilevamento Avanzato

Se vuoi rilevare **oggetti di un colore specifico** (es. "macchine rosse"):
1. Allena un modello YOLO custom con dataset di macchine rosse
2. Oppure usa YOLO per rilevare le macchine, poi analizza il colore dentro i bounding box

---

## 7. Testing e Ottimizzazione

### 7.1 Installazione delle Dipendenze

Con l'ambiente virtuale attivato:

```bash
cd ~/yolo_project
pip install -r requirements.txt
```

### 7.2 Test Componente per Componente

**Test 1: Camera**
```bash
python3 test_camera.py
```

**Test 2: YOLO**
```python
python3 << 'EOF'
from ultralytics import YOLO
model = YOLO("yolo11n.pt")
results = model("images/test_photo.jpg")
print(f"Oggetti rilevati: {len(results[0].boxes)}")
EOF
```

**Test 3: Rilevamento Colori**
```bash
python3 -c "
from color_detector import ColorDetector
from config import COLORS_TO_DETECT, YOLO_MODEL
detector = ColorDetector(YOLO_MODEL)
results = detector.detect_color_in_image('images/test_photo.jpg', COLORS_TO_DETECT)
print(results)
"
```

### 7.3 Esecuzione Scatto Singolo

```bash
python3 main.py --mode single --name primo_test
```

Controlla i file generati in `images/` e `results/`.

### 7.4 Esecuzione Monitoraggio Continuo

```bash
# 5 scatti ogni 10 secondi
python3 main.py --mode continuous --interval 10 --iterations 5

# Monitoraggio infinito (Ctrl+C per fermare)
python3 main.py --mode continuous --interval 5
```

### 7.5 Ottimizzazioni per Raspberry Pi 4

**Ridurre il Carico Computazionale:**

1. **Usa modello più leggero**:
   ```python
   YOLO_MODEL = "yolo11n.pt"  # Nano (più veloce)
   # Invece di "yolo11m.pt" (medium)
   ```

2. **Riduci la risoluzione della camera**:
   ```python
   CAMERA_RESOLUTION = (640, 480)  # Anziché (1280, 720)
   ```

3. **Aumenta gli intervalli**:
   ```bash
   python3 main.py --mode continuous --interval 10
   ```

4. **Esporta YOLO in formato TensorRT** (per inferenza più veloce):
   ```python
   model = YOLO("yolo11n.pt")
   model.export(format="engine", device=0)
   ```

### 7.6 Monitoraggio Prestazioni

Aggiungi timing al codice:

```python
import time

start = time.time()
results = detector.analyze_image_complete(image_path, COLORS_TO_DETECT)
elapsed = time.time() - start

print(f"Tempo di analisi: {elapsed:.2f}s")
```

---

## 8. Troubleshooting

### 8.1 "ModuleNotFoundError: No module named 'picamera2'"

```bash
# Assicurati di essere nel venv
source venv/bin/activate

# Installa picamera2
sudo apt install python3-picamera2
```

### 8.2 "YOLO Model not found"

```bash
# Il modello verrà scaricato automaticamente al primo utilizzo
# Se manca spazio, libera memoria:
sudo apt clean
sudo apt autoremove
```

### 8.3 "Camera not initialized" o "libcamera: ERROR"

```bash
# Verifica che la camera sia abilitata
raspi-config

# Testa la camera
libcamera-hello --timeout 5000

# Se problemi di permessi:
sudo usermod -a -G video $USER
```

### 8.4 Basse Prestazioni / Timeout

- **Riduci risoluzione**: 640x480 anziché 1280x720
- **Usa modello nano**: `yolo11n.pt` anziché altri
- **Aumenta intervallo**: 10-15 secondi anziché 5
- **Disabilita visualizzazione**: Non usare `show=True` in `model.predict()`

### 8.5 "Out of Memory" Error

```bash
# Abilita swap su Raspberry Pi
sudo dphys-swapfile swapon

# Verifica memoria disponibile
free -h

# Interrompi altri servizi non necessari
sudo systemctl disable cups
sudo systemctl disable avahi-daemon
```

### 8.6 File JSON Corrotto

Se i file `results/*.json` sono vuoti o corrotti:

```bash
# Verifica i permessi sulla cartella
ls -la results/

# Ricrea la cartella se necessario
rm -rf results
mkdir results
```

---

## Checklista di Verifica Finale

- [ ] Raspberry Pi 4 aggiornato a Bookworm (OS più recente)
- [ ] PiCamera funzionante e riconosciuta
- [ ] Python 3.9+ installato
- [ ] Virtual environment creato e attivato
- [ ] YOLO Ultralytics installato
- [ ] Tutti i moduli Python importabili
- [ ] Cartelle `images/` e `results/` create
- [ ] File `config.py`, `color_detector.py`, `main.py` presenti
- [ ] Test camera eseguito con successo
- [ ] Test YOLO eseguito con successo
- [ ] Scatto singolo testato
- [ ] Risultati JSON e immagini annotate salvati correttamente

---

## Prossimi Passi Consigliati

1. **Custom Training**: Allena YOLO su dataset custom per rilevare specifici oggetti colorati
2. **Automazione**: Aggiungi esecuzione schedulata con `cron`
3. **WebServer**: Crea interfaccia web per controllare l'app
4. **Database**: Archivia risultati su database SQL
5. **Notifiche**: Aggiungi email/SMS quando colore specifico è rilevato

---

## Riferimenti Ufficiali

- YOLO Ultralytics: https://docs.ultralytics.com/it/usage/python/
- PiCamera2: https://www.raspberrypi.com/documentation/computers/camera_software.html
- Raspberry Pi: https://www.raspberrypi.com/documentation/

Buon lavoro! 🚀