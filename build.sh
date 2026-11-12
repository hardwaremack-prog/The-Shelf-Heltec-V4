#!/usr/bin/env bash
# Builds The Shelf into docs/firmware/, for:
#   shelf-v4     Heltec V4  (ESP32-S3R2, 2 MB PSRAM)  -- most V4 boards
#   shelf-v4-r8  Heltec V4-R8 (ESP32-S3R8, 8 MB PSRAM)
#   shelf-v3     Heltec V3  (ESP32-S3FN8, 8 MB flash, no PSRAM)
#   shelf-esp32  a plain ESP32 dev board (NodeMCU-32S / DevKitC: ESP32-D0WD, 4 MB flash)
# Each comes as two pieces, so installing never touches the settings saved at 0x9000-0xE000
# (Wi-Fi, admin password):
#   <name>-boot.bin  0x0..0x9000   bootloader + partition table
#   <name>-main.bin  from 0xE000   otadata + app + The Shelf's web app
# and docs/ota/ holds the pieces a board downloads to update itself over Wi-Fi.
#
# Uses libraries/AsyncTCP: AsyncTCP that skips poll events when its queue is busy (no lwIP deadlock).
# Uses libraries/SD: the core's SD library with longer busy-card waits (see its sd_diskio.cpp).
# Needs: arduino-cli + esp32 core 2.0.17 + the libraries in README.md, mklittlefs,
# and a copy of The Shelf:  SHELF=../the-shelf ./build.sh   (default: clones it)
set -euo pipefail
cd "$(dirname "$0")"
SHELF="${SHELF:-build/the-shelf}"
# the clone is refreshed every build, so boards always get the newest Shelf page
if [ "$SHELF" = build/the-shelf ] && [ -d build/the-shelf/.git ]; then
  git -C build/the-shelf fetch -q --depth 1 origin && git -C build/the-shelf reset -q --hard FETCH_HEAD || echo "(couldn't refresh build/the-shelf; using what's there)"
fi
if [ ! -f "$SHELF/public/app.js" ]; then
  rm -rf build/the-shelf
  git clone --depth 1 https://github.com/hardwaremack-prog/the-shelf.git build/the-shelf
  SHELF=build/the-shelf
fi
mkdir -p build docs/firmware
python3 tools/pack_www.py "$SHELF" build/www-v4.bin 0x200000 V4
python3 tools/pack_www.py "$SHELF" build/www-v3.bin 0x200000 V3
python3 tools/pack_www.py "$SHELF" build/www-esp32.bin 0x130000 ESP32

CORE="$HOME/Arduino/hardware/espressif/esp32"
TOOLPROPS=()
if [ -d "$CORE/tools/xtensa-esp32s3-elf" ]; then   # core installed by hand rather than Boards Manager
  TOOLPROPS=(--build-property "runtime.tools.esptool_py.path=$CORE/tools/esptool"
             --build-property "runtime.tools.xtensa-esp32s3-elf-gcc.path=$CORE/tools/xtensa-esp32s3-elf"
             --build-property "runtime.tools.xtensa-esp32-elf-gcc.path=$CORE/tools/xtensa-esp32-elf")
  command -v ctags >/dev/null && TOOLPROPS+=(--build-property "runtime.tools.ctags.path=$(dirname "$(command -v ctags)")")
fi
ESPTOOL="esptool.py"
[ -f "$CORE/tools/esptool/esptool.py" ] && ESPTOOL="python3 $CORE/tools/esptool/esptool.py"

