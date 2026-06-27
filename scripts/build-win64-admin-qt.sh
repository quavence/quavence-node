#!/usr/bin/env bash
# Admin Qt wallet for Windows (cross-compile via mingw).
# Separate from public build-win64/: includes bootstrap RPC/miner tooling.
#
# Output:
#   build-win64-admin/src/qt/quavence-qt.exe
#   build-win64-admin/src/qt/quavence-qt-admin.exe  (copy for side-by-side use)
#   build-win64-admin/src/quavenced.exe
#   build-win64-admin/src/quavence-cli.exe
#
# Usage (WSL):
#   sed -i 's/\r$//' scripts/build-win64-admin-qt.sh
#   bash scripts/build-win64-admin-qt.sh
#
# Run with bootstrap (example quavence.conf or CLI):
#   bootstrapmining=1
#   bootstrapmineonstart=0
#   staking=0
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="${LOG:-$ROOT/build-win64-admin-qt.log}"
exec > >(tee -a "$LOG") 2>&1
echo "=== build-win64-admin-qt started $(date -Is) ==="

export PATH="$(echo "$PATH" | sed -e 's/:\/mnt.*//g')"
BUILD="${BUILD_DIR:-$ROOT/build-win64-admin}"
JOBS="${JOBS:-2}"

restore_configure() {
  if [[ -f "$ROOT/configure.ac.public-bak" ]]; then
    mv -f "$ROOT/configure.ac.public-bak" "$ROOT/configure.ac"
    "$ROOT/autogen.sh" >/dev/null 2>&1 || true
  fi
}
trap restore_configure EXIT

cp "$ROOT/configure.ac" "$ROOT/configure.ac.public-bak"
python3 - <<'PY'
from pathlib import Path
p = Path("configure.ac")
p.write_text(p.read_text().replace(
    "define(_CLIENT_VERSION_IS_RELEASE, true)",
    "define(_CLIENT_VERSION_IS_RELEASE, false)",
))
PY

"$ROOT/autogen.sh" >/dev/null 2>&1

mkdir -p "$BUILD"
cd "$BUILD"

CONFIG_SITE="$ROOT/depends/x86_64-w64-mingw32/share/config.site" \
  CC_FOR_BUILD=gcc CXX_FOR_BUILD=g++ \
  "$ROOT/configure" --prefix=/ --disable-bench --disable-tests \
    --enable-wallet --with-gui=qt5 --enable-bootstrap-tools

secp="$BUILD/src/secp256k1"
if [[ -d "$secp" ]]; then
  gcc -I"$ROOT/src/secp256k1" -I"$secp/src" -O2 \
    "$ROOT/src/secp256k1/src/gen_context.c" -o "$secp/gen_context"
  (cd "$secp" && ./gen_context)
fi

make -j"$JOBS" src/qt/quavence-qt.exe src/quavenced.exe src/quavence-cli.exe

cp -f src/qt/quavence-qt.exe src/qt/quavence-qt-admin.exe

echo "=== verify admin binaries ==="
make -C src verify-admin-binaries

strings src/qt/quavence-qt.exe | grep -q 'QVNC_BUILD_VARIANT=admin'

ls -lh src/qt/quavence-qt.exe src/qt/quavence-qt-admin.exe src/quavenced.exe src/quavence-cli.exe

echo ""
echo "Admin Qt wallet ready:"
echo "  $BUILD/src/qt/quavence-qt-admin.exe"
echo "  Daemon: $BUILD/src/quavenced.exe"
echo "  CLI:    $BUILD/src/quavence-cli.exe"
echo ""
echo "Keep public and admin builds in separate folders — do not mix quavence.conf bootstrap flags into public Qt."
