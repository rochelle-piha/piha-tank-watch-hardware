#!/usr/bin/env bash
# Compile and execute every host-side firmware test using the Nix-provided C++
# toolchain. Keep binaries in a temporary directory rather than the checkout.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
temporary="$(mktemp -d)"
trap 'rm -rf "$temporary"' EXIT

for source in "$root"/firmware/host_tests/test_*.cpp; do
  name="$(basename "${source%.cpp}")"
  g++ -std=c++17 -Wall -Wextra -Werror -o "$temporary/$name" "$source"
  "$temporary/$name"
done
