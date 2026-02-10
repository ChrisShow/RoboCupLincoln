import serial
import time

SERIAL_PORT = "/dev/ttyACM0"  # o /dev/ttyUSB0
BAUDRATE = 115200

test_string = (
    "00000000000000000000"
    "11111111111111111111"
    "22222222222222222222"
    "33333333333333333333"
)  # 80 caratteri

def main():
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=2)
    time.sleep(2)  # tempo per reset Arduino

    print(f"Lunghezza stringa: {len(test_string)}")
    ser.write((test_string + "\n").encode("utf-8"))
    ser.flush()
    print("Stringa inviata. Guarda il display per 10 secondi.")

    time.sleep(12)
    ser.close()

if __name__ == "__main__":
    main()
