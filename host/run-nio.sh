#!/bin/sh
# Run fujinet-nio (FujiBus over serial) on the Springboard NIO link port.
# Its config and data live in host/nio-run; logs go to stdout.
set -e
HOST=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HOST")
NIO=${NIO:-$ROOT/repos/fujinet-nio/build/fujibus-rs232-debug/fujinet-nio}
PORT=${FN_SERIAL_PORT:-$(python3 "$HOST/sbcon.py" --port "NIO link")}
mkdir -p "$HOST/nio-run"
cd "$HOST/nio-run"
echo "fujinet-nio on $PORT"
FN_SERIAL_PORT=$PORT exec "$NIO"
