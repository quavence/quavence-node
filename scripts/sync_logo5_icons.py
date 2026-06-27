#!/usr/bin/env python3
"""Sync Logo5/Logo2 branding files into Qt wallet from hub icon pack."""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HUB = Path("/mnt/c/Users/Admin/Desktop/WEB/quavence-dao/quavence_app")
PACK = HUB / "public" / "brand" / "icons"
BRAND = HUB / "public" / "brand"
ICONS = ROOT / "src" / "qt" / "res" / "icons"
PIX = ROOT / "share" / "pixmaps"
BUILD = ROOT / "build-win64"


def main() -> int:
    if not PACK.is_dir():
        print(f"FAIL: missing {PACK}", file=sys.stderr)
        return 1

    # Refresh pack + deploy png/brand from hub tooling
    subprocess.run(
        [sys.executable, "scripts/generate_icon_pack.py", "--deploy", "qt-wallet"],
        cwd=HUB,
        check=True,
    )

    # Logo5 .ico: png_to_ico matches Pillow multi-size embed (hub pack had a broken writer)
    subprocess.run([sys.executable, str(ROOT / "contrib/scripts/png_to_ico.py")], check=True)
    for dst in (
        ICONS / "bitcoin_testnet.ico",
        PIX / "bitcoin.ico",
    ):
        shutil.copy2(ICONS / "bitcoin.ico", dst)
        print(f"copied bitcoin.ico -> {dst}")

    shutil.copy2(PACK / "icon-1024.png", ICONS / "bitcoin.png")
    shutil.copy2(BRAND / "logo2-1024.png", ICONS / "quavence-brand.png")
    print(f"copied icon-1024.png -> {ICONS / 'bitcoin.png'}")
    print(f"copied logo2-1024.png -> {ICONS / 'quavence-brand.png'}")

    for size in (16, 32, 64, 128, 256):
        shutil.copy2(PACK / f"icon-{size}.png", PIX / f"bitcoin{size}.png")
        print(f"copied icon-{size}.png -> {PIX / f'bitcoin{size}.png'}")

    rc = subprocess.run([sys.executable, str(ROOT / "scripts" / "verify_qt_branding.py")])
    if rc.returncode != 0:
        return rc.returncode

    if BUILD.joinpath("Makefile").is_file() and "--skip-rebuild" not in sys.argv:
        print("==> rebuild quavence-qt.exe (embed new .ico)")
        ICONS.joinpath("bitcoin.ico").touch()
        (ROOT / "src/qt/res/bitcoin-qt-res.rc").touch()
        subprocess.run(
            ["make", "-C", str(BUILD), "-j2", "src/qt/quavence-qt.exe"],
            check=True,
        )
        exe = BUILD / "src/qt/quavence-qt.exe"
        print(exe, exe.stat().st_size)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
