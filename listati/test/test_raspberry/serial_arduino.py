import serial
import time

# Sostituisci con il nome della porta corretta:
porta = '/dev/ttyACM0'
baud = 9600

def main():
    try:
        ser = serial.Serial(porta, baud, timeout=1)
    except Exception as e:
        print("Errore apertura seriale:", e)
        return

    time.sleep(2)  # aspettare che la connessione seriale si stabilizzi
    ser.reset_input_buffer()

    print("In ascolto su Arduino...")

    while True:
        if ser.in_waiting > 0:
            linea = ser.readline().decode('utf-8', errors='ignore').rstrip()
            print("Arduino dice:", linea)
        time.sleep(0.1)

if __name__ == "__main__":
    main()
