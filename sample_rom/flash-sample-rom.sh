#!/bin/bash
# Restore the sample ROM (wavetables + samples) into the raw flash region
# at 0xB00000 (5 MiB, unpartitioned — OUTSIDE the storage partition; wiped
# by any erase_flash/full-chip erase). Port: $1 or /dev/ttyUSB0.
PORT="${1:-/dev/ttyUSB0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

ESPTOOL_BIN="${ESPTOOL_BIN:-esptool}"
if ! command -v "$ESPTOOL_BIN" >/dev/null 2>&1; then
    ESPTOOL_BIN="python $IDF_PATH/components/esptool_py/esptool/esptool.py"
fi

echo Erasing flash
$ESPTOOL_BIN -p "$PORT" -b 460800 erase_region 0xB00000 0x500000
echo Writing flash
$ESPTOOL_BIN -p "$PORT" -b 460800 write_flash --flash_size detect --flash_mode dio 0xB00000 "$SCRIPT_DIR/sample-rom.tbd"
