#!/usr/bin/env bash
# Prove a new, empty state compiles the ESP32-C3 sketch without Arduino CLI
# fetching packages at runtime. Run inside nix-shell with an empty state path.
set -euo pipefail

if [ -z "${PTW_ARDUINO_STATE_DIR:-}" ]; then
  echo "Set PTW_ARDUINO_STATE_DIR to a new empty directory before entering nix-shell." >&2
  exit 2
fi

if [ -e "$PTW_ARDUINO_STATE_DIR" ]; then
  echo "Fresh-cache test requires a state path that does not exist: $PTW_ARDUINO_STATE_DIR" >&2
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
"$repo_root/firmware/bootstrap-arduino-toolchain.sh"
"$repo_root/firmware/require-arduino-toolchain.sh"

arduino-cli compile \
  --clean \
  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc \
  --build-path "$PTW_ARDUINO_STATE_DIR/build" \
  "$repo_root/firmware/water_level"

if find "$PTW_ARDUINO_STATE_DIR/downloads" -mindepth 1 -print -quit | grep -q .; then
  echo "Fresh-cache compile attempted to populate Arduino downloads." >&2
  exit 1
fi

echo "Fresh ESP32-C3 cache compiled without Arduino runtime downloads."
