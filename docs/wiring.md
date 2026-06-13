# Wiring diagram

## Components

| Part | Purpose |
|------|---------|
| ESP32-C3 SuperMini | Main controller + WiFi |
| JSN-SR04T | Waterproof ultrasonic distance sensor |
| USB-C power supply (5V 1A) | Powers the ESP32 + sensor |
| Weatherproof enclosure | Protects the ESP32 from moisture |

## Connections

```
JSN-SR04T          ESP32-C3 SuperMini
─────────          ──────────────────
VCC  (red)   ───── 5V  (or VIN)
GND  (black) ───── GND
TRIG (yellow)───── GPIO4
ECHO (blue)  ───── GPIO5
```

> ⚠ **ECHO line — bench verification pending (#498):** In standard trig/echo mode the JSN-SR04T's ECHO output is nominally 5 V logic. The ESP32-C3 GPIO5 is a 3.3 V input and may not be 5 V-tolerant — a direct connection is out of spec. This wiring has **not yet been verified on physical hardware**. Before building, confirm your JSN-SR04T variant outputs 3.3 V on ECHO, or add a voltage divider or level-shifter on the ECHO line. This notice will be removed once the reference build is bench-tested.

The JSN-SR04T sensor module has a cable that exits from the back. The other
end of the cable has four bare wires. The waterproof transducer head is
permanently attached to the module via a cable — do not modify this.

## Physical layout

```
                          ┌─ Tank lid / mounting point
                          │
         Sensor head      │
         (inside tank)    │
              │           │
              │ cable     │
              │           │
   ┌──────────┴────────┐  │
   │   JSN-SR04T PCB   │  │
   │  (in enclosure)   ◄──┘
   └─────────┬─────────┘
     4-wire  │ cable
             │
   ┌─────────┴─────────┐
   │  ESP32-C3 SuperMini│
   │  (in enclosure)   │
   └─────────┬─────────┘
             │ USB-C
          5V power
```

## Mounting notes

- The JSN-SR04T sensor head should point straight down into the tank.
- Mount it centrally if possible — avoid pointing at the fill pipe.
- The recommended clearance from the sensor head to the water surface is
  **≥ 25 cm** (the firmware ignores readings closer than this). Mounting
  too close to the maximum fill level means some capacity won't be measured.
- The cable between the sensor head and the PCB is typically 2.5 m — this
  gives enough reach to keep the electronics outside the tank.
- The ESP32 PCB and the JSN-SR04T module can share the same weatherproof
  enclosure. Use a junction box with a cable gland for the sensor head cable.
- Power the ESP32 via USB-C. A standard phone charger or a weatherproof
  outdoor power point works well.

## Boot button

GPIO9 is wired to the BOOT button on the ESP32-C3 SuperMini (built-in).
Hold it for **5 seconds** to clear stored WiFi credentials and re-enter
the captive portal setup mode.

## Serial monitor

Connect via USB and open a serial monitor at **115200 baud** to view
device ID, WiFi status, and live readings. On macOS/Linux:

```bash
arduino-cli monitor --port /dev/cu.usbserial-* --config baudrate=115200
```
