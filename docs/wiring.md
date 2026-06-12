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
VCC  (red)   ───── 3V3   (the 3.3 V pin — see note)
GND  (black) ───── GND
TRIG (yellow)───── GPIO4
ECHO (blue)  ───── GPIO5
```

> **Power the sensor from 3.3 V, not 5 V.** The ESP32-C3's GPIOs are only rated
> to ~3.6 V, but the JSN-SR04T's `ECHO` output sits at its supply voltage — so a
> 5 V supply would drive 5 V into `GPIO5`, over the pin's limit. Running the
> sensor at 3.3 V keeps `ECHO` safe with no extra parts, and the reference build
> works fine this way. The USB-C supply still powers the board at 5 V; the sensor
> just taps the board's regulated 3V3 pin.
>
> **Need more range (a deep tank)?** The JSN-SR04T reaches a little further at
> 5 V. If 3.3 V isn't enough for your depth, power it from 5 V instead and
> protect `GPIO5` — either a resistor divider on `ECHO` (e.g. 1 kΩ from ECHO to
> GPIO5, 2 kΩ from GPIO5 to GND) or a small logic-level step-down. Don't feed 5 V
> straight into the GPIO.

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
