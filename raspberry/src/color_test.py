import serial
import time

# Configurazione porta seriale
SERIAL_PORT = '/dev/ttyACM0' # TODO: Verificare porta
BAUD_RATE = 115200

def color_test_receiver():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        print(f"Ricevitore Test Colore avviato su {SERIAL_PORT}")
        print("In attesa di dati dall'Arduino... (Double Click START per uscire)")
    except Exception as e:
        print(f"ERRORE SERIALE: {e}")
        return

    while True:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if line == "<STOP>":
                print("\n[INFO] Segnale di stop ricevuto. Uscita...")
                break
            
            if line.startswith("<COLOR:"):
                # Estrae i valori R, G, B
                # Formato: <COLOR:R,G,B>
                data = line[7:-1].split(',')
                if len(data) == 3:
                    r, g, b = data
                    print(f"[{time.strftime('%H:%M:%S')}] Rilevazione: R={r}, G={g}, B={b}")
        
        time.sleep(0.01)

if __name__ == "__main__":
    color_test_receiver()
