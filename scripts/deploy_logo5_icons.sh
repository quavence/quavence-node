#!/usr/bin/env bash
# Deploy Logo5 (icon) + Logo2 (splash) into Qt wallet; sync pixmaps; rebuild .ico/.icns.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HUB="${HUB_ROOT:-/mnt/c/Users/Admin/Desktop/WEB/quavence-dao/quavence_app}"
PACK="$HUB/public/brand/icons"
BRAND="$HUB/public/brand"
LOGO5="$HUB/public/images/Logo5-centered-1024.png"
LOGO2="$BRAND/logo2-1024.png"
ICONS="$ROOT/src/qt/res/icons"
PIX="$ROOT/share/pixmaps"
BUILD="${BUILD_DIR:-$ROOT/build-win64}"

die() { echo "ERROR: $*" >&2; exit 1; }

[[ -f "$LOGO5" ]] || die "missing $LOGO5 — run normalize_logo5.py in quavence_app"
[[ -d "$PACK" ]] || die "missing $PACK — run generate_icon_pack.py in quavence_app"

echo "==> icon pack from hub"
(cd "$HUB" && python3 scripts/generate_icon_pack.py --deploy qt-wallet)
cp -f "$ICONS/bitcoin.ico" "$PIX/bitcoin.ico"
cp -f "$ICONS/bitcoin.ico" "$ICONS/bitcoin_testnet.ico"

echo "==> sync share/pixmaps PNG sizes"
for s in 16 32 64 128 256; do
  cp -f "$PACK/icon-$s.png" "$PIX/bitcoin$s.png"
done

echo "==> bitcoin.icns (macOS bundle)"
if command -v png2icns >/dev/null 2>&1; then
  png2icns "$ICONS/bitcoin.icns" \
    "$PACK/icon-16.png" "$PACK/icon-32.png" "$PACK/icon-48.png" \
    "$PACK/icon-128.png" "$PACK/icon-256.png" "$PACK/icon-512.png" \
    "$PACK/icon-1024.png"
  echo "wrote $ICONS/bitcoin.icns via png2icns"
else
  echo "skip icns: install icnsutils (png2icns) for macOS icon bundle"
fi

echo "==> verify"
python3 "$ROOT/scripts/verify_qt_branding.py"

echo "==> checksums"
md5sum "$ICONS/bitcoin.png" "$PACK/icon-1024.png"
md5sum "$ICONS/bitcoin.ico" "$PACK/favicon.ico"
md5sum "$ICONS/quavence-brand.png" "$LOGO2"

if [[ "${SKIP_REBUILD:-}" == "1" ]]; then
  echo "SKIP_REBUILD=1 — not rebuilding quavence-qt.exe"
  exit 0
fi

if [[ -f "$BUILD/Makefile" ]]; then
  echo "==> rebuild Windows exe icon (windres + link)"
  touch "$ICONS/bitcoin.ico" "$ROOT/src/qt/res/bitcoin-qt-res.rc"
  make -C "$BUILD" -j2 src/qt/quavence-qt.exe
  ls -lh "$BUILD/src/qt/quavence-qt.exe"
else
  echo "skip rebuild: no $BUILD/Makefile"
fi

echo "Done."
