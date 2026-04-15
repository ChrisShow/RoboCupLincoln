import serial
import time

SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE   = 115200

def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=5)
    except Exception as e:
        print(f"[ERRORE] Impossibile aprire {SERIAL_PORT}: {e}")
        return

    time.sleep(2)
    ser.reset_input_buffer()
    print("=== ToF Monitor - mENIAC 2026 ===")
    print(f"In ascolto su {SERIAL_PORT} @ {BAUD_RATE} baud...")
    print("Premi CTRL+C per uscire.\n")

    try:
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[{time.strftime('%H:%M:%S')}] {line}")
    except KeyboardInterrupt:
        print("\nChiuso.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
