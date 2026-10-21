#!/usr/bin/env bash
# Cross-compile Quavence Qt wallet for 64-bit Windows from Linux/WSL.
# See doc/build-windows.md — this script only automates the documented flow.
#
# Release version: bump repo root VERSION and configure.ac _CLIENT_VERSION_* together,
# run autoreconf -fi, then rebuild so quavence-qt.exe and quavenced report the same number.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# WSL: avoid Windows PATH entries breaking autoconf (doc/build-windows.md)
export PATH="$(echo "$PATH" | sed -e 's/:\/mnt.*//g')"

# WSL can execute mingw PE via binfmt; autotools then mis-detects "not cross
# compiling" and builds secp256k1 gen_context as gen_context.exe (fails to run
# as ./gen_context). Prefer native gcc for *_FOR_BUILD tools.
export CC_FOR_BUILD="${CC_FOR_BUILD:-gcc}"
export CXX_FOR_BUILD="${CXX_FOR_BUILD:-g++}"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  echo "ERROR: MinGW-w64 toolchain not found."
  echo "Install (Debian/Ubuntu): sudo apt install g++-mingw-w64-x86-64"
  echo "Then set POSIX threads (required for std::mutex), e.g.:"
  echo "  sudo update-alternatives --config x86_64-w64-mingw32-g++   # choose posix"
  exit 1
fi

# win32-thread mingw breaks C++11 std::mutex; require posix variant
if x86_64-w64-mingw32-g++ -v 2>&1 | grep -q win32; then
  echo "ERROR: x86_64-w64-mingw32-g++ is the win32-threads build."
  echo "Run: sudo update-alternatives --config x86_64-w64-mingw32-g++"
  echo "     and select the *posix* entry."
  exit 1
fi

JOBS="${JOBS:-2}"

if [[ -f "$ROOT/config.status" ]]; then
  echo "ERROR: $ROOT was already configured (Linux/in-tree build)."
  echo "Autotools allows only one configure tree in the source directory."
  echo ""
  echo "Fix (depends stay cached — no need to rebuild them):"
  echo "  cd $ROOT && make distclean"
  echo "  ./contrib/cross-build-win64-wallet.sh"
  echo ""
  echo "After the Windows build, reconfigure Linux in src/ if you still need it:"
  echo "  cd src && ../configure --disable-wallet --without-gui && make -j\$(nproc)"
  exit 1
fi

if [[ "${SKIP_DEPENDS:-}" != "1" ]]; then
  echo "==> Building depends for x86_64-w64-mingw32 (first run can take hours)..."
  make -C depends HOST=x86_64-w64-mingw32 -j"${JOBS}"
else
  echo "==> SKIP_DEPENDS=1 — using existing depends/x86_64-w64-mingw32"
fi

if [[ ! -f depends/x86_64-w64-mingw32/share/config.site ]]; then
  echo "ERROR: depends did not produce depends/x86_64-w64-mingw32/share/config.site"
  exit 1
fi

BUILD_DIR="${BUILD_DIR:-$ROOT/build-win64}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [[ ! -f Makefile ]]; then
  echo "==> configure (wallet + Qt, Windows target)..."
  CONFIG_SITE="$ROOT/depends/x86_64-w64-mingw32/share/config.site" \
    CC_FOR_BUILD="${CC_FOR_BUILD}" CXX_FOR_BUILD="${CXX_FOR_BUILD}" \
    "$ROOT/configure" \
    --prefix=/ \
    --disable-ccache \
    --disable-bench \
    --disable-tests \
    --enable-wallet \
    --with-gui=qt5
fi

# WSL: secp256k1 may build gen_context as a Windows .exe but run ./gen_context.
fix_secp256k1_gen_context() {
  local secp="$BUILD_DIR/src/secp256k1"
  local hdr="$secp/src/ecmult_static_context.h"
  [[ -d "$secp" ]] || return 0
  if [[ -f "$hdr" ]] && [[ -x "$secp/gen_context" ]] && file "$secp/gen_context" | grep -q ELF; then
    return 0
  fi
  echo "==> Native gen_context for secp256k1 (WSL cross-compile workaround)..."
  gcc -I"$ROOT/src/secp256k1" -I"$secp/src" -O2 \
    "$ROOT/src/secp256k1/src/gen_context.c" -o "$secp/gen_context"
  (cd "$secp" && ./gen_context)
}

fix_secp256k1_gen_context

# If ../configure becomes newer than $BUILD_DIR/config.status, incremental
# builds may run `./config.status --recheck` without CONFIG_SITE from depends
# and fail with native "Qt5Core not found". Fix: touch that config.status
# (see doc/build-windows.md) then make from BUILD_DIR again.

echo "==> make Qt GUI (quavence-qt.exe)..."
# Top-level Makefile exposes src/qt/quavence-qt.exe, not a bare "quavence-qt" target.
make -j"${JOBS}" src/qt/quavence-qt.exe

echo "Done. Quavence release $(tr -d '\r\n' < "$ROOT/VERSION" 2>/dev/null || echo '?'). Binary:"
ls -la src/qt/quavence-qt.exe
echo "Optional: pack installer with  make deploy  (needs NSIS), or copy tree with  make install DESTDIR=/path"
