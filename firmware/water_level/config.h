#pragma once

// Flash this firmware once — no WiFi credentials or API key needed.
// WiFi is configured by the customer via captive portal on first boot.
// Device credentials are auto-generated and stored on the device.

#define API_URL      "https://api.pihatankwatch.nz/reading"
#define REGISTER_URL "https://api.pihatankwatch.nz/devices/auto-register"
#define LINK_URL     "https://api.pihatankwatch.nz/devices/link"

// Sensor pins (ESP32-C3 SuperMini)
#define TRIG_PIN 4
#define ECHO_PIN 5
#define BOOT_PIN 9  // BOOT button — hold 5s to reset WiFi credentials

#define READING_INTERVAL_MS 60000
#define WIFI_RESET_HOLD_MS  5000
