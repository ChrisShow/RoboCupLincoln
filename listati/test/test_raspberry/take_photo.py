from picamera2 import Picamera2
import time
picam2 = Picamera2() # Inizializza la camera
config = picam2.create_preview_configuration() # Crea e configura
picam2.configure(config)
picam2.start() # Avvia la camera
print("Camera avviata, attesa 2 secondi...")
time.sleep(2)
nome_foto = "foto_" + time.strftime("%Y%m%d_%H%M%S") + ".jpg" # Scatta la foto
picam2.capture_file(nome_foto)
print(f"Foto salvata come: {nome_foto}")
picam2.stop() # Ferma la camera