build() {   # name psram-option flash-size partitions app-size www-offset extra-defines usb
  local name=$1 psram=$2 flash=$3 parts=$4 appmax=$5 wwwoff=$6 defs=$7 cdc=$8 bd="build/$1" www="build/www-v4.bin"
  local chip=esp32s3 fqbn="espressif:esp32:esp32s3:FlashSize=$flash,FlashMode=qio,PSRAM=$psram,USBMode=hwcdc,CDCOnBoot=$cdc,CPUFreq=240" boot=0x0
  [[ "$defs" == *SHELF_V3* ]] && www="build/www-v3.bin"
  if [[ "$defs" == *SHELF_ESP32* ]]; then   # plain ESP32: its bootloader lives at 0x1000; DIO flash is the safe choice
    www="build/www-esp32.bin" chip=esp32 boot=0x1000
    fqbn="espressif:esp32:esp32:FlashSize=$flash,FlashMode=dio,PSRAM=$psram,CPUFreq=240"
  fi
  cp "partitions/$parts" ShelfV4/partitions.csv
  arduino-cli compile \
    --fqbn "$fqbn" \
    --build-property "compiler.cpp.extra_flags=-DCONFIG_ASYNC_TCP_RUNNING_CORE=0 -DCONFIG_ASYNC_TCP_STACK_SIZE=16384 -DCONFIG_ASYNC_TCP_QUEUE_SIZE=256 $defs" \
    --build-property "upload.maximum_size=$appmax" \
    --library "$PWD/libraries/SD" --library "$PWD/libraries/AsyncTCP" \
    ${TOOLPROPS[@]+"${TOOLPROPS[@]}"} --build-path "$bd" ShelfV4
  rm -f ShelfV4/partitions.csv
  local extra=()
  if [[ "$defs" == *SHELF_ESP32* ]]; then   # its update loader goes in the second app slot (see ShelfLoader/)
    cp "partitions/$parts" ShelfLoader/partitions.csv
    arduino-cli compile --fqbn "$fqbn" --build-property "upload.maximum_size=393216" \
      --library "$PWD/libraries/SD" ${TOOLPROPS[@]+"${TOOLPROPS[@]}"} --build-path "build/$name-loader" ShelfLoader
    rm -f ShelfLoader/partitions.csv
    extra=(0x260000 "build/$name-loader/ShelfLoader.ino.bin")
  fi
  local boot_app0
  boot_app0="$(find "$CORE" "$HOME/.arduino15" -name boot_app0.bin 2>/dev/null | head -1)"
  local fill=(); [[ "$chip" == esp32 ]] && fill=(--fill-flash-size "${flash}B")
  $ESPTOOL --chip "$chip" merge_bin -o "build/$name-full.bin" --flash_mode keep --flash_freq keep --flash_size "${flash}B" ${fill[@]+"${fill[@]}"} \
    "$boot" "$bd/ShelfV4.ino.bootloader.bin" 0x8000 "$bd/ShelfV4.ino.partitions.bin" \
    0xe000 "$boot_app0" 0x10000 "$bd/ShelfV4.ino.bin" ${extra[@]+"${extra[@]}"} "$wwwoff" "$www"
  # the two pieces the installer writes, skipping the saved settings in between
  python3 - "build/$name-full.bin" "docs/firmware/$name" <<'PY'
import sys
b = open(sys.argv[1], 'rb').read()
open(sys.argv[2] + '-boot.bin', 'wb').write(b[:0x9000])
open(sys.argv[2] + '-main.bin', 'wb').write(b[0xE000:])
PY
  # the two pieces a board downloads to update itself over Wi-Fi (its /update page)
  mkdir -p docs/ota
  gzip -9 -n -c "$bd/ShelfV4.ino.bin" > "docs/ota/$name-app.bin.gz"
  gzip -9 -n -c "$www" > "docs/ota/$name-www.bin.gz"
  local ver; ver=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' ShelfV4/board.h)
  printf '{"version":"%s","app":"%s-app.bin.gz","appSize":%d,"www":"%s-www.bin.gz","wwwSize":%d}\n' \
    "$ver" "$name" "$(stat -c %s "$bd/ShelfV4.ino.bin")" "$name" "$(stat -c %s "$www")" > "docs/ota/$name.json"
}

#     name        PSRAM    flash partitions       app max  www       defines     Serial
build shelf-v4    enabled  16M   v4_16mb.csv      2097152  0x410000  ""          cdc
build shelf-v4-r8 opi      16M   v4_16mb.csv      2097152  0x410000  ""          cdc
build shelf-v3    disabled 8M    v3_8mb.csv       1703936  0x350000  -DSHELF_V3  default   # V3 talks USB through a CP2102
build shelf-esp32 disabled 4M    esp32_4mb.csv    2424832  0x2C0000  -DSHELF_ESP32 -       # USB through a CP2102/CH340
ls -la docs/firmware
