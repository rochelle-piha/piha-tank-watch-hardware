#!/usr/bin/env bash
set -euo pipefail
SKETCH="$(dirname "$0")/https_test"
FQBN="esp32:esp32:esp32c3"
PORT=$(arduino-cli board list 2>/dev/null | awk 'NR>1 && $1 ~ /^\/dev\/(cu\.usb|tty\.usb|cu\.SLAB|cu\.wchusbserial)/ {print $1}' | head -1)
if [ -z "$PORT" ]; then
  echo "No device detected."
  exit 1
fi
echo "Compiling and flashing test sketch to $PORT..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH"
arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$SKETCH"
echo "Opening serial monitor (Ctrl+C to exit)..."
arduino-cli monitor --port "$PORT" --config baudrate=115200
