#!/usr/bin/env python3
"""Fail closed if binaries violate public/admin bootstrap tooling policy."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

FORBIDDEN_PUBLIC_STRINGS = (
    "generatebootstrap",
    "ThreadPowBootstrapMiner",
    "QVNC_BUILD_VARIANT=admin",
)

REQUIRED_PUBLIC_STRINGS = (
    "QVNC_BUILD_VARIANT=public",
)

FORBIDDEN_PUBLIC_SYMBOLS = (
    "generatebootstrap",
    "ThreadPowBootstrapMiner",
)

REQUIRED_ADMIN_STRINGS = (
    "QVNC_BUILD_VARIANT=admin",
)

REQUIRED_ADMIN_SYMBOLS = (
    "generatebootstrap",
    "ThreadPowBootstrapMiner",
)


def _read_strings(path: Path) -> str:
    try:
        out = subprocess.check_output(["strings", str(path)], stderr=subprocess.DEVNULL)
        return out.decode("utf-8", errors="replace")
    except (FileNotFoundError, subprocess.CalledProcessError):
        data = path.read_bytes()
        return data.decode("utf-8", errors="replace")


def _read_nm(path: Path) -> str:
    try:
        out = subprocess.check_output(["nm", "-C", str(path)], stderr=subprocess.DEVNULL)
        return out.decode("utf-8", errors="replace")
    except (FileNotFoundError, subprocess.CalledProcessError):
        return ""


def verify_public(path: Path) -> list[str]:
    errors: list[str] = []
    text = _read_strings(path)
    nm = _read_nm(path)

    for needle in REQUIRED_PUBLIC_STRINGS:
        if needle not in text:
            errors.append(f"missing required string {needle!r}")

    for needle in FORBIDDEN_PUBLIC_STRINGS:
        if needle in text:
            errors.append(f"forbidden string {needle!r}")

    for sym in FORBIDDEN_PUBLIC_SYMBOLS:
        if sym in nm:
            errors.append(f"forbidden symbol {sym!r}")

    return errors


def verify_admin(path: Path, require_bootstrap_symbols: bool) -> list[str]:
    errors: list[str] = []
    text = _read_strings(path)
    nm = _read_nm(path)

    for needle in REQUIRED_ADMIN_STRINGS:
        if needle not in text:
            errors.append(f"missing required string {needle!r}")

    if require_bootstrap_symbols:
        for sym in REQUIRED_ADMIN_SYMBOLS:
            if sym not in nm and sym not in text:
                errors.append(f"missing required bootstrap symbol/string {sym!r}")

    return errors


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(
            "usage: verify_bootstrap_binaries.py public|admin BIN [BIN...]\n"
            "  public  — user-facing binaries must not contain bootstrap tooling\n"
            "  admin   — operator binaries must expose bootstrap tooling",
            file=sys.stderr,
        )
        return 2

    variant = argv[1]
    if variant not in ("public", "admin"):
        print(f"unknown variant {variant!r}", file=sys.stderr)
        return 2

    failed = False
    for raw in argv[2:]:
        path = Path(raw)
        if not path.is_file():
            print(f"FAIL {path}: not a file", file=sys.stderr)
            failed = True
            continue

        name = path.name
        require_bootstrap_symbols = variant == "admin" and (
            "quavenced" in name or "quavence-qt" in name or "blackmore" in name
        )

        if variant == "public":
            errors = verify_public(path)
        else:
            errors = verify_admin(path, require_bootstrap_symbols)

        if errors:
            failed = True
            print(f"FAIL {path}:", file=sys.stderr)
            for err in errors:
                print(f"  - {err}", file=sys.stderr)
        else:
            print(f"OK {path} ({variant})")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
