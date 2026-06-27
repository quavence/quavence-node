#!/usr/bin/env python3
"""Rasterize filled miniapp toolbar SVGs to monochrome PNG for Qt wallet."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src" / "qt" / "res" / "icons"
SRC_DIRS = [
    ROOT / "src" / "qt" / "res" / "icons" / "toolbar-src",
    Path("/mnt/c/Users/Admin/Desktop/WEB/quavence-dao/design/qt-wallet-toolbar-icons/icons/filled"),
    Path(r"C:\Users\Admin\Desktop\WEB\quavence-dao\design\qt-wallet-toolbar-icons\icons\filled"),
]
SIZE = 40
MAP = {
    "overview": "overview.png",
    "send": "send.png",
    "receive": "receive.png",
    "transactions": "history.png",
}


def find_src_dir() -> Path:
    for d in SRC_DIRS:
        if (d / "overview.svg").is_file():
            return d
    raise SystemExit(f"FAIL: filled SVG source not found. Tried: {SRC_DIRS}")


def svg_bytes(svg_path: Path) -> bytes:
    text = svg_path.read_text(encoding="utf-8")
    text = text.replace("currentColor", "#000000")
    if 'fill="#000000"' not in text and "fill=" not in text:
        text = text.replace("<path ", '<path fill="#000000" ', 1)
    return text.encode("utf-8")


def render_cairosvg(svg_path: Path, out_path: Path) -> None:
    import cairosvg

    cairosvg.svg2png(
        bytestring=svg_bytes(svg_path),
        write_to=str(out_path),
        output_width=SIZE,
        output_height=SIZE,
    )


def render_rsvg(svg_path: Path, out_path: Path) -> None:
    tmp = out_path.with_suffix(".tmp.svg")
    tmp.write_bytes(svg_bytes(svg_path))
    try:
        subprocess.run(
            ["rsvg-convert", "-w", str(SIZE), "-h", str(SIZE), "-o", str(out_path), str(tmp)],
            check=True,
        )
    finally:
        tmp.unlink(missing_ok=True)


def render_inkscape(svg_path: Path, out_path: Path) -> None:
    tmp = out_path.with_suffix(".tmp.svg")
    tmp.write_bytes(svg_bytes(svg_path))
    try:
        subprocess.run(
            [
                "inkscape",
                str(tmp),
                f"--export-width={SIZE}",
                f"--export-height={SIZE}",
                f"--export-filename={out_path}",
            ],
            check=True,
        )
    finally:
        tmp.unlink(missing_ok=True)


def ensure_renderer():
    for mod in ("cairosvg",):
        try:
            __import__(mod)
            return render_cairosvg
        except ImportError:
            try:
                import subprocess
                subprocess.check_call(
                    [sys.executable, "-m", "pip", "install", "cairosvg", "--user", "-q"],
                    stderr=subprocess.DEVNULL,
                )
                __import__(mod)
                return render_cairosvg
            except Exception:
                pass
    for name, fn in (("rsvg-convert", render_rsvg), ("inkscape", render_inkscape)):
        try:
            subprocess.run([name, "--version"], capture_output=True, check=True)
            return fn
        except Exception:
            continue
    return None


def main() -> int:
    src = find_src_dir()
    OUT.mkdir(parents=True, exist_ok=True)
    render = ensure_renderer()
    if render is None:
        print("Toolbar icons are drawn in C++ (brandToolbarIcon).", file=sys.stderr)
        print("Optional: pip install cairosvg, then re-run to refresh menu PNGs.", file=sys.stderr)
        return 0

    print(f"source: {src}")
    print(f"renderer: {render.__name__}")
    for stem, out_name in MAP.items():
        svg_path = src / f"{stem}.svg"
        out_path = OUT / out_name
        render(svg_path, out_path)
        print(f"  {svg_path.name} -> {out_path} ({out_path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
