# Firmware flashing guide

Flash the Piha Tank Watch firmware onto a new ESP32-C3 SuperMini.
This only needs to be done once — the device generates its own credentials
and configures WiFi via the captive portal on first boot.

## Prerequisites

Install the Arduino CLI toolchain. The easiest way on macOS/Linux is via
the project's Nix dev environment:

```bash
nix-shell  # from the repo root
```

This installs `arduino-cli`, adds the ESP32 board package, and installs
required libraries automatically on first run. You will see:

```
Setting up Arduino toolchain (one-time, may take a few minutes)...
Arduino toolchain ready.
```

If you prefer to manage Arduino manually:

1. Install [Arduino CLI](https://arduino.github.io/arduino-cli/installation/)
2. Add the ESP32 board package URL:
   ```bash
   arduino-cli config add board_manager.additional_urls \
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core update-index
   arduino-cli core install esp32:esp32
   ```
3. Install ArduinoJson:
   ```bash
   arduino-cli lib install "ArduinoJson"
   ```

## Flash a single device

1. Connect the ESP32-C3 SuperMini to your computer via USB-C.

2. From the repo root, enter the dev environment if you haven't already:
   ```bash
   nix-shell
   ```

3. Run the flash script:
   ```bash
   bash firmware/flash.sh
   ```

   The script auto-detects the USB serial port, compiles the firmware, and
   uploads it. It then opens the serial monitor so you can see the device ID
   and confirm it's working:

   ```
   Found device on /dev/cu.usbserial-...
   Compiling...
   Uploading...
   Done. Opening serial monitor (Ctrl+C to exit)...

   =================================
   Device ID: a3f72b1c
   =================================
   ```

   Note the Device ID — it's useful for troubleshooting. The device registers
   itself automatically on first WiFi connection so you don't need to record it.

4. Press **Ctrl+C** to exit the serial monitor.

## Flash manually (if auto-detect fails)

List connected devices:

```bash
arduino-cli board list
```

Then compile and upload, replacing `PORT` with the detected port:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc firmware/water_level
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --port PORT firmware/water_level
```

> **Keep the `:CDCOnBoot=cdc` on the FQBN.** The bare `esp32:esp32:esp32c3`
> FQBN defaults `CDCOnBoot=Disabled`, which routes `Serial` to the UART pins
> instead of the C3's native USB — so on a SuperMini the **serial monitor stays
> silent over USB** and the device looks dead when it isn't. The helper scripts
> (`flash.sh`/`flash_test.sh`) already set this; match it here.

## After flashing

The device is ready to install. Hand it to the customer or install it yourself:

1. Mount the sensor head pointing straight down into the tank.
2. Power the ESP32 via USB-C.
3. On the customer's phone, connect to the WiFi network **PihaTankWatch-XXXX**.
4. The captive portal opens automatically — select the home WiFi network and
   enter the password.
5. The sensor connects, then asks for an email address to link to an account.
6. The customer enters their email, receives a 6-digit code, and the sensor
   is linked. Done.

See [wiring.md](wiring.md) for hardware connection details.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No port detected | USB driver issue or bad cable | Try a different USB-C cable; install CP210x or CH340 driver |
| Upload fails (permission denied on Linux) | User not in `dialout` group | `sudo usermod -a -G dialout $USER` then log out/in |
| WiFi network doesn't appear | Device crashed or firmware not flashed | Check serial monitor; re-flash if needed |
| Sensor reads `-1` or 0 | Wiring error or sensor out of range | Check TRIG/ECHO connections; minimum range is ~25 cm |
| Device won't enter portal | WiFi credentials saved from previous attempt | Hold BOOT button for 5 seconds to clear credentials |
