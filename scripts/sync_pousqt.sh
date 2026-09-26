#!/usr/bin/env bash
# sync_pousqt.sh
# Sync security-critical files from dao-security-node to dao-security-node-pous-qt.
# Run after any change to netaddress, addrdb, version, or test files.
set -euo pipefail

SRC=/home/dtdtosh/dao-security-node/src
DST=/home/dtdtosh/dao-security-node-pous-qt/src

FILES=(
  version.h
  addrdb.cpp
  netaddress.h
  netaddress.cpp
  Makefile.test.include
  test/audit2_tests.cpp
  test/audit4_stian_tests.cpp
)

for f in "${FILES[@]}"; do
  cp "$SRC/$f" "$DST/$f"
  echo "synced: $f"
done

echo ""
echo "Rebuild the test binary:"
echo "  cd /home/dtdtosh/dao-security-node-pous-qt/build-tests"
echo "  touch ../src/netaddress.h ../src/netaddress.cpp ../src/addrdb.cpp"
echo "  make -C src test/test_blackmore -j4"
