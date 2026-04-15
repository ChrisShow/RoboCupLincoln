import serial
import time

SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE   = 115200

def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=3)
    except Exception as e:
        print(f"[ERRORE] Impossibile aprire {SERIAL_PORT}: {e}")
        return

    time.sleep(2)  # Attendi reset Arduino dopo apertura seriale
    ser.reset_input_buffer()
    print("=== Echo Test RPi <-> Arduino ===")
    print("Digita una parola (max 16 caratteri). Scrivi FINE per uscire.\n")

    while True:
        parola = input("Inserisci parola: ").strip()

        if len(parola) > 16:
            print(f"[ERRORE] Troppo lunga ({len(parola)} caratteri). Max 16.")
            continue

        if parola == "FINE":
            ser.write(b"<msg:FINE>\n")
            print("Uscita. Ciao!")
            break

        if not parola:
            print("[ERRORE] Parola vuota, riprova.")
            continue

        # Invia pacchetto ad Arduino
        pacchetto = f"<msg:{parola}>\n"
        ser.write(pacchetto.encode())
        print(f"TX -> {pacchetto.strip()}")

        # Attendi l'echo da Arduino
        risposta = ser.readline().decode('utf-8', errors='ignore').strip()
        if risposta:
            # Estrai la parola dal pacchetto <msg:...>
            if risposta.startswith("<msg:") and risposta.endswith(">"):
                echo = risposta[5:-1]
                print(f"RX <- {risposta}  =>  ECHO: \"{echo}\"\n")
            else:
                print(f"RX <- (risposta inattesa) {risposta}\n")
        else:
            print("[TIMEOUT] Nessuna risposta da Arduino entro 3 secondi.\n")

    ser.close()

if __name__ == "__main__":
    main()
