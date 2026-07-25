#!/usr/bin/env bash
# Materialise only links to the immutable, Nix-fetched ESP32-C3 toolchain.
#
# This is deliberately an explicit command. Entering nix-shell must not fetch
# Arduino packages, mutate a cache, or install every ESP32-family artifact.
set -euo pipefail

if [ -z "${PTW_ARDUINO_TOOLCHAIN:-}" ] || [ -z "${PTW_ARDUINO_STATE_DIR:-}" ]; then
  echo "Enter nix-shell first; it provides the pinned ESP32-C3 toolchain." >&2
  exit 2
fi

toolchain="$PTW_ARDUINO_TOOLCHAIN"
state_root="$PTW_ARDUINO_STATE_DIR"

if [ ! -d "$toolchain/data" ] || [ ! -d "$toolchain/user" ]; then
  echo "The supplied PTW_ARDUINO_TOOLCHAIN is incomplete: $toolchain" >&2
  exit 2
fi

mkdir -p "$state_root/data" "$state_root/user" "$state_root/downloads"

link_if_expected() {
  local source="$1"
  local destination="$2"

  if [ -e "$destination" ] || [ -L "$destination" ]; then
    if [ -L "$destination" ] && [ "$(readlink "$destination")" = "$source" ]; then
      return
    fi
    echo "Refusing to replace existing state at $destination." >&2
    echo "Use an empty PTW_ARDUINO_STATE_DIR or inspect that state first." >&2
    exit 2
  fi

  ln -s "$source" "$destination"
}

link_if_expected "$toolchain/data/packages" "$state_root/data/packages"
link_if_expected "$toolchain/data/package_index.json" "$state_root/data/package_index.json"
link_if_expected "$toolchain/data/package_esp32_index.json" "$state_root/data/package_esp32_index.json"
link_if_expected "$toolchain/data/library_index.json" "$state_root/data/library_index.json"
link_if_expected "$toolchain/data/inventory.yaml" "$state_root/data/inventory.yaml"
link_if_expected "$toolchain/user/libraries" "$state_root/user/libraries"

# A missing artifact must fail closed. Arduino CLI cannot populate this cache
# behind the user's back after the bootstrap has completed.
chmod a-w "$state_root/data" "$state_root/user" "$state_root/downloads"

echo "Pinned ESP32-C3 toolchain linked at $state_root"
echo "No Arduino package-manager command or network fetch was run."
