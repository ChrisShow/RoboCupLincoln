import serial
import time

SERIAL_PORT = "/dev/ttyACM0"  # o /dev/ttyUSB0
BAUDRATE = 115200

reply_string = (
    "00000000000000000000"
    "11111111111111111111"
    "22222222222222222222"
    "33333333333333333333"
)  # 80 caratteri, ma Arduino userà solo i primi 60

def main():
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=2)
    time.sleep(2)

    print("In attesa dei dati US da Arduino...")

    while True:
        line = ser.readline().decode("utf-8").strip()
        if not line:
            continue

        # Esempio: USFRONTE:23,USDESTRA:45,USSINISTRA:67
        print(f"Ricevuto da Arduino: {line}")

        # Aspetta 5 secondi
        time.sleep(5)

        # Rimanda la stringa di test
        # Arduino la visualizzerà con prefisso "RB PI dice:"
        ser.write((reply_string + "\n").encode("utf-8"))
        ser.flush()
        print("Stringa di risposta inviata al robot.\n")

        # Pausa prima di attendere nuovo giro
        time.sleep(1)

if __name__ == "__main__":
    main()
