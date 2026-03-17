### Schema Elettrico e Pinout - RoboCup Rescue Maze 2026

---

## 1. Sezione Alimentazione (Power Management)

**Regola d'oro:** Tutti i GND (Massa) di batteria, step-down, Raspberry Pi, STM32, driver e sensori **DEVONO essere collegati insieme**.

* **Pacco Batteria 12V (+ Rosso):** Collegato a un polo dell'**Interruttore 0-1**.
* **Pacco Batteria 12V (- Nero):** Collegato alla linea di **GND Comune**.
* **Uscita dell'Interruttore 0-1:** Da qui il segnale 12V si divide in 3 rami:
    1. Ingresso **IN+** dello Step-Down 1 (Logica).
    2. Ingresso **IN+** dello Step-Down 2 (Servo).
    3. Pin **VMOT** di entrambi i driver motori TB6612FNG.

### Moduli Step-Down
* **Step-Down 1 (Logica - Regolato a 5V spaccati):**
    * **OUT+ (5V):** Va al pin `5V` del Raspberry Pi, pin `5V` (o `E5V`) della STM32, `VCC` di tutti i sensori (I2C, ToF, Colore) e della ventolina.
    * **OUT- (GND):** Alla linea di GND Comune.
* **Step-Down 2 (Servo - Regolato a 5V spaccati):**
    * **OUT+ (5V):** Va *ESCLUSIVAMENTE* al cavo rosso (VCC) del servo SG90.
    * **OUT- (GND):** Alla linea di GND Comune.

> **Nota per l'STM32:** Ricorda di spostare il ponticello (jumper) di alimentazione sulla Nucleo per abilitare l'alimentazione esterna (solitamente JP6 o JP2 impostato su "E5V").

---

## 2. Comunicazione STM32 <-> Raspberry Pi (UART)

Si utilizza la porta `UART3` della STM32. La logica è a 3.3V per entrambe le schede, quindi il collegamento è diretto.

| STM32 NUCLEO | Raspberry Pi 4B |
| :--- | :--- |
| `PD8` (TX) | Pin 10 (RX / GPIO 15) |
| `PD9` (RX) | Pin 8 (TX / GPIO 14) |
| `GND` | `GND` |

---

## 3. Collegamenti Raspberry Pi 4B

* **Alimentazione:** Pin 2 o 4 (5V) e Pin 6 (GND) collegati allo Step-Down 1.
* **Ventolina CPU:** Pin 5V e GND (utilizzabili i pin fisici 4 e 6).
* **Camere Innomaker:** Collegate direttamente alle porte USB 3.0 (quelle con l'inserto blu).

---

## 4. Collegamenti STM32 NUCLEO-H7A3ZI-Q



Tutti i componenti seguenti condividono il **GND Comune** e il **VCC** (5V o 3.3V a seconda della tolleranza del modulo).

### A. Bus I2C1 (Sensori e Display)
Tutti i pin SCL vanno uniti insieme, e tutti i pin SDA vanno uniti insieme.

* **STM32 `PB8` (SCL):** ➔ SCL di Display LCD, MPU6050, 8x VL53L0X.
* **STM32 `PB9` (SDA):** ➔ SDA di Display LCD, MPU6050, 8x VL53L0X.

### B. Sensori VL53L0X (Gestione XSHUT)
Per evitare conflitti di indirizzo sul bus I2C, ogni pin XSHUT richiede un pin digitale distinto (Banco PE).

* **Fronte-Destra:** `PE0`
* **Fronte-Sinistra:** `PE1`
* **Retro-Destra:** `PE2`
* **Retro-Sinistra:** `PE3`
* **Lato Destro-Anteriore:** `PE4`
* **Lato Destro-Posteriore:** `PE5`
* **Lato Sinistro-Anteriore:** `PE6`
* **Lato Sinistro-Posteriore:** `PE7`

### C. Driver Motori (TB6612FNG)
Gestione tramite `Timer 1` per i segnali PWM e Banco PC per le direzioni.

**Driver 1 (Fiancata Destra):**
* `PWMA` (Motore Anteriore DX): `PA8` (TIM1_CH1)
* `AIN1` / `AIN2` (Direzione Ant. DX): `PC0` / `PC1`
* `PWMB` (Motore Posteriore DX): `PA9` (TIM1_CH2)
* `BIN1` / `BIN2` (Direzione Post. DX): `PC2` / `PC3`
* `STBY` (Abilitazione): `PC4` (Tenerlo HIGH via codice)

**Driver 2 (Fiancata Sinistra):**
* `PWMA` (Motore Anteriore SX): `PA10` (TIM1_CH3)
* `AIN1` / `AIN2` (Direzione Ant. SX): `PC5` / `PC6`
* `PWMB` (Motore Posteriore SX): `PA11` (TIM1_CH4)
* `BIN1` / `BIN2` (Direzione Post. SX): `PC7` / `PC8`
* `STBY` (Abilitazione): `PC9`

### D. Sensore di Colore (TCS3200)
* `S0`: `PD0`
* `S1`: `PD1`
* `S2`: `PD2`
* `S3`: `PD3`
* `OUT`: `PA0` (Collegato a TIM5_CH1 per Input Capture)
* `OE` (Output Enable): Collegato fisicamente a **GND**

### E. Servo Motore SG90 (Rilascio Medikit)
* **Segnale PWM (Arancione/Giallo):** `PB4` (Collegato a TIM3_CH1 per generare PWM a 50Hz)
* **VCC (Rosso):** Step-Down 2 (5V)
* **GND (Marrone):** GND Comune

### F. Interfaccia Utente (Pulsanti e LED)
* **Pulsante START:** Un pin su `PG0`, l'altro a **GND** (Abilitare Pull-Up interno via codice).
* **Pulsante RESET:** Un pin su `NRST`, l'altro a **GND** (Hard reset del micro).
* **LED di Stato:** Anodo su `PB0`, catodo collegato a resistenza da 330 ohm, e poi a **GND**.