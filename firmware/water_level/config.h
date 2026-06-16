#pragma once

// Flash this firmware once — no WiFi credentials or API key needed.
// WiFi is configured by the customer via captive portal on first boot.
// Device credentials are auto-generated and stored on the device.

// API host — prod by default. A `-DPTW_STAGING` build (define the flag at
// compile/flash time) targets the staging environment instead, for validating
// the staging API end-to-end with a real device (#564 staging/prod split). The
// shipped fleet builds WITHOUT this flag — RFC 0001 §4 Option C: devices are
// hardcoded to prod, so a customer unit can never accidentally point at staging.
#ifdef PTW_STAGING
#define API_HOST "api.staging.pihatankwatch.nz"
#else
#define API_HOST "api.pihatankwatch.nz"
#endif
#define API_URL      "https://" API_HOST "/reading"
#define REGISTER_URL "https://" API_HOST "/devices/auto-register"
#define LINK_URL     "https://" API_HOST "/devices/link"

// Firmware version reported with each reading (#111) so the fleet/admin view can
// see what's deployed and plan OTA. Bump this on every released change.
// A staging build carries a "+staging" suffix so its readings are visibly
// distinguishable in the fleet/admin view from real prod devices (QE, #586).
#ifdef PTW_STAGING
#define FIRMWARE_VERSION "1.1.0+staging"
#else
#define FIRMWARE_VERSION "1.1.0"
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  BOARD CONFIG — set this for your board
// ─────────────────────────────────────────────────────────────────────────────
// The default is the ESP32-C3 SuperMini (the reference build). To use a
// different ESP32 board, switch the BOARD_* line below to its preset and flash
// with the matching FQBN, e.g.:
//
//     FQBN=esp32:esp32:esp32 bash firmware/flash.sh
//
// The sketch itself is board-agnostic — only these pins and the flash FQBN
// change. Pin presets are sensible starting points; always confirm them
// against your board's own pinout (avoid input-only / strapping pins for TRIG).
//
// NOT a drop-in: ESP8266 and Raspberry Pi Pico W use a different core/SDK and
// won't compile as-is. See docs/hardware-compatibility.md.

#define BOARD_ESP32C3_SUPERMINI   // ← change this line to select another board

#if defined(BOARD_ESP32C3_SUPERMINI)
  // ESP32-C3 SuperMini — flash FQBN: esp32:esp32:esp32c3
  #define TRIG_PIN 4
  #define ECHO_PIN 5
  #define BOOT_PIN 9    // on-board BOOT button

#elif defined(BOARD_ESP32_DEVKIT)
  // ESP32 DevKit / WROOM — flash FQBN: esp32:esp32:esp32
  #define TRIG_PIN 26
  #define ECHO_PIN 25
  #define BOOT_PIN 0    // on-board BOOT button (GPIO0)

#elif defined(BOARD_ESP32S3)
  // ESP32-S3 DevKit — flash FQBN: esp32:esp32:esp32s3
  #define TRIG_PIN 4
  #define ECHO_PIN 5
  #define BOOT_PIN 0    // on-board BOOT button (GPIO0)

#else
  #error "No board selected in config.h — define one of BOARD_ESP32C3_SUPERMINI / BOARD_ESP32_DEVKIT / BOARD_ESP32S3, or set TRIG_PIN/ECHO_PIN/BOOT_PIN for your board."
#endif

// ── Behaviour ────────────────────────────────────────────────────────────────

// Initial reporting cadence, used until the backend returns its own
// `next_interval_secs` (which it sets per plan tier). See applyServerInterval().
#define READING_INTERVAL_MS 60000
#define WIFI_RESET_HOLD_MS  5000

// ── TLS / SNTP (#189) ────────────────────────────────────────────────────────
// All device HTTPS calls validate the server cert against the embedded Amazon
// root bundle (certs.h) — fail-closed, no insecure fallback. TLS checks the
// cert's validity dates against the device clock, and the ESP32 boots at epoch
// 0 (1970), so SNTP must sync the clock BEFORE the first handshake or validation
// fails "cert not yet valid". beginSecure() waits up to NTP_SYNC_TIMEOUT_MS for a
// plausible clock (ptw_clock_is_plausible) and fails the call closed if it can't.
#define NTP_SYNC_TIMEOUT_MS 15000UL
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"

// Ultrasonic sampling (#190): take SENSOR_SAMPLES pings per reading and report
// the median of the in-range ones, to reject the noise a single ping picks up
// off a moving water surface. Report a failed read if fewer than SENSOR_MIN_VALID
// pings are plausible. SENSOR_PING_GAP_MS lets echoes settle between pings.
#define SENSOR_SAMPLES      5
#define SENSOR_MIN_VALID    3
#define SENSOR_PING_GAP_MS  60

// ── Deep sleep (#188) — optional, off by default ─────────────────────────────
// USB-powered units stay always-on (no per-cycle WiFi reconnect latency). For a
// battery/solar install, uncomment DEEP_SLEEP_ENABLED: the device deep-sleeps
// between readings (µA-class draw vs ~80 mA busy-wait) and reboots each cycle —
// WiFi creds, device credentials, and the server-driven reporting interval all
// persist in NVS across sleep. Hold BOOT to wake; keep holding to reset WiFi.
// ⚠ NOT yet bench-validated (real current draw, wake reliability, sensor
// settling) — pair battery installs with mains/solar top-up until it is.
// #define DEEP_SLEEP_ENABLED           // ← uncomment for battery installs
#define SLEEP_RETRY_CAP_MS  300000UL    // failed cycle → retry within 5 min, not a full day
#define SENSOR_WARMUP_MS    300         // sensor settle after wake before the first ping

// ── Battery telemetry (#111) — optional, off by default ──────────────────────
// USB-powered units have no battery to measure and omit battery telemetry. For a
// battery/solar install, wire the pack through a voltage divider to an ADC pin
// and set BATTERY_ADC_PIN to that GPIO; the device will then report battery_v.
// Set BATTERY_DIVIDER to (R1+R2)/R2 for your divider, and the full/empty volts
// for your chemistry so battery_pct can be derived (defaults suit a 1S LiPo).
// #define BATTERY_ADC_PIN     3        // ← uncomment + set for battery installs
#define BATTERY_DIVIDER     2.0f
#define BATTERY_FULL_V      4.2f
#define BATTERY_EMPTY_V     3.3f
