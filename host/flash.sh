#!/bin/sh
# Build the RP2350 firmware, flash it through picotool's reset interface and
# restart fujinet-nio on the new USB port.
#   host/flash.sh             # firmware with the current ROM
#   host/flash.sh --palm      # rebuild the Palm app and ROM first
set -e
HOST=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HOST")
export PATH="$HOME/opt/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
if [ "$1" = "--palm" ]; then
    "$ROOT/palm/build.sh"
fi
cmake -S "$ROOT/firmware" -B "$ROOT/firmware/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSBNIO_ROM="$ROOT/palm/build/sbnio.rom" >/dev/null
cmake --build "$ROOT/firmware/build"
# Flash only the Springboard board, by serial, even with other RP2350s attached.
SERIAL=${SBNIO_SERIAL:-$(cat "$HOST/board.serial" 2>/dev/null)}
[ -n "$SERIAL" ] || { echo "set SBNIO_SERIAL or write host/board.serial" >&2; exit 1; }
"$HOST/nio.sh" stop
picotool load -x "$ROOT/firmware/build/sbnio.uf2" --ser "$SERIAL" -f
sleep 2
"$HOST/nio.sh" start
