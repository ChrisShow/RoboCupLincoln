from picamera2 import Picamera2
import time
# Inizializza la camera
picam2 = Picamera2()
# Crea la configurazione per l'anteprima
config = picam2.create_preview_configuration()
picam2.configure(config)
# Avvia la camera
print("Anteprima camera in avvio...")
print("Premi Ctrl+C per fermare")
picam2.start_preview()
try:
    # Mantieni l'anteprima attiva
    time.sleep(300) # 5 minuti di anteprima
except KeyboardInterrupt:
    print("\nAnteprima interrotta dall'utente")
finally:
    picam2.stop_preview()
    print("Camera spenta")