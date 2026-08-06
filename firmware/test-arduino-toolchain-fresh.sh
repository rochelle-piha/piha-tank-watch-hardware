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

arduino_cli_dir="$(dirname "$(command -v arduino-cli)")"
path_without_arduino_cli=":$PATH:"
path_without_arduino_cli="${path_without_arduino_cli//:$arduino_cli_dir:/:}"
path_without_arduino_cli="${path_without_arduino_cli#:}"
path_without_arduino_cli="${path_without_arduino_cli%:}"

expect_precompile_rejection() {
  local description="$1"
  shift
  local output
  local status

  if output="$("$@" 2>&1)"; then
    status=0
  else
    status=$?
  fi

  if [ "$status" -ne 2 ]; then
    echo "$description must exit 2 before invoking arduino-cli (got $status): $output" >&2
    exit 1
  fi
}

compile_wrapper="$repo_root/firmware/compile-firmware.sh"
expect_precompile_rejection \
  "Unset build directory" \
  env -u PTW_ARDUINO_BUILD_DIR PATH="$path_without_arduino_cli" "$BASH" "$compile_wrapper"
expect_precompile_rejection \
  "Missing build directory" \
  env PTW_ARDUINO_BUILD_DIR="$PTW_ARDUINO_STATE_DIR/missing-build-directory" PATH="$path_without_arduino_cli" "$BASH" "$compile_wrapper"

non_empty_build_directory="$PTW_ARDUINO_STATE_DIR/non-empty-build-directory"
mkdir "$non_empty_build_directory"
printf 'caller sentinel\n' > "$non_empty_build_directory/sentinel"
expect_precompile_rejection \
  "Non-empty build directory" \
  env PTW_ARDUINO_BUILD_DIR="$non_empty_build_directory" PATH="$path_without_arduino_cli" "$BASH" "$compile_wrapper"
if ! grep -qx 'caller sentinel' "$non_empty_build_directory/sentinel"; then
  echo "Non-empty build-directory rejection modified caller data." >&2
  exit 1
fi

symlink_target_directory="$PTW_ARDUINO_STATE_DIR/symlink-target-directory"
mkdir "$symlink_target_directory"
printf 'caller sentinel\n' > "$symlink_target_directory/sentinel"
symlink_build_directory="$PTW_ARDUINO_STATE_DIR/symlink-build-directory"
ln -s "$symlink_target_directory" "$symlink_build_directory"
expect_precompile_rejection \
  "Symlink build directory" \
  env PTW_ARDUINO_BUILD_DIR="$symlink_build_directory" PATH="$path_without_arduino_cli" "$BASH" "$compile_wrapper"
if ! grep -qx 'caller sentinel' "$symlink_target_directory/sentinel"; then
  echo "Symlink build-directory rejection modified target caller data." >&2
  exit 1
fi

unwritable_build_directory="$PTW_ARDUINO_STATE_DIR/unwritable-build-directory"
mkdir "$unwritable_build_directory"
chmod a-w "$unwritable_build_directory"
if [ -w "$unwritable_build_directory" ]; then
  echo "Skipping unwritable build-directory rejection: this environment cannot prove the fixture is unwritable." >&2
else
  expect_precompile_rejection \
    "Unwritable build directory" \
    env PTW_ARDUINO_BUILD_DIR="$unwritable_build_directory" PATH="$path_without_arduino_cli" "$BASH" "$compile_wrapper"
fi
chmod u+w "$unwritable_build_directory"

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
