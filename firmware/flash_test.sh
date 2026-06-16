#!/usr/bin/env bash
set -euo pipefail
SKETCH="$(dirname "$0")/https_test"
# CDCOnBoot=cdc routes Serial to the C3's NATIVE USB so the harness's
# POSITIVE/NEGATIVE readout is visible over USB on a SuperMini — the bare C3 FQBN
# defaults CDCOnBoot=Disabled (Serial → UART pins → silent over USB) (#189 bench).
# After flashing, open the monitor THEN tap reset so the native-USB port
# re-enumerates and the boot output is captured.
FQBN="${FQBN:-esp32:esp32:esp32c3:CDCOnBoot=cdc}"
PORT="${PORT:-$(arduino-cli board list 2>/dev/null | awk 'NR>1 && $1 ~ /^\/dev\/(cu\.usb|tty\.usb|cu\.SLAB|cu\.wchusbserial)/ {print $1}' | head -1)}"
if [ -z "$PORT" ]; then
  echo "No device detected."
  exit 1
fi
echo "Compiling and flashing test sketch to $PORT..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH"
arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$SKETCH"
echo "Opening serial monitor (Ctrl+C to exit)..."
arduino-cli monitor --port "$PORT" --config baudrate=115200
