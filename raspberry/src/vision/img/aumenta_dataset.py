
import cv2
import os

def aumenta_dataset():
    # Percorso base dove si trovano le cartelle dei target
    base_path = "."
    target_folders = ["letterTargets", "cognitiveTargets"]
    categories = ["harmed", "unharmed", "stable", "fake"]

    print("Inizio aumento dataset (Data Augmentation)...")

    for target in target_folders:
        for cat in categories:
            folder_path = os.path.join(base_path, target, cat)
            
            if not os.path.exists(folder_path):
                print(f"Salto cartella non trovata: {folder_path}")
                continue

            print(f"Elaborazione cartella: {folder_path}")
            
            # Lista dei file originali (evitiamo di processare i nuovi file creati)
            files = [f for f in os.listdir(folder_path) if f.endswith(".jpg") and "_flip_" not in f]

            for filename in files:
                img_path = os.path.join(folder_path, filename)
                img = cv2.imread(img_path)

                if img is None:
                    continue

                # 1. Ribaltamento Orizzontale (Specchio)
                flip_h = cv2.flip(img, 1)
                name_h = filename.replace(".jpg", "_flip_h.jpg")
                cv2.imwrite(os.path.join(folder_path, name_h), flip_h)

                # 2. Ribaltamento Verticale
                flip_v = cv2.flip(img, 0)
                name_v = filename.replace(".jpg", "_flip_v.jpg")
                cv2.imwrite(os.path.join(folder_path, name_v), flip_v)

                # 3. Ribaltamento Diagonale (Entrambi gli assi)
                flip_d = cv2.flip(img, -1)
                name_d = filename.replace(".jpg", "_flip_d.jpg")
                cv2.imwrite(os.path.join(folder_path, name_d), flip_d)

    print("Aumento dataset completato con successo!")

if __name__ == "__main__":
    aumenta_dataset()
