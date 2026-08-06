#!/usr/bin/env python3
"""Fast source-level guardrails for the pinned declared ESP32 toolchain."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
LOCK_PATH = ROOT / "firmware" / "arduino-toolchain-lock.json"
SHELL_PATH = ROOT / "shell.nix"
COMPILE_SCRIPT = ROOT / "firmware" / "compile-firmware.sh"


def fail(message: str) -> None:
    print(f"toolchain-lock verification failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_artifact(name: str, artifact: object) -> None:
    if not isinstance(artifact, dict):
        fail(f"{name} must be an object")
    for field in ("url", "hash"):
        value = artifact.get(field)
        if not isinstance(value, str) or not value:
            fail(f"{name}.{field} is missing")
    if not artifact["url"].startswith("https://"):
        fail(f"{name}.url is not HTTPS")
    if not re.fullmatch(r"sha256-[A-Za-z0-9+/]+={0,2}", artifact["hash"]):
        fail(f"{name}.hash is not an SRI SHA-256 hash")


def main() -> None:
    lock = json.loads(LOCK_PATH.read_text())
    if lock.get("schemaVersion") != 1:
        fail("unsupported schemaVersion")
    if lock.get("board") != "esp32:esp32:esp32c3":
        fail("the lock must retain ESP32-C3 as the reference board")

    for name in ("platform", "c3Libraries", "esp32Libraries", "s3Libraries", "arduinoJson"):
        require_artifact(name, lock.get(name))
        if not lock[name].get("version") or not lock[name].get("archiveRoot"):
            fail(f"{name} must pin a version and archive root")

    if "indexes" in lock:
        fail("the closure must not retain a mutable Arduino index source")

    expected_tools = {
        "esp-rv32",
        "esp-x32",
        "esp32-libs",
        "esp32c3-libs",
        "esp32s3-libs",
        "esptool_py",
    }
    if set(lock.get("allowedEsp32ToolDirectories", [])) != expected_tools:
        fail("the ESP32 binary-payload allowlist changed")

    targets = lock.get("targets")
    if not isinstance(targets, dict) or set(targets) != {"aarch64-darwin", "x86_64-linux"}:
        fail("only the reviewed local and Platform runner targets are supported")
    required_target_artifacts = {
        "riscvToolchain",
        "xtensaToolchain",
        "esptool",
        "ctags",
        "serialDiscovery",
        "serialMonitor",
        "mdnsDiscovery",
        "dfuDiscovery",
    }
    for target_name, target in targets.items():
        if not isinstance(target, dict) or set(target) != required_target_artifacts:
            fail(f"{target_name} does not have the reviewed support-tool set")
        for name, artifact in target.items():
            require_artifact(f"targets.{target_name}.{name}", artifact)
            if not artifact.get("version") or not artifact.get("archiveRoot"):
                fail(f"targets.{target_name}.{name} must pin a version and archive root")

    shell = SHELL_PATH.read_text()
    for required in (
        "builtins.fromJSON (builtins.readFile ./firmware/arduino-toolchain-lock.json)",
        "fetchArtifact = spec: pkgs.fetchurl",
        "PTW_ARDUINO_TOOLCHAIN",
        "PTW_ARDUINO_STATE_DIR",
        '"packages": []',
        '"libraries": []',
        "test ! -e \"$out/data/packages/esp32/tools/$unexpected\"",
        "esp-x32/2601/bin/xtensa-esp-elf-g++",
        "esp32-libs/3.3.11/bin/bootloader_qio_80m.elf",
        "esp32s3-libs/3.3.11/bin/bootloader_qio_80m.elf",
    ):
        if required not in shell:
            fail(f"shell.nix no longer enforces {required!r}")
    if "bootstrap-arduino-toolchain.sh\"" in shell:
        fail("entering the shell must not run the bootstrap")
    if "ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS" in shell:
        fail("the shell must not provide a runtime board-manager URL")

    forbidden = (
        "arduino-cli core update-index",
        "arduino-cli core install",
        "arduino-cli lib install",
        "arduino-cli core download",
    )
    for path in ROOT.rglob("*"):
        if (
            not path.is_file()
            or path == Path(__file__).resolve()
            or ".git" in path.parts
            or ".arduino" in path.parts
        ):
            continue
        text = path.read_text(errors="ignore")
        for pattern in forbidden:
            if pattern in text:
                fail(f"runtime Arduino package-management command remains in {path.relative_to(ROOT)}")

    if (ROOT / "firmware" / "setup-arduino-toolchain.sh").exists():
        fail("legacy auto-install bootstrap still exists")

    compile_script = COMPILE_SCRIPT.read_text()
    for required in (
        'readonly PTW_C3_FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc"',
        "PTW_ARDUINO_BUILD_DIR",
        "must be empty so --clean cannot remove caller data",
        "--clean",
        "--build-path \"$PTW_ARDUINO_BUILD_DIR\"",
        '"$script_dir/require-arduino-toolchain.sh"',
    ):
        if required not in compile_script:
            fail(f"compile-only wrapper no longer enforces {required!r}")
    for forbidden in ("arduino-cli board list", "arduino-cli upload", "arduino-cli monitor"):
        if forbidden in compile_script:
            fail(f"compile-only wrapper must not run {forbidden!r}")

    for script in ("firmware/flash.sh", "firmware/flash_test.sh"):
        text = (ROOT / script).read_text()
        if "compile-firmware.sh" not in text or 'compile_firmware "$FQBN"' not in text:
            fail(f"{script} must reuse the compile-only contract")
        if '--build-path "$PTW_ARDUINO_BUILD_DIR"' not in text:
            fail(f"{script} must upload the caller-owned compile output")

    fresh_test = (ROOT / "firmware" / "test-arduino-toolchain-fresh.sh").read_text()
    if '"$repo_root/firmware/compile-firmware.sh"' not in fresh_test:
        fail("fresh-cache test must exercise the C3 compile-only wrapper")

    print("Pinned ESP32 C3, DevKit/WROOM and S3 toolchain lock and no-runtime-download boundary verified.")


if __name__ == "__main__":
    main()
