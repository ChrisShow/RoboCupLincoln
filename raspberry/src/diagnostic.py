import serial
import time
import sys

# Configurazione Porta Seriale
# Modifica /dev/ttyACM0 con la tua porta (es: COM3 su Windows)
SERIAL_PORT = '/dev/ttyACM0' 
BAUD_RATE = 115200

def run_diagnostic():
    print("--- INIZIO DIAGNOSTICA RASPBERRY PI ---")
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
        print(f"Porta {SERIAL_PORT} aperta correttamente.")
    except Exception as e:
        print(f"ERRORE: Impossibile aprire la porta seriale: {e}")
        return

    print("In attesa di PING da Arduino (Arduino attenderà fino a 60s)...")
    
    start_time = time.time()
    ping_count = 0
    
    try:
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                
                if "<DIAG:PING>" in line:
                    ping_count += 1
                    print(f"[{ping_count}] Ricevuto PING da Arduino. Rispondo con PONG...")
                    ser.write("<DIAG:PONG>".encode())
                    
                    # Se abbiamo ricevuto almeno 5 ping, consideriamo il test superato
                    if ping_count >= 5:
                        print("\nTEST COMUNICAZIONE SUPERATO CON SUCCESSO!")
                        print("Il link seriale è stabile bidirezionalmente.")
                        break
            
            # Timeout di sicurezza locale nel caso Arduino non invii nulla
            if time.time() - start_time > 120: 
                print("\nTIMEOUT: Nessun segnale ricevuto da Arduino per 120 secondi.")
                break
                
            time.sleep(0.1)
            
    except KeyboardInterrupt:
        print("\nDiagnostica interrotta dall'utente.")
    finally:
        ser.close()
        print("Porta seriale chiusa. Fine diagnostica.")

if __name__ == "__main__":
    run_diagnostic()
