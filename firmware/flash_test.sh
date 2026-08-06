#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=firmware/compile-firmware.sh
source "$script_dir/compile-firmware.sh"
# CDCOnBoot=cdc routes Serial to the C3's NATIVE USB so the harness's
# POSITIVE/NEGATIVE readout is visible over USB on a SuperMini — the bare C3 FQBN
# defaults CDCOnBoot=Disabled (Serial → UART pins → silent over USB).
# After flashing, open the monitor THEN tap reset so the native-USB port
# re-enumerates and the boot output is captured.
FQBN="${FQBN:-$PTW_C3_FQBN}"

echo "Compiling test sketch..."
compile_firmware "$FQBN" "$script_dir/https_test"

PORT="${PORT:-$(arduino-cli board list 2>/dev/null | awk 'NR>1 && $1 ~ /^\/dev\/(cu\.usb|tty\.usb|cu\.SLAB|cu\.wchusbserial)/ {print $1}' | head -1)}"
if [ -z "$PORT" ]; then
  echo "No device detected."
  exit 1
fi
echo "Flashing test sketch to $PORT..."
arduino-cli upload --fqbn "$FQBN" --port "$PORT" --build-path "$PTW_ARDUINO_BUILD_DIR"
echo "Opening serial monitor (Ctrl+C to exit)..."
arduino-cli monitor --port "$PORT" --config baudrate=115200
