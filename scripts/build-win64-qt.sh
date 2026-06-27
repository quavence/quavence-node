#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="${LOG:-$ROOT/build-win64-qt.log}"
exec > >(tee -a "$LOG") 2>&1
echo "=== build-win64-qt started $(date -Is) ==="
export PATH="$(echo "$PATH" | sed -e 's/:\/mnt.*//g')"
BUILD="$ROOT/build-win64"
cd "$BUILD"

CONFIG_SITE="$ROOT/depends/x86_64-w64-mingw32/share/config.site" \
  CC_FOR_BUILD=gcc CXX_FOR_BUILD=g++ \
  "$ROOT/configure" --prefix=/ --disable-bench --disable-tests --enable-wallet --with-gui=qt5

secp="$BUILD/src/secp256k1"
if [[ -d "$secp" ]]; then
  gcc -I"$ROOT/src/secp256k1" -I"$secp/src" -O2 \
    "$ROOT/src/secp256k1/src/gen_context.c" -o "$secp/gen_context"
  (cd "$secp" && ./gen_context)
fi

make -j2 src/qt/quavence-qt.exe
ls -lh src/qt/quavence-qt.exe
