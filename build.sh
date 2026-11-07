#!/usr/bin/env bash
# Builds The Shelf for Heltec V4 into docs/firmware/:
#   shelf-v4.bin     Heltec V4  (ESP32-S3R2, 2 MB PSRAM)  -- most V4 boards
#   shelf-v4-r8.bin  Heltec V4-R8 (ESP32-S3R8, 8 MB PSRAM)
# Each is a full image (bootloader + app + The Shelf's web app) to flash at 0x0.
#
# Needs: arduino-cli + esp32 core 2.0.17 + the libraries in README.md, mklittlefs,
# and a copy of The Shelf:  SHELF=../the-shelf ./build.sh   (default: clones it)
set -euo pipefail
cd "$(dirname "$0")"
SHELF="${SHELF:-build/the-shelf}"
if [ ! -f "$SHELF/public/app.js" ]; then
  rm -rf build/the-shelf
  git clone --depth 1 https://github.com/hardwaremack-prog/the-shelf.git build/the-shelf
  SHELF=build/the-shelf
fi
mkdir -p build docs/firmware
python3 tools/pack_www.py "$SHELF" build/www.bin 0x200000

CORE="$HOME/Arduino/hardware/espressif/esp32"
TOOLPROPS=()
if [ -d "$CORE/tools/xtensa-esp32s3-elf" ]; then   # core installed by hand rather than Boards Manager
  TOOLPROPS=(--build-property "runtime.tools.esptool_py.path=$CORE/tools/esptool"
             --build-property "runtime.tools.xtensa-esp32s3-elf-gcc.path=$CORE/tools/xtensa-esp32s3-elf")
  command -v ctags >/dev/null && TOOLPROPS+=(--build-property "runtime.tools.ctags.path=$(dirname "$(command -v ctags)")")
fi
ESPTOOL="esptool.py"
[ -f "$CORE/tools/esptool/esptool.py" ] && ESPTOOL="python3 $CORE/tools/esptool/esptool.py"

build() {   # name psram-option
  local name=$1 psram=$2 bd="build/$1"
  cp partitions/v4_16mb.csv ShelfV4/partitions.csv
  arduino-cli compile \
    --fqbn "espressif:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=$psram,USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=240" \
    --build-property "compiler.cpp.extra_flags=-DCONFIG_ASYNC_TCP_RUNNING_CORE=0 -DCONFIG_ASYNC_TCP_STACK_SIZE=16384" \
    --build-property "upload.maximum_size=3145728" \
    ${TOOLPROPS[@]+"${TOOLPROPS[@]}"} --build-path "$bd" ShelfV4
  rm -f ShelfV4/partitions.csv
  local boot_app0
  boot_app0="$(find "$CORE" "$HOME/.arduino15" -name boot_app0.bin 2>/dev/null | head -1)"
  $ESPTOOL --chip esp32s3 merge_bin -o "docs/firmware/$name.bin" --flash_mode keep --flash_freq keep --flash_size 16MB \
    0x0 "$bd/ShelfV4.ino.bootloader.bin" 0x8000 "$bd/ShelfV4.ino.partitions.bin" \
    0xe000 "$boot_app0" 0x10000 "$bd/ShelfV4.ino.bin" 0x310000 build/www.bin
}

build shelf-v4    enabled
build shelf-v4-r8 opi
ls -la docs/firmware
