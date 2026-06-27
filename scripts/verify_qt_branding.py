#!/usr/bin/env python3
"""Verify Qt wallet branding assets (Logo5 icon, Logo2-centered splash)."""
from __future__ import annotations

import struct
import sys
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ICON_PNG = ROOT / "src/qt/res/icons/bitcoin.png"
BRAND_PNG = ROOT / "src/qt/res/icons/quavence-brand.png"
ICON_ICO = ROOT / "src/qt/res/icons/bitcoin.ico"
TESTNET_ICO = ROOT / "src/qt/res/icons/bitcoin_testnet.ico"
ICON_ICNS = ROOT / "src/qt/res/icons/bitcoin.icns"
PIXMAP_ICO = ROOT / "share/pixmaps/bitcoin.ico"
PACK_FAVICON = Path("/mnt/c/Users/Admin/Desktop/WEB/quavence-dao/quavence_app/public/brand/icons/favicon.ico")
PACK_ICON_1024 = Path("/mnt/c/Users/Admin/Desktop/WEB/quavence-dao/quavence_app/public/brand/icons/icon-1024.png")
PACK_DIR = PACK_FAVICON.parent if PACK_FAVICON.parent.is_dir() else None
LOGO2_REF = Path("/mnt/c/Users/Admin/Desktop/WEB/quavence-dao/quavence_app/public/brand/logo2-1024.png")
PIXMAP_SIZES = (16, 32, 64, 128, 256)


def ico_sizes(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) < 6 or data[:2] != b"\x00\x00":
        return []
    count = struct.unpack_from("<H", data, 4)[0]
    sizes: list[int] = []
    for i in range(count):
        off = 6 + i * 16
        w, h = data[off], data[off + 1]
        sizes.append(256 if w == 0 else w)
    return sizes


def main() -> int:
    try:
        from PIL import Image
    except ImportError:
        print("FAIL: Pillow required", file=sys.stderr)
        return 1

    errors: list[str] = []

    if not ICON_PNG.is_file():
        errors.append(f"missing {ICON_PNG}")
    else:
        icon = Image.open(ICON_PNG)
        if icon.size != (1024, 1024):
            errors.append(f"bitcoin.png expected 1024x1024, got {icon.size}")
        if icon.mode != "RGBA":
            errors.append(f"bitcoin.png expected RGBA, got {icon.mode}")

    if not BRAND_PNG.is_file():
        errors.append(f"missing {BRAND_PNG}")
    else:
        brand = Image.open(BRAND_PNG)
        if brand.size[0] != brand.size[1]:
            errors.append(f"quavence-brand.png must be square, got {brand.size}")
        if brand.size[0] < 512:
            errors.append(f"quavence-brand.png too small: {brand.size}")

    for ico_path in (ICON_ICO, PIXMAP_ICO):
        if not ico_path.is_file():
            errors.append(f"missing {ico_path}")
            continue
        if ico_path.stat().st_size < 4096:
            errors.append(f"{ico_path} suspiciously small ({ico_path.stat().st_size} bytes)")
        sizes = ico_sizes(ico_path)
        if not sizes:
            errors.append(f"{ico_path} is not a valid ICO")
        elif max(sizes) < 256:
            errors.append(f"{ico_path} missing 256px layer (have {sizes})")

    if PIXMAP_ICO.is_file() and ICON_ICO.is_file():
        if PIXMAP_ICO.read_bytes() != ICON_ICO.read_bytes():
            errors.append("share/pixmaps/bitcoin.ico differs from src/qt/res/icons/bitcoin.ico")

    if TESTNET_ICO.is_file() and ICON_ICO.is_file():
        if TESTNET_ICO.read_bytes() != ICON_ICO.read_bytes():
            errors.append("bitcoin_testnet.ico differs from bitcoin.ico")

    if PACK_ICON_1024.is_file() and ICON_PNG.is_file():
        if ICON_PNG.read_bytes() != PACK_ICON_1024.read_bytes():
            errors.append("bitcoin.png differs from hub icon-1024.png (Logo5 pack)")

    if LOGO2_REF.is_file() and BRAND_PNG.is_file():
        if BRAND_PNG.read_bytes() != LOGO2_REF.read_bytes():
            errors.append("quavence-brand.png differs from hub logo2-1024.png")

    if PACK_DIR:
        for size in PIXMAP_SIZES:
            src = PACK_DIR / f"icon-{size}.png"
            dst = ROOT / "share/pixmaps" / f"bitcoin{size}.png"
            if not src.is_file() or not dst.is_file():
                continue
            if dst.read_bytes() != src.read_bytes():
                errors.append(f"share/pixmaps/bitcoin{size}.png is not Logo5 pack icon-{size}.png")
                break

    if ICON_ICNS.is_file() and PACK_ICON_1024.is_file():
        if ICON_ICNS.stat().st_mtime < PACK_ICON_1024.stat().st_mtime:
            if shutil.which("png2icns"):
                errors.append(
                    "bitcoin.icns is older than Logo5 pack — run scripts/sync_logo5_icons.py"
                )
            else:
                print(
                    "WARN: bitcoin.icns not refreshed (install icnsutils for macOS builds)",
                    file=sys.stderr,
                )

    if errors:
        for e in errors:
            print(f"FAIL: {e}", file=sys.stderr)
        return 1

    print("OK: Qt branding assets")
    print(f"  icon (Logo5): {ICON_PNG} {Image.open(ICON_PNG).size}")
    print(f"  splash (Logo2): {BRAND_PNG} {Image.open(BRAND_PNG).size}")
    print(f"  ico layers: {ico_sizes(ICON_ICO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
