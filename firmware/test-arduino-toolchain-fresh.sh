#!/usr/bin/env bash
# Prove a new, empty state compiles every declared ESP32 board preset without
# Arduino CLI fetching packages at runtime. Run inside nix-shell with an empty
# state path.
set -euo pipefail

if [ -z "${PTW_ARDUINO_STATE_DIR:-}" ]; then
  echo "Set PTW_ARDUINO_STATE_DIR to a new empty directory before entering nix-shell." >&2
  exit 2
fi

if [ -e "$PTW_ARDUINO_STATE_DIR" ]; then
  echo "Fresh-cache test requires a state path that does not exist: $PTW_ARDUINO_STATE_DIR" >&2
  exit 2
fi

export ARDUINO_DIRECTORIES_DATA="$PTW_ARDUINO_STATE_DIR/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$PTW_ARDUINO_STATE_DIR/downloads"
export ARDUINO_DIRECTORIES_USER="$PTW_ARDUINO_STATE_DIR/user"

repo_root="$(git rev-parse --show-toplevel)"
"$repo_root/firmware/bootstrap-arduino-toolchain.sh"
"$repo_root/firmware/require-arduino-toolchain.sh"

export PTW_ARDUINO_BUILD_DIR="$PTW_ARDUINO_STATE_DIR/build-c3"
mkdir "$PTW_ARDUINO_BUILD_DIR"
"$repo_root/firmware/compile-firmware.sh"

compile_preset() {
  local name="$1"
  local fqbn="$2"
  local board_define="${3:-}"
  local args=(
    --clean
    --fqbn "$fqbn"
    --build-path "$PTW_ARDUINO_STATE_DIR/build-$name"
  )

  if [ -n "$board_define" ]; then
    args+=(--build-property "compiler.cpp.extra_flags=-D$board_define")
  fi

  args+=("$repo_root/firmware/water_level")
  arduino-cli compile "${args[@]}"
}

compile_preset devkit "esp32:esp32:esp32" BOARD_ESP32_DEVKIT
compile_preset s3 "esp32:esp32:esp32s3" BOARD_ESP32S3

if find "$PTW_ARDUINO_STATE_DIR/downloads" -mindepth 1 -print -quit | grep -q .; then
  echo "Fresh-cache compile attempted to populate Arduino downloads." >&2
  exit 1
fi

echo "Fresh ESP32 C3, DevKit/WROOM and S3 presets compiled without Arduino runtime downloads."
