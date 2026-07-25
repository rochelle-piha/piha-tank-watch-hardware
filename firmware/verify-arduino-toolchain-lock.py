#!/usr/bin/env python3
"""Fast source-level guardrails for the pinned ESP32-C3 toolchain."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
LOCK_PATH = ROOT / "firmware" / "arduino-toolchain-lock.json"
SHELL_PATH = ROOT / "shell.nix"


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
        fail("the lock must be specific to ESP32-C3")

    for name in ("platform", "c3Libraries", "arduinoJson"):
        require_artifact(name, lock.get(name))
        if not lock[name].get("version") or not lock[name].get("archiveRoot"):
            fail(f"{name} must pin a version and archive root")

    if "indexes" in lock:
        fail("the closure must not retain a mutable Arduino index source")

    expected_tools = {"esp-rv32", "esp32c3-libs", "esptool_py"}
    if set(lock.get("allowedEsp32ToolDirectories", [])) != expected_tools:
        fail("the ESP32 binary-payload allowlist changed")

    targets = lock.get("targets")
    if not isinstance(targets, dict) or set(targets) != {"aarch64-darwin", "x86_64-linux"}:
        fail("only the reviewed local and Platform runner targets are supported")
    required_target_artifacts = {
        "riscvToolchain",
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
    for script in ("firmware/flash.sh", "firmware/flash_test.sh"):
        if "require-arduino-toolchain.sh" not in (ROOT / script).read_text():
            fail(f"{script} must verify the explicit pinned bootstrap")

    print("Pinned ESP32-C3 toolchain lock and no-runtime-download boundary verified.")


if __name__ == "__main__":
    main()
