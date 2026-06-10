#!/usr/bin/env bash
set -euo pipefail

SKETCH="$(dirname "$0")/water_level"
FQBN="esp32:esp32:esp32c3"

# Auto-detect the connected ESP32 (USB serial only, exclude Bluetooth)
PORT=$(arduino-cli board list 2>/dev/null | awk 'NR>1 && $1 ~ /^\/dev\/(cu\.usb|tty\.usb|cu\.SLAB|cu\.wchusbserial)/ {print $1}' | head -1)

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
