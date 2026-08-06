# Firmware flashing guide

Flash the Piha Tank Watch firmware onto a new ESP32-C3 SuperMini.
This only needs to be done once — the device generates its own credentials
and configures WiFi via the captive portal on first boot.

## Prerequisites

Use the project's Nix dev environment. It is the supported build route; it
fetches only the artifact hashes pinned in
[`firmware/arduino-toolchain-lock.json`](../firmware/arduino-toolchain-lock.json).

```bash
nix-shell  # from the repo root
bash firmware/bootstrap-arduino-toolchain.sh
```

`nix-shell` makes no Arduino package-manager call and does not mutate a local
cache. The explicit bootstrap creates a small state directory containing only
links to the immutable Nix closure, then makes the Arduino package, library and
download paths read-only. It prints:

```
Pinned ESP32 C3, DevKit/WROOM and S3 toolchains linked at ...
No Arduino package-manager command or network fetch was run.
```

The closure includes the shared ESP32 Arduino source core and only the exact
binary payloads needed by the declared C3, DevKit/WROOM and S3 presets: C3
RISC-V, DevKit/S3 Xtensa, and their matching libraries. It deliberately
excludes S2/C5/C6/H2/P4 and other fallback payloads. A missing pinned artifact
fails closed rather than causing Arduino CLI to download one.

## Compile the reference firmware without a device

The compile-only command uses the documented ESP32-C3 FQBN and never lists
ports, uploads, or opens a serial monitor. Create a new, empty build directory
yourself, then pass it explicitly:

```bash
build_dir="$(mktemp -d)"
PTW_ARDUINO_BUILD_DIR="$build_dir" bash firmware/compile-firmware.sh
```

The command rejects an unset, missing, unwritable, or non-empty build directory
so `--clean` cannot remove unrelated files. It writes the compiled binaries
only under `PTW_ARDUINO_BUILD_DIR`.

## Flash a single device

1. Connect the ESP32-C3 SuperMini to your computer via USB-C.

2. From the repo root, enter the dev environment if you haven't already:
   ```bash
   nix-shell
   bash firmware/bootstrap-arduino-toolchain.sh
   ```

3. Create a new, empty build directory and run the flash script:
   ```bash
   build_dir="$(mktemp -d)"
   PTW_ARDUINO_BUILD_DIR="$build_dir" bash firmware/flash.sh
   ```

   The script first compiles into that caller-owned directory, then
   auto-detects the USB serial port and uploads the compiled binary. It then
   opens the serial monitor so you can see the device ID and confirm it's
   working:

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
build_dir="$(mktemp -d)"
PTW_ARDUINO_BUILD_DIR="$build_dir" bash firmware/compile-firmware.sh
arduino-cli upload --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --port PORT --build-path "$build_dir"
```

> **Keep the `:CDCOnBoot=cdc` on the FQBN.** The bare `esp32:esp32:esp32c3`
> FQBN defaults `CDCOnBoot=Disabled`, which routes `Serial` to the UART pins
> instead of the C3's native USB — so on a SuperMini the **serial monitor stays
> silent over USB** and the device looks dead when it isn't. The helper scripts
> (`flash.sh`/`flash_test.sh`) already set this; match it here.

## Verify a fresh pinned cache

From an empty temporary state, this command proves that the declared C3,
DevKit/WROOM and S3 firmware presets compile without Arduino CLI downloading an
index, core, library or tool. It is useful after changing the lock or Nix
closure:

```bash
state="$(mktemp -d)/piha-tank-watch-arduino"
PTW_ARDUINO_STATE_DIR="$state" nix-shell --run 'bash firmware/test-arduino-toolchain-fresh.sh'
```

The test deliberately leaves the temporary state for inspection. Remove it
yourself after reviewing it.

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
