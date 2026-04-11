import cv2
from ultralytics import YOLO
import time

print("🤖 Caricamento del modello in corso...")
model = YOLO("best.pt")

cap = cv2.VideoCapture(0)

if not cap.isOpened():
    print("❌ Errore: Impossibile accedere alla fotocamera.")
    exit()

print("📷 Fotocamera aperta! Scatto tra 3 secondi...")
time.sleep(3)

# Pulisci il buffer del video
for _ in range(5):
    cap.read()

# Scatto!
ret, frame = cap.read()
cap.release()

if not ret:
    print("❌ Errore: Impossibile catturare l'immagine.")
    exit()

print("📸 Foto scattata. Analisi e filtraggio in corso...")

# Inferenza
results = model(frame, imgsz=320)
annotated_frame = results[0].plot()

nomi_classi = results[0].names
box_rilevati = results[0].boxes

print("\n" + "="*30)
print("🎯 RISULTATO DELL'ANALISI 🎯")
print("="*30)

# --- INIZIO LOGICA DI FILTRAGGIO ---
target_prioritari = ["PHI", "OMEGA", "PSI"]
bersaglio_finale = None
massima_confidenza = 0.0
trovato_prioritario = False

# Fase 1: Cerchiamo i target ad alta priorità (PHI, OMEGA, PSI)
for box in box_rilevati:
    nome_simbolo = nomi_classi[int(box.cls[0])]
    confidenza = float(box.conf[0]) * 100
    
    if nome_simbolo in target_prioritari:
        trovato_prioritario = True
        # Se inquadriamo più target VIP, teniamo quello con la confidenza maggiore
        if confidenza > massima_confidenza:
            bersaglio_finale = nome_simbolo
            massima_confidenza = confidenza

# Fase 2: Se NON abbiamo trovato target prioritari, cerchiamo il COGNITIVE
if not trovato_prioritario:
    for box in box_rilevati:
        nome_simbolo = nomi_classi[int(box.cls[0])]
        confidenza = float(box.conf[0]) * 100
        
        # Accettiamo solo COGNITIVE, ignorando i FAKE
        if nome_simbolo == "COGNITIVE":
            if confidenza > massima_confidenza:
                bersaglio_finale = nome_simbolo
                massima_confidenza = confidenza
# --- FINE LOGICA DI FILTRAGGIO ---

# Stampiamo l'unico risultato che ci interessa
if bersaglio_finale:
    print(f"✅ BERSAGLIO SCELTO: {bersaglio_finale} (Sicurezza: {massima_confidenza:.1f}%)")
else:
    print("Nessun bersaglio valido rilevato. 🤷‍♂️")

print("="*30 + "\n")

# Salva l'immagine con tutti i box rilevati per poter fare debug
nome_file = "ultimo_rilevamento.jpg"
cv2.imwrite(nome_file, annotated_frame)
print(f"💾 Immagine salvata come '{nome_file}'.")