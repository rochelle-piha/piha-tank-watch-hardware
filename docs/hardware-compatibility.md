# Hardware compatibility

What the Piha Tank Watch firmware actually runs on today, what's a simple
config change, and what would be a real port. This is the honest engineering
reference for self-builders — only what's shipped or genuinely adaptable.

Legend:
- **Supported** — works as-is with the reference build.
- **Adaptable** — works with a config change (one line in `config.h` + the
  matching flash FQBN); no code changes.
- **DIY-experimental** — the firmware doesn't *block* it (the code is structured
  so you swap a contained piece, not fork it), but it's a code change you make
  yourself and we **haven't bench-validated** it. Not officially supported.
- **Roadmap** — tracked, not built yet.
- **Not supported** — would need a real port (different SDK), not a config swap.

## Boards

| Board | Status | Notes |
|-------|--------|-------|
| **ESP32-C3 SuperMini** | **Supported** (reference) | The default build. Flash FQBN `esp32:esp32:esp32c3`. Pins: TRIG GPIO4, ECHO GPIO5, BOOT GPIO9. |
| ESP32 DevKit / WROOM | **Adaptable** | Select `BOARD_ESP32_DEVKIT` in `config.h`, flash with `FQBN=esp32:esp32:esp32`. BOOT button is GPIO0. |
| ESP32-S3 DevKit | **Adaptable** | Select `BOARD_ESP32S3`, flash with `FQBN=esp32:esp32:esp32s3`. |
| Other ESP32 (C6, etc.) | **Adaptable** | Same Arduino-ESP32 core — set your pins in `config.h` and pass the right FQBN. Confirm pins against the board's pinout (avoid input-only / strapping pins for TRIG). |
| ESP8266 | **Not supported** | Different core/SDK — no `esp_fill_random`, no ESP32 `mbedtls`/`Preferences` semantics. Would be a port. |
| Raspberry Pi Pico W | **Not supported** | Different SDK entirely. Would be a port. |

The firmware logic (WiFi provisioning, credential generation, HTTPS reporting,
backend-driven cadence) is board-agnostic across the ESP32 family — only the
pin map and the flash FQBN differ. See the `BOARD CONFIG` block in `config.h`.

## Sensors

| Sensor | Status | Notes |
|--------|--------|-------|
| **JSN-SR04T** | **Supported** (reference) | Waterproof ultrasonic, trig/echo protocol. The right choice for tanks. ~25 cm blind zone — mount ≥25 cm above the full water line. |
| HC-SR04 | **Adaptable, but unsuitable** | Same trig/echo protocol, so it works electrically with no code change — but it is **not waterproof** and will fail in a tank's humid/condensing environment. Use the JSN-SR04T. |
| UART / analog / pressure sensors (A02YYUW, submersible, etc.) | **Roadmap** | A different read path than trig/echo `pulseIn`. Needs the sensor-read seam. |

## Transports (sensor → backend)

| Transport | Status | Notes |
|-----------|--------|-------|
| **WiFi direct** | **Supported** | The device connects to home WiFi and POSTs readings to the API. |
| WiFi reach workarounds | **Supported** (no hardware change) | If WiFi is marginal at the tank: a WiFi repeater or powerline adapter. The device only connects briefly per reading. |
| LoRa relay (long-range) | **DIY-experimental** | Tank unit transmits over LoRa to a house receiver that proxies to the API — for tanks with no power/WiFi (~1–5 km, NZ 915 MHz). **Not officially supported, not bench-validated**, and the credentials travel over the radio link (encrypt it). Step-by-step + honest limitations: [lora-variant.md](lora-variant.md). |

## Power

| Power | Status | Notes |
|-------|--------|-------|
| **USB (5V)** | **Supported** | Mains USB adapter or a USB power bank. Continuous draw (~80 mA) — a battery bank lasts days, not months, until deep-sleep ships. See [power-options.md](power-options.md). |
| Battery / solar (long-life) | **Roadmap** | Viable once deep-sleep lands — the device will sleep between readings instead of busy-waiting. |

## Reporting cadence

Cadence is **set by the backend**, not the firmware: each `/reading` response
returns `next_interval_secs` per the tank's plan tier (Free = daily, Plus =
hourly, Pro+ = per-minute) and the firmware adapts to it (clamped to a 30 s
floor / 24 h ceiling). The compile-time `READING_INTERVAL_MS` is only the
startup default used before the first server response.
