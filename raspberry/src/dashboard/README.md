# Dashboard mENIAC (dimostrativa)

Pagina web con le **2 camere live** e lo **stato di tutti i sensori e motori** del robot.
Il robot **non si muove**: sull'Arduino gira un firmware che legge soltanto i sensori.
I file di gara (`main.py`, `main.ino`, `fine_gara.ino`) non vengono toccati.

## Cosa mostra
- Camera SX (`/dev/video0`) e DX (`/dev/video2`) in streaming MJPEG
- 6 ToF VL53L0X: vista dall'alto del robot e andamento degli ultimi 30 s
- Sensore colore TCS3200 (valori grezzi + classe, se la calibrazione è salvata in EEPROM)
- Motori TB6612 (sempre a 0 in questo firmware), servo, pulsante, LED, MPU6050 (se presente)
- Raspberry: temperatura, carico, RAM, Wi-Fi, **sottotensione / throttling**
- Stato della seriale e log

## Avvio sul robot
1. Carica sull'Arduino `arduino/firmware/src/dashboard_demo/dashboard_demo.ino`
   (i motori restano spenti; per gareggiare ricarica il firmware di gara).
2. Sul Raspberry (main.py **non** deve essere in esecuzione):
   ```bash
   cd raspberry/src
   python3 -m dashboard.standalone
   ```
3. Dal PC/telefono apri `http://<ip-del-raspberry>:8080` (l'indirizzo viene stampato all'avvio).

Opzioni: `--port 8080`, `--serial /dev/ttyACM0`, `--cam-sx 0`, `--cam-dx 2`, `--fps 12`,
`--token PAROLA` (chiede `?token=PAROLA` nell'indirizzo).

## Senza robot
```bash
python3 -m dashboard.standalone --demo
```
Dati e camere finti (c'è anche una mappa del labirinto di esempio): utile per provare la pagina sul PC.

## Rete
Serve solo che PC e Raspberry siano sulla stessa rete (Wi-Fi o hotspot del Pi): la pagina non usa
risorse esterne e funziona anche senza internet. Per vederla da una rete diversa, la soluzione
più semplice è installare [Tailscale](https://tailscale.com) sul Pi e sul PC, invece di aprire porte sul router.

### Con Raspberry Pi Connect
Pi Connect (condivisione schermo + shell remota) e la dashboard funzionano insieme senza conflitti:
Connect non usa né le camere, né la seriale, né la porta 8080.
- Stessa rete: apri `http://<ip-del-raspberry>:8080` sul tuo PC (più fluido).
- Rete diversa: Connect non inoltra la porta 8080. Apri Chromium sul desktop del Pi su
  `http://localhost:8080` e guardalo attraverso la condivisione schermo di Connect.
- Se lo schermo condiviso va a scatti, metti in pausa una camera (pulsante "Live") o avvia con `--fps 6`.

Dipendenze: `opencv-python`, `numpy`, `pyserial` (già usate dal progetto).
