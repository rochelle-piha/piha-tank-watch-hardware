#!/usr/bin/env bash
# Configure the firmware toolchain inside a repository-local cache.
#
# This runs from shell.nix. It deliberately keeps Arduino's downloaded core and
# library data out of the host configuration so entering the Nix shell is the
# complete, repeatable setup path for this repository.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
state_root="${ARDUINO_CLI_STATE_DIR:-$repo_root/.arduino}"

mkdir -p "$state_root"
export ARDUINO_DIRECTORIES_DATA="${ARDUINO_DIRECTORIES_DATA:-$state_root/data}"
export ARDUINO_DIRECTORIES_DOWNLOADS="${ARDUINO_DIRECTORIES_DOWNLOADS:-$state_root/downloads}"
export ARDUINO_DIRECTORIES_USER="${ARDUINO_DIRECTORIES_USER:-$state_root/sketchbook}"
export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS="${ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS:-https://espressif.github.io/arduino-esp32/package_esp32_index.json}"

core_installed() {
  arduino-cli core list | awk '$1 == "esp32:esp32" { found = 1 } END { exit !found }'
}

library_installed() {
  arduino-cli lib list | awk '$1 == "ArduinoJson" { found = 1 } END { exit !found }'
}

if ! core_installed || ! library_installed; then
  printf 'Setting up Arduino toolchain (one-time, may take a few minutes)...\n'
  if ! core_installed; then
    arduino-cli core update-index
    arduino-cli core install esp32:esp32
  fi
  if ! library_installed; then
    arduino-cli lib install ArduinoJson
  fi
  printf 'Arduino toolchain ready.\n'
fi
