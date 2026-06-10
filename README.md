# Piha Tank Watch — Hardware

Open-source firmware and documentation for the Piha Tank Watch ESP32-C3 sensor.

## Hardware required

| Part | Notes |
|------|-------|
| ESP32-C3 SuperMini | Main controller + WiFi |
| JSN-SR04T | Waterproof ultrasonic distance sensor |
| IP65 weatherproof enclosure | Fits the ESP32 + cable gland |
| USB 5V power supply | 500 mA minimum |
| M12 cable gland | Seals sensor cable entry |

Typical build cost: **$30–60 NZD**. Parts available from Jaycar, PB Tech, or AliExpress.

## Docs

- [Wiring diagram](docs/wiring.md) — pin assignments and mounting notes
- [Firmware & flashing guide](docs/flashing.md) — prerequisites and step-by-step flashing

## Full build guide

[pihatankwatch.nz/diy](https://pihatankwatch.nz/diy)

## Firmware

Firmware is in [`firmware/water_level/`](firmware/water_level/). Flash it once — no WiFi
credentials or API keys needed before install. WiFi is provisioned via captive portal on first boot.

### Requirements

- [arduino-cli](https://arduino.github.io/arduino-cli/latest/installation/) >= 0.35
- ESP32 Arduino core >= 3.0

### Quick start

```bash
cd firmware
./flash.sh
```

See [docs/flashing.md](docs/flashing.md) for full instructions.

## Licence

Firmware and docs: MIT licence. Cloud backend and web app: proprietary.
