#!/bin/sh
# Build the Palm apps and Springboard ROM in the sbnio-palm container.
#   palm/build.sh            # build/*.prc and build/sbnio.rom
#   palm/build.sh clean
# FI Config comes from a fujinet-palm checkout on its nio branch: FI_PALM, by
# default repos/fujinet-palm-nio.
set -e
PALM=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$PALM")
FI_PALM=$(cd "${FI_PALM:-$ROOT/repos/fujinet-palm-nio}" 2>/dev/null && pwd -P) ||
    { echo "no fujinet-palm (nio branch) checkout: set FI_PALM or clone it to repos/fujinet-palm-nio" >&2; exit 1; }
if ! docker image inspect sbnio-palm >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t sbnio-palm "$PALM/docker"
fi
exec docker run --rm --platform linux/amd64 -v "$ROOT":"$ROOT" -v "$FI_PALM":"$FI_PALM" -w "$PALM" \
    sbnio-palm make FI_PALM="$FI_PALM" "$@"
