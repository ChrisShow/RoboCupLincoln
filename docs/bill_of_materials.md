# Bill of Materials — mENIAC
## RoboCup Junior Rescue Maze 2026

**Team:** mENIAC  
**Date:** April 2026  
**Note:** All components are new (first participation). Prices in EUR — to be completed by team.

---

## BOM Table

| # | Component Name / Description | Part Number / Model | Supplier / Source | Status | Kit / Custom | Unit Price (€) | Qty | Total (€) |
|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|:---:|
| 1 | Single-Board Computer — Raspberry Pi 4B, 4 GB RAM, quad-core ARM Cortex-A72 1.8 GHz | Raspberry Pi 4 Model B — 4GB | Raspberry Pi Foundation / local electronics retailer | New | Kit | 115€ | 1 | 115€ |
| 2 | Microcontroller board — Arduino Mega 2560 Rev3, ATmega2560, 54 digital I/O, 16 analog inputs | A000067 | Arduino / local electronics retailer | New | Kit | 30€ | 1 | 30€ |
| 3 | USB camera — Innomaker U20CAM, 1080p, MJPG codec, wide-angle lens, USB 3.0 | CAM-USB-1080P | Innomaker / Amazon | New | Kit | 18.59€ | 2 | 37.18€ |
| 4 | Time-of-Flight distance sensor — VL53L0X, laser ranging up to 200 cm, I2C, 3.3 V/5 V compatible | VL53L0X breakout | Pololu / Amazon | New | Kit | 1.69€ | 6 | 10.14€ |
| 5 | DC gear motor — TT Motor (yellow), 3–6 V, 200 RPM @ 5 V, DIP shaft | TT Motor 200RPM | Generic / AliExpress | New | Kit | 2.03€ | 4 | 8.15€ |
| 6 | Dual motor driver — TB6612FNG, 1.2 A continuous per channel, 3.3 V logic compatible | TB6612FNG breakout | Sparkfun / Amazon | New | Kit | 5.5€ | 2 | 11€ |
| 7 | Color sensor — TCS3200 (TCS34725 compatible), programmable frequency scaling (S0–S3), 5 V | TCS3200 module | Generic / AliExpress | New | Kit | 13.90€  | 1 | 13.90€ |
| 8 | IMU / Gyroscope — MPU6050, 6-axis (3-axis accel + 3-axis gyro), I2C, DMP on-chip | GY-521 / MPU6050 breakout | Generic / AliExpress | New | Kit | 2.93€ | 1 | 2.93€ |
| 9 | Micro servo — SG90, 9 g, 180° rotation, PWM control, torque 1.8 kg·cm @ 5 V | SG90 | Tower Pro / Amazon | New | Kit | 4.17€ | 1 | 4.17€ |
| 10 | LCD display — 16×2 character, HD44780 controller, I2C backpack PCF8574, 5 V, blue backlight | LCD 1602 I2C | Generic / AliExpress | New | Kit | 5.90€ | 1 | 5.90€ |
| 11 | Rechargeable battery pack — Li-ion/LiPo 12 V, nominal capacity ≥ 4 Ah, XT60 connector | 12V 4Ah Li-ion pack | Generic / AliExpress | New | Kit | 28€ | 1 | 28€ |
| 12 | DC-DC step-down converter — XL4015E1, input 8–36 V, output adjustable 1.25–32 V, 5 A max; set to 5.1 V (Raspberry Pi) | XL4015 step-down module | XLSEMI / AliExpress | New | Kit | 5.17€ | 1 | 5.17€ |
| 13 | DC-DC step-down converter — XL4015E1, input 8–36 V, output adjustable 1.25–32 V, 5 A max; set to 6.0 V (motors + servo) | XL4015 step-down module | XLSEMI / AliExpress | New | Kit | 7.99€ | 1 | 7.99€ |
| 14 | Tactile push button — normally open, momentary, rated 12 V / 50 mA; used for START / STOP / calibration | 6×6mm tactile switch | Generic / local electronics retailer | New | Kit | 1.20€ | 1 | 1.20€ |
| 15 | LED indicator — 5 mm red LED, 20 mA, forward voltage ≈ 2.0 V; rescue-active status indicator | 5mm LED red | Generic / local electronics retailer | New | Kit | 0.10€ | 1 | 0.10€ |
| 16 | Current-limiting resistor — 220 Ω, 1/4 W, for LED on Arduino pin 45 | 220R resistor | Generic / local electronics retailer | New | Kit | 0.10€ | 1 | 0.10€ |
| 17 | ON/OFF power switch — SPST rocker switch, rated 10 A / 250 VAC; main power line | SPST rocker switch | Generic / local electronics retailer | New | Kit | 1.99€ | 1 | 1.99€ |
| 18 | 5V cooling fan — 30×30×7 mm brushless DC fan, 5 V, for Raspberry Pi CPU thermal management | 30mm 5V fan | Generic / AliExpress | New | Kit | 2.89€ | 1 | 2.89€ |
| 19 | USB-A to USB-B cable — 0.5 m, for Arduino–Raspberry Pi serial communication | USB-A to USB-B 0.5m | Generic / local electronics retailer | New | Kit | 1.50€ | 1 | 1.50€ |
| 20 | Jumper wires and connector cables — assorted M-M, M-F, F-F, 20–30 cm, for sensor and motor wiring | Jumper wire kit | Generic / AliExpress | New | Kit | / | 1 set | / |
| 21 | Robot chassis — structural frame and body panels, printed in PLA (1.75 mm filament), layer height 0.2 mm, 20% infill; designed in-house | Custom PLA chassis | Custom — FDM 3D print (PLA filament, Bambulab A1 MINI) | New | Custom | / | 1 | / |
| 22 | Drive wheels — 4 wheels printed in TPU 95A (1.75 mm filament), diameter 65 mm, width 26 mm, shore hardness 95A for traction on Rescue Maze tiles; designed in-house | Custom TPU wheels | Custom — FDM 3D print (TPU 95A filament, Creality Ender 3) | New | Custom | / | 4 | / |
| 23 | Medikit capsules / rescue kit — small plastic dices used as dispensable rescue kits; dropped via SG90 servo mechanism (9-paddle wheel, max 8 kits per run) | Rescue kit balls | Generic stationery / craft shop | New | Kit | / | 8 | / |
| 24 | PLA filament — 1.75 mm, orange, for chassis and handle printing | PLA 1.75mm white 1kg | eSUN / Amazon | New | Kit | 18.90€ | 1 spool | 18.90€ |
| 27 | TPU 95A filament — 1.75 mm, white, for wheel printing | TPU 95A 1.75mm black 1kg | JAYO / Amazon | New | Kit | 15.99€ | 1 spool | 15.99€ |

---

## Summary

| Category | Items | Notes |
|---|:---:|---|
| Computing & AI | 2 | Raspberry Pi 4B + Arduino Mega 2560 |
| Cameras | 2 | Innomaker U20CAM × 2 |
| Sensors | 8 | 6× VL53L0X + TCS3200 + MPU6050 |
| Actuators | 5 | 4× TT Motor + SG90 servo |
| Electronics (power/driver) | 6 | 2× TB6612FNG + 2× XL4015E1 + battery + switch |
| Display & UI | 2 | LCD 16×2 I2C + LED + button |
| 3D-printed custom parts | 3 | Chassis (PLA) + wheels (TPU) |
| Consumables & wiring | 7 | Cables, filaments, rescue kits, misc |
| **TOTAL ITEMS** | **27** | |

**Total Cost: €323.1** 

---

*All custom-manufactured components (chassis, wheels, handle) were designed using Autodesk Fusion and sliced with Cura and Bambu Studio. STL files available upon request.*
