#!/usr/bin/env bash
# Smoke checks for public vs admin bootstrap tooling compile split.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${BITCOIN_CONFIG:-$ROOT/src/config/bitcoin-config.h}"
EXPECT="${QVNC_EXPECT_BOOTSTRAP_TOOLS:?set QVNC_EXPECT_BOOTSTRAP_TOOLS to 0 or 1}"
PYTHON="${PYTHON:-python3}"

fail() {
  echo "smoke_bootstrap_build: FAIL — $*" >&2
  exit 1
}

if [[ ! -f "$CONFIG" ]]; then
  fail "missing $CONFIG (run ./autogen.sh && ./configure first)"
fi

if grep -q '#define QVNC_ENABLE_BOOTSTRAP_TOOLS 1' "$CONFIG"; then
  found=1
elif grep -q '#define QVNC_ENABLE_BOOTSTRAP_TOOLS 0' "$CONFIG"; then
  found=0
else
  fail "QVNC_ENABLE_BOOTSTRAP_TOOLS not defined in $CONFIG"
fi

if [[ "$found" != "$EXPECT" ]]; then
  fail "config header: expected QVNC_ENABLE_BOOTSTRAP_TOOLS=$EXPECT, got $found"
fi
echo "config header: QVNC_ENABLE_BOOTSTRAP_TOOLS=$found (ok)"

CLI="${QUAVENCE_CLI:-}"
if [[ -n "$CLI" ]]; then
  if [[ ! -x "$CLI" ]]; then
    fail "QUAVENCE_CLI is not executable: $CLI"
  fi
  help_out="$("$CLI" help 2>&1 || true)"
  if [[ "$EXPECT" == "1" ]]; then
    echo "$help_out" | grep -q 'generatebootstrap' || fail "admin build: generatebootstrap missing from RPC help"
    echo "RPC help: generatebootstrap present (ok)"
  else
    echo "$help_out" | grep -q 'generatebootstrap' && fail "public build: generatebootstrap must not appear in RPC help"
    echo "RPC help: generatebootstrap absent (ok)"
  fi

  netinfo="$("$CLI" getnetworkinfo 2>&1 || true)"
  if [[ "$EXPECT" == "1" ]]; then
    echo "$netinfo" | grep -q '"build_type": "admin"' || fail "admin build: getnetworkinfo.build_type != admin"
    echo "$netinfo" | grep -q '"bootstrap_tools": "enabled"' || fail "admin build: bootstrap_tools != enabled"
  else
    echo "$netinfo" | grep -q '"build_type": "public"' || fail "public build: getnetworkinfo.build_type != public"
    echo "$netinfo" | grep -q '"bootstrap_tools": "disabled"' || fail "public build: bootstrap_tools != disabled"
  fi
  echo "getnetworkinfo build markers (ok)"
fi

DAEMON="${QUAVENCED:-}"
if [[ -n "$DAEMON" && -x "$DAEMON" ]]; then
  variant=public
  [[ "$EXPECT" == "1" ]] && variant=admin
  "$PYTHON" "$ROOT/scripts/verify_bootstrap_binaries.py" "$variant" "$DAEMON"
fi

echo "smoke_bootstrap_build: OK (expect=$EXPECT)"
