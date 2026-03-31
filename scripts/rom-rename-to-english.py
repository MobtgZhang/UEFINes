#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""
Rename rom/*.nes to:  NNN English Title With Spaces.nes

Uses scripts/rom_english_titles.txt (262 lines, line N = title for index N).
Accepts current names:  NNN_anything.nes  (three-digit index prefix).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROM_DIR = SCRIPT_DIR.parent / "rom"
TITLES_FILE = SCRIPT_DIR / "rom_english_titles.txt"
CUR_RE = re.compile(r"^(\d{3})_.+\.nes$", re.IGNORECASE)
# FAT / Windows forbidden in filenames
BAD_CHARS = '<>:"/\\|?*'


def load_titles() -> list[str]:
    raw = TITLES_FILE.read_text(encoding="utf-8")
    lines = [ln.strip() for ln in raw.splitlines()]
    lines = [ln for ln in lines if ln]
    if len(lines) != 262:
        sys.exit(f"Expected 262 non-empty lines in {TITLES_FILE}, got {len(lines)}")
    return lines


def safe_title(s: str) -> str:
    t = "".join(ch if ch not in BAD_CHARS else " " for ch in s)
    t = re.sub(r" +", " ", t).strip()
    return t


def main() -> int:
    if not ROM_DIR.is_dir():
        print(f"Missing {ROM_DIR}", file=sys.stderr)
        return 1

    titles = load_titles()
    work: list[tuple[Path, str]] = []

    for p in sorted(ROM_DIR.iterdir(), key=lambda x: x.name):
        if not p.is_file() or p.suffix.lower() != ".nes":
            continue
        m = CUR_RE.match(p.name)
        if not m:
            print(f"Skip (need NNN_slug.nes): {p.name}", file=sys.stderr)
            continue
        n = int(m.group(1))
        if n < 1 or n > 262:
            print(f"Skip (index out of range): {p.name}", file=sys.stderr)
            continue
        title = safe_title(titles[n - 1])
        new_name = f"{n:03d} {title}.nes"
        if p.name == new_name:
            continue
        dest = ROM_DIR / new_name
        if dest.exists() and dest.resolve() != p.resolve():
            print(f"Target exists: {new_name}", file=sys.stderr)
            return 1
        work.append((p, new_name))

    for i, (src, _) in enumerate(work):
        src.rename(ROM_DIR / f".__romren_{i:05d}.nes")

    for i, (_, new_name) in enumerate(work):
        mid = ROM_DIR / f".__romren_{i:05d}.nes"
        mid.rename(ROM_DIR / new_name)

    print(f"Renamed {len(work)} files under {ROM_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
