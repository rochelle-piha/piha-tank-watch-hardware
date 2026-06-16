# Piha Tank Watch — Hardware

**Piha Tank Watch** keeps an eye on your rainwater tank and shows you how much water you have — right from your phone — so you know when you're getting low without climbing up to check. It's built for New Zealand rural and lifestyle properties that rely on tank water.

🌐 **Learn more: [pihatankwatch.nz](https://pihatankwatch.nz)**

This repository is the **open-source hardware and firmware** for the sensor — for DIY builders who'd rather make their own. Everything you need to build and flash a unit is below.

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
- ArduinoJson library

### Quick start

```bash
cd firmware
./flash.sh
```

See [docs/flashing.md](docs/flashing.md) for full instructions.

## Licence

Firmware and docs: MIT licence. Cloud API and web app: proprietary.
