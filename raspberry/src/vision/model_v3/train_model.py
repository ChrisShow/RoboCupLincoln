from ultralytics import YOLO

# Carica il modello nano pre-addestrato
modello = YOLO("yolov8n.pt")

# Avvia l'addestramento sul tuo dataset
risultati = modello.train(data="data.yaml", epochs=50, imgsz=640)