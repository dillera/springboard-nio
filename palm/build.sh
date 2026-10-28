#!/bin/sh
# Build the Palm app and Springboard ROM in the sbnio-palm container.
#   palm/build.sh            # build/sbnio.prc and build/sbnio.rom
#   palm/build.sh clean
set -e
PALM=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$PALM")
if ! docker image inspect sbnio-palm >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t sbnio-palm "$PALM/docker"
fi
exec docker run --rm --platform linux/amd64 -v "$ROOT":"$ROOT" -w "$PALM" sbnio-palm make "$@"
