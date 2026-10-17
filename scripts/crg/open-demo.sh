#!/usr/bin/env bash
set -eu
CRG_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$CRG_DIR"
export HORIZON_POOL="$CRG_DIR/examples/demo/pool"
./bin/horizon-crg-native examples/demo/blocks.json "$HORIZON_POOL" validation
exec ./bin/horizon-imp -c examples/demo/blocks.json examples/demo/pictures
