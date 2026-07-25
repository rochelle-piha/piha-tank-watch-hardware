{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  packages = [
    pkgs.git
    pkgs.gcc
    pkgs.arduino-cli
    pkgs.python3
  ];

  shellHook = ''
    repo_root="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
    export ARDUINO_CLI_STATE_DIR="''${ARDUINO_CLI_STATE_DIR:-$repo_root/.arduino}"
    export ARDUINO_DIRECTORIES_DATA="$ARDUINO_CLI_STATE_DIR/data"
    export ARDUINO_DIRECTORIES_DOWNLOADS="$ARDUINO_CLI_STATE_DIR/downloads"
    export ARDUINO_DIRECTORIES_USER="$ARDUINO_CLI_STATE_DIR/sketchbook"
    export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
    bash "$repo_root/firmware/setup-arduino-toolchain.sh"
  '';
}
