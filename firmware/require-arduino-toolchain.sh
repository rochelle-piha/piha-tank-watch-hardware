#!/usr/bin/env bash
# Verify that a command is using the repository's explicit, pinned bootstrap.
set -euo pipefail

if [ -z "${PTW_ARDUINO_TOOLCHAIN:-}" ] || [ -z "${PTW_ARDUINO_STATE_DIR:-}" ]; then
  echo "Enter nix-shell and run bash firmware/bootstrap-arduino-toolchain.sh first." >&2
  exit 2
fi

toolchain="$PTW_ARDUINO_TOOLCHAIN"
state_root="$PTW_ARDUINO_STATE_DIR"

require_link() {
  local source="$1"
  local destination="$2"
  if [ ! -L "$destination" ] || [ "$(readlink "$destination")" != "$source" ]; then
    echo "Pinned Arduino state is absent or does not match this Nix closure: $destination" >&2
    echo "Run bash firmware/bootstrap-arduino-toolchain.sh with an empty state directory." >&2
    exit 2
  fi
}

require_link "$toolchain/data/packages" "$state_root/data/packages"
require_link "$toolchain/data/package_index.json" "$state_root/data/package_index.json"
require_link "$toolchain/data/package_esp32_index.json" "$state_root/data/package_esp32_index.json"
require_link "$toolchain/data/library_index.json" "$state_root/data/library_index.json"
require_link "$toolchain/data/inventory.yaml" "$state_root/data/inventory.yaml"
require_link "$toolchain/user/libraries" "$state_root/user/libraries"

for required in \
  "$state_root/data/packages/esp32/hardware/esp32/3.3.11/platform.txt" \
  "$state_root/data/packages/esp32/tools/esp-rv32/2601" \
  "$state_root/data/packages/esp32/tools/esp32c3-libs/3.3.11" \
  "$state_root/data/packages/esp32/tools/esptool_py/5.3.1/esptool" \
  "$state_root/data/packages/builtin/tools/ctags/5.8-arduino11/ctags" \
  "$state_root/user/libraries/ArduinoJson/library.properties"; do
  if [ ! -e "$required" ]; then
    echo "Pinned Arduino artifact is missing: $required" >&2
    exit 2
  fi
done

for unexpected in esp-x32 esp32-libs esp32s2-libs esp32s3-libs esp32c5-libs esp32c6-libs esp32h2-libs esp32p4-libs esp32p4_es-libs; do
  if [ -e "$state_root/data/packages/esp32/tools/$unexpected" ]; then
    echo "Unexpected non-C3 ESP32 binary payload found: $unexpected" >&2
    exit 2
  fi
done

if [ -w "$state_root/data" ] || [ -w "$state_root/user" ] || [ -w "$state_root/downloads" ]; then
  echo "Pinned Arduino paths must be read-only; rerun the explicit bootstrap." >&2
  exit 2
fi
