import serial
import time

# Sostituisci '/dev/ttyACM0' con la tua porta seriale se diversa
arduino = serial.Serial('/dev/ttyACM0', 9600, timeout=1)
time.sleep(2)  # Attendi che la seriale si stabilizzi

def invia_comando(comando):
    arduino.write((comando + '\n').encode())
    risposta = arduino.readline().decode().strip()
    print("Arduino risponde:", risposta)

# Esempio interattivo
while True:
    cmd = input("Invia comando a Arduino (ON/OFF/ESCI): ").strip().upper()
    if cmd == "ESCI":
        break
    elif cmd in ["ON", "OFF"]:
        invia_comando(cmd)
    else:
        print("Comando non valido")
