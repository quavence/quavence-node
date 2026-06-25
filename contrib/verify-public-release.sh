#!/usr/bin/env bash
# Mandatory gate before publishing user-facing Quavence binaries.
# Fails closed if bootstrap tooling is present or build metadata is wrong.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
VERIFY="${ROOT}/scripts/verify_bootstrap_binaries.py"
SMOKE="${ROOT}/scripts/smoke_bootstrap_build.sh"
CONFIG="${BITCOIN_CONFIG:-$ROOT/src/config/bitcoin-config.h}"

if [[ ! -f "$CONFIG" ]]; then
  echo "verify-public-release: missing $CONFIG — run ./configure (without --enable-bootstrap-tools)" >&2
  exit 1
fi

if grep -q '#define QVNC_ENABLE_BOOTSTRAP_TOOLS 1' "$CONFIG"; then
  echo "verify-public-release: refusing to certify an admin build for public release" >&2
  exit 1
fi

if grep -q '#define CLIENT_VERSION_IS_RELEASE false' "$CONFIG"; then
  echo "verify-public-release: warning — CLIENT_VERSION_IS_RELEASE is false" >&2
fi

shopt -s nullglob
BINS=("$@")
if [[ ${#BINS[@]} -eq 0 ]]; then
  BINS=(
    "$ROOT/src/quavenced"
    "$ROOT/src/quavence-cli"
    "$ROOT/src/quavence-tx"
    "$ROOT/src/qt/quavence-qt"
  )
fi

PRESENT=()
for bin in "${BINS[@]}"; do
  if [[ -f "$bin" ]]; then
    PRESENT+=("$bin")
  fi
done

if [[ ${#PRESENT[@]} -eq 0 ]]; then
  echo "verify-public-release: no binaries found to verify" >&2
  exit 1
fi

echo "verify-public-release: config + smoke"
QVNC_EXPECT_BOOTSTRAP_TOOLS=0 "$SMOKE"

echo "verify-public-release: binary scan (${#PRESENT[@]} file(s))"
"$PYTHON" "$VERIFY" public "${PRESENT[@]}"

echo "verify-public-release: PASSED — safe for end-user distribution"
