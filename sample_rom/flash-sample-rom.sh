#!/bin/bash
# Restore the sample ROM (wavetables + samples) into the raw flash region
# at 0xB00000 (5 MiB, unpartitioned — OUTSIDE the storage partition; wiped
# by any erase_flash/full-chip erase). Port: $1 or /dev/ttyUSB0.
PORT="${1:-/dev/ttyUSB0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo Erasing flash
python $IDF_PATH/components/esptool_py/esptool/esptool.py -p "$PORT" -b 460800 erase_region 0xB00000 0x500000
echo Writing flash
python $IDF_PATH/components/esptool_py/esptool/esptool.py -p "$PORT" -b 460800 --flash_size detect write_flash --flash_mode dio 0xB00000 "$SCRIPT_DIR/sample-rom.tbd"
