{ pkgs ? import <nixpkgs> {} }:
let
  lock = builtins.fromJSON (builtins.readFile ./firmware/arduino-toolchain-lock.json);
  system = pkgs.stdenv.hostPlatform.system;
  target =
    if builtins.hasAttr system lock.targets
    then lock.targets.${system}
    else throw "Piha Tank Watch's ESP32-C3 toolchain is pinned for aarch64-darwin and x86_64-linux; unsupported system: ${system}";

  # Keep lock hashes over the exact release bytes, rather than an unpacker's
  # platform-dependent output tree. The derivation below unpacks them itself.
  fetchArtifact = spec: pkgs.fetchurl {
    inherit (spec) url hash;
  };

  platform = fetchArtifact lock.platform;
  c3Libraries = fetchArtifact lock.c3Libraries;
  esp32Libraries = fetchArtifact lock.esp32Libraries;
  s3Libraries = fetchArtifact lock.s3Libraries;
  arduinoJson = fetchArtifact lock.arduinoJson;
  riscvToolchain = fetchArtifact target.riscvToolchain;
  xtensaToolchain = fetchArtifact target.xtensaToolchain;
  esptool = fetchArtifact target.esptool;
  ctags = fetchArtifact target.ctags;
  serialDiscovery = fetchArtifact target.serialDiscovery;
  serialMonitor = fetchArtifact target.serialMonitor;
  mdnsDiscovery = fetchArtifact target.mdnsDiscovery;
  dfuDiscovery = fetchArtifact target.dfuDiscovery;

  toolchain = pkgs.runCommand "piha-tank-watch-esp32-toolchain-${lock.platform.version}-${system}" {
    nativeBuildInputs = [ pkgs.unzip pkgs.gnutar pkgs.gzip pkgs.bzip2 ];
  } ''
    unpack() {
      local url="$1"
      local archive="$2"
      local destination="$3"
      case "$url" in
        *.zip) unzip -q "$archive" -d "$destination" ;;
        *.tar.gz) tar -xzf "$archive" -C "$destination" ;;
        *.tar.bz2) tar -xjf "$archive" -C "$destination" ;;
        *) echo "Unsupported pinned archive: $url" >&2; exit 1 ;;
      esac
    }

    mkdir -p \
      "$out/data/packages/esp32/hardware/esp32" \
      "$out/data/packages/esp32/tools" \
      "$out/data/packages/builtin/tools" \
      "$out/user/libraries"

    unpack '${lock.platform.url}' '${platform}' "$out/data/packages/esp32/hardware/esp32"
    mv "$out/data/packages/esp32/hardware/esp32/${lock.platform.archiveRoot}" \
      "$out/data/packages/esp32/hardware/esp32/${lock.platform.version}"
    mkdir -p "$out/unpack/c3-libraries"
    unpack '${lock.c3Libraries.url}' '${c3Libraries}' "$out/unpack/c3-libraries"
    mkdir -p "$out/data/packages/esp32/tools/esp32c3-libs"
    mv "$out/unpack/c3-libraries/${lock.c3Libraries.archiveRoot}" \
      "$out/data/packages/esp32/tools/esp32c3-libs/${lock.c3Libraries.version}"
    mkdir -p "$out/unpack/esp32-libraries"
    unpack '${lock.esp32Libraries.url}' '${esp32Libraries}' "$out/unpack/esp32-libraries"
    mkdir -p "$out/data/packages/esp32/tools/esp32-libs"
    mv "$out/unpack/esp32-libraries/${lock.esp32Libraries.archiveRoot}" \
      "$out/data/packages/esp32/tools/esp32-libs/${lock.esp32Libraries.version}"
    mkdir -p "$out/unpack/s3-libraries"
    unpack '${lock.s3Libraries.url}' '${s3Libraries}' "$out/unpack/s3-libraries"
    mkdir -p "$out/data/packages/esp32/tools/esp32s3-libs"
    mv "$out/unpack/s3-libraries/${lock.s3Libraries.archiveRoot}" \
      "$out/data/packages/esp32/tools/esp32s3-libs/${lock.s3Libraries.version}"
    unpack '${target.riscvToolchain.url}' '${riscvToolchain}' "$out/data/packages/esp32/tools"
    mkdir -p "$out/data/packages/esp32/tools/esp-rv32"
    mv "$out/data/packages/esp32/tools/${target.riscvToolchain.archiveRoot}" \
      "$out/data/packages/esp32/tools/esp-rv32/${target.riscvToolchain.version}"
    unpack '${target.xtensaToolchain.url}' '${xtensaToolchain}' "$out/data/packages/esp32/tools"
    mkdir -p "$out/data/packages/esp32/tools/esp-x32"
    mv "$out/data/packages/esp32/tools/${target.xtensaToolchain.archiveRoot}" \
      "$out/data/packages/esp32/tools/esp-x32/${target.xtensaToolchain.version}"
    unpack '${target.esptool.url}' '${esptool}' "$out/data/packages/esp32/tools"
    mkdir -p "$out/data/packages/esp32/tools/esptool_py"
    mv "$out/data/packages/esp32/tools/${target.esptool.archiveRoot}" \
      "$out/data/packages/esp32/tools/esptool_py/${target.esptool.version}"
    unpack '${target.ctags.url}' '${ctags}' "$out/data/packages/builtin/tools"
    mkdir -p "$out/data/packages/builtin/tools/ctags"
    mv "$out/data/packages/builtin/tools/${target.ctags.archiveRoot}" \
      "$out/data/packages/builtin/tools/ctags/${target.ctags.version}"
    unpack '${target.serialDiscovery.url}' '${serialDiscovery}' "$out/data/packages/builtin/tools"
    mkdir -p "$out/data/packages/builtin/tools/serial-discovery"
    mv "$out/data/packages/builtin/tools/${target.serialDiscovery.archiveRoot}" \
      "$out/data/packages/builtin/tools/serial-discovery/${target.serialDiscovery.version}"
    unpack '${target.serialMonitor.url}' '${serialMonitor}' "$out/data/packages/builtin/tools"
    mkdir -p "$out/data/packages/builtin/tools/serial-monitor"
    mv "$out/data/packages/builtin/tools/${target.serialMonitor.archiveRoot}" \
      "$out/data/packages/builtin/tools/serial-monitor/${target.serialMonitor.version}"
    unpack '${target.mdnsDiscovery.url}' '${mdnsDiscovery}' "$out/data/packages/builtin/tools"
    mkdir -p "$out/data/packages/builtin/tools/mdns-discovery"
    mv "$out/data/packages/builtin/tools/${target.mdnsDiscovery.archiveRoot}" \
      "$out/data/packages/builtin/tools/mdns-discovery/${target.mdnsDiscovery.version}"
    unpack '${target.dfuDiscovery.url}' '${dfuDiscovery}' "$out/data/packages/builtin/tools"
    mkdir -p "$out/data/packages/builtin/tools/dfu-discovery"
    mv "$out/data/packages/builtin/tools/${target.dfuDiscovery.archiveRoot}" \
      "$out/data/packages/builtin/tools/dfu-discovery/${target.dfuDiscovery.version}"
    unpack '${lock.arduinoJson.url}' '${arduinoJson}' "$out/user/libraries"
    mv "$out/user/libraries/${lock.arduinoJson.archiveRoot}" \
      "$out/user/libraries/ArduinoJson"
    # The installed C3-only artifacts are sufficient for Arduino CLI. Empty,
    # immutable manager indexes deliberately remove any runtime route to the
    # mutable upstream board/library catalogues.
    printf '%s\n' '{' '  "packages": []' '}' > "$out/data/package_index.json"
    printf '%s\n' '{' '  "packages": []' '}' > "$out/data/package_esp32_index.json"
    printf '%s\n' '{' '  "libraries": []' '}' > "$out/data/library_index.json"

    printf '%s\n' \
      'installation:' \
      '    id: 8b7bf3cf-b2fc-4808-9103-e4b479ef064d' \
      '    secret: 2b7f29d18c32db44a57a892eff8f4c2d' \
      > "$out/data/inventory.yaml"

    test -x "$out/data/packages/esp32/tools/esp-rv32/2601/bin/riscv32-esp-elf-g++"
    test -x "$out/data/packages/esp32/tools/esp-x32/2601/bin/xtensa-esp-elf-g++"
    test -e "$out/data/packages/esp32/tools/esp32-libs/3.3.11/bin/bootloader_qio_80m.elf"
    test -e "$out/data/packages/esp32/tools/esp32s3-libs/3.3.11/bin/bootloader_qio_80m.elf"
    test -x "$out/data/packages/esp32/tools/esptool_py/5.3.1/esptool"
    test -x "$out/data/packages/builtin/tools/ctags/5.8-arduino11/ctags"
    for unexpected in esp32s2-libs esp32c5-libs esp32c6-libs esp32h2-libs esp32p4-libs esp32p4_es-libs; do
      test ! -e "$out/data/packages/esp32/tools/$unexpected"
    done
  '';
in
pkgs.mkShell {
  packages = [
    pkgs.git
    pkgs.gcc
    pkgs.arduino-cli
    pkgs.python3
    pkgs.shellcheck
  ];

  PTW_ARDUINO_TOOLCHAIN = toolchain;

  shellHook = ''
    export PTW_ARDUINO_TOOLCHAIN="${toolchain}"
    export PTW_ARDUINO_STATE_DIR="''${PTW_ARDUINO_STATE_DIR:-''${XDG_CACHE_HOME:-$HOME/.cache}/piha-tank-watch-hardware/arduino-esp32c3-v1}"
    export ARDUINO_DIRECTORIES_DATA="$PTW_ARDUINO_STATE_DIR/data"
    export ARDUINO_DIRECTORIES_DOWNLOADS="$PTW_ARDUINO_STATE_DIR/downloads"
    export ARDUINO_DIRECTORIES_USER="$PTW_ARDUINO_STATE_DIR/user"
    printf '%s\n' "ESP32 C3, DevKit/WROOM and S3 toolchains are pinned and read-only. Run bash firmware/bootstrap-arduino-toolchain.sh before compiling or flashing."
  '';
}
