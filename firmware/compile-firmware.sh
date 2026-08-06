#!/usr/bin/env bash
# Compile the documented ESP32-C3 firmware without interacting with a device.
#
# Direct execution deliberately fixes the reference C3 FQBN. Flash helpers
# source compile_firmware for their documented alternate-board FQBNs so they
# retain the same pinned-state and caller-owned build-output contract.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=firmware/require-arduino-toolchain.sh
source "$script_dir/require-arduino-toolchain.sh"

readonly PTW_C3_FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc"

require_build_directory() {
  if [ -z "${PTW_ARDUINO_BUILD_DIR:-}" ]; then
    echo "Set PTW_ARDUINO_BUILD_DIR to a new empty writable directory before compiling." >&2
    exit 2
  fi

  if [ -L "$PTW_ARDUINO_BUILD_DIR" ]; then
    echo "PTW_ARDUINO_BUILD_DIR must not be a symlink: $PTW_ARDUINO_BUILD_DIR" >&2
    exit 2
  fi

  if [ ! -d "$PTW_ARDUINO_BUILD_DIR" ] || [ ! -w "$PTW_ARDUINO_BUILD_DIR" ]; then
    echo "PTW_ARDUINO_BUILD_DIR must name an existing writable directory: $PTW_ARDUINO_BUILD_DIR" >&2
    exit 2
  fi

  if find "$PTW_ARDUINO_BUILD_DIR" -mindepth 1 -print -quit | grep -q .; then
    echo "PTW_ARDUINO_BUILD_DIR must be empty so --clean cannot remove caller data: $PTW_ARDUINO_BUILD_DIR" >&2
    exit 2
  fi
}

compile_firmware() {
  local fqbn="$1"
  local sketch="${2:-$script_dir/water_level}"

  require_build_directory
  arduino-cli compile \
    --clean \
    --fqbn "$fqbn" \
    --build-path "$PTW_ARDUINO_BUILD_DIR" \
    "$sketch"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  compile_firmware "$PTW_C3_FQBN"
fi
