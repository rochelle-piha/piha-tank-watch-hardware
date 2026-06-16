#!/usr/bin/env bash
set -euo pipefail

SKETCH="$(dirname "$0")/water_level"
# CDCOnBoot=cdc routes Serial to the C3's NATIVE USB. Without it the C3 FQBN
# defaults CDCOnBoot=Disabled → Serial goes to the UART pins, so on a SuperMini
# (no UART adapter) the USB serial monitor shows nothing.
# Override for non-C3 boards, e.g. FQBN=esp32:esp32:esp32 bash firmware/flash.sh
# (must match the BOARD_* preset selected in water_level/config.h); a classic
# ESP32 has no native USB, so don't carry CDCOnBoot on that FQBN.
FQBN="${FQBN:-esp32:esp32:esp32c3:CDCOnBoot=cdc}"

# Auto-detect the connected ESP32 (USB serial only, exclude Bluetooth).
# Override with PORT=/dev/ttyUSB0 bash firmware/flash.sh if auto-detect misses it.
PORT="${PORT:-$(arduino-cli board list 2>/dev/null | awk 'NR>1 && $1 ~ /^\/dev\/(cu\.usb|tty\.usb|cu\.SLAB|cu\.wchusbserial)/ {print $1}' | head -1)}"

if [ -z "$PORT" ]; then
  echo "No device detected. Plug in the ESP32 and try again."
  exit 1
fi

echo "Found device on $PORT"
echo "Compiling..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

echo "Uploading..."
arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$SKETCH"

echo "Done. Opening serial monitor (Ctrl+C to exit)..."
arduino-cli monitor --port "$PORT" --config baudrate=115200
