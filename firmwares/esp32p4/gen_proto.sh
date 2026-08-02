#!/usr/bin/env bash
# Regenerate main/gen/pv_wire.pb.{c,h} from ../../proto/pv_wire.proto, bounded
# by ../../proto/pv_wire.options. Keep the nanopb generator version in step with
# the vendored runtime in components/nanopb (see pb.h: NANOPB_VERSION).
#
# Needs: python3 with the `nanopb` pip package (pip install nanopb==0.4.9.1).
set -euo pipefail
cd "$(dirname "$0")"

python3 -m nanopb.generator.nanopb_generator \
    --output-dir=main/gen \
    --proto-path=../../proto \
    ../../proto/pv_wire.proto

echo "generated main/gen/pv_wire.pb.{c,h}"
