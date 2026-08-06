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

- [Wiring diagram](docs/wiring.md) — safe pin assignments and sensor wiring
- [Enclosure and mounting](docs/enclosure-and-mounting.md) — weatherproof box, cable gland, and mounting position
- [Power options](docs/power-options.md) — USB, power bank, and solar/battery notes
- [Firmware & flashing guide](docs/flashing.md) — prerequisites and step-by-step flashing
- [Hardware compatibility](docs/hardware-compatibility.md) — supported boards, sensors, transports, and cadence
- [LoRa long-range variant](docs/lora-variant.md) — DIY-experimental radio relay notes

## Full build guide

[pihatankwatch.nz/diy](https://pihatankwatch.nz/diy)

## Firmware

Firmware is in [`firmware/water_level/`](firmware/water_level/). Flash it once — no WiFi
credentials or API keys needed before install. WiFi is provisioned via captive portal on first boot.

### Requirements

- [Nix](https://nixos.org/download/) with access to the repository-managed shell.

The supported toolchain is deliberately bounded and reproducible: ESP32 Arduino
core 3.3.11; the exact C3, DevKit/WROOM and S3 binary libraries; the matching
RISC-V and Xtensa compilers; esptool; ArduinoJson 7.4.3; and the Arduino CLI
support tools required to flash and monitor a board. Their URLs and SHA-256 hashes live in
[`firmware/arduino-toolchain-lock.json`](firmware/arduino-toolchain-lock.json).
Entering the shell does not install Arduino packages or mutate a cache.

### Quick start

```bash
nix-shell
bash firmware/bootstrap-arduino-toolchain.sh
build_dir="$(mktemp -d)"
PTW_ARDUINO_BUILD_DIR="$build_dir" bash firmware/flash.sh
```

See [docs/flashing.md](docs/flashing.md) for full instructions.

## Licence

Firmware and docs: MIT licence. Cloud API and web app: proprietary.
