#!/usr/bin/env bash
# Dev-build the pico-view engine and point the `pico_view` package at it.
#
# The package does not carry committed binaries: its hook/build.dart downloads
# the release pinned in packages/pico_view/native/engine.lock. This script writes
# the `engine.local` override next to that lock, so the package links the library
# you just built instead of a published one — which is how you test an unreleased
# engine against the Flutter app.
#
# Delete packages/pico_view/native/engine.local to go back to the pinned release.
#
#   ./build.sh [--target TRIPLE]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET=x86_64-unknown-linux-gnu

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target) TARGET="$2"; shift 2 ;;
    --target=*) TARGET="${1#*=}"; shift ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "!! unknown argument: $1" >&2; exit 2 ;;
  esac
done

case "$TARGET" in
  *-linux-*)   libname=libpico_view.so ;;
  *-apple-*)   libname=libpico_view.dylib ;;
  *-windows-*) libname=pico_view.dll ;;
  *) echo "!! unsupported target: $TARGET" >&2; exit 2 ;;
esac

echo ">> building $TARGET"
cargo build --release -p pico-view --target "$TARGET"

lib="$HERE/target/$TARGET/release/$libname"
local_file="$HERE/packages/pico_view/native/engine.local"

# A path relative to engine.local's own directory, so the override survives the
# checkout being moved or renamed. The hook resolves it against that directory.
cat > "$local_file" <<EOF
# Written by build.sh — makes hook/build.dart link this locally built engine
# instead of the release pinned in engine.lock. Delete this file to go back.
# Built for $TARGET.
../../../target/$TARGET/release/$libname
EOF

echo "   -> $lib"
echo "   -> wrote $local_file"
echo ">> remember to regenerate Dart bindings if src/pico_view.h changed:"
echo "   (in packages/pico_view) dart run ffigen --config ffigen.yaml"
