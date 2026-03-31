#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
#
# 生成 UFNT.BIN：默认中文用无衬线 CJK、英文用无衬线拉丁（可 --latin-ttf 指定）。
#
# 依赖: pip install pillow
# 示例:
#   ./scripts/gen_ufnt_bin.py /path/NotoSansCJKsc-Regular.otf fonts/UFNT.BIN \
#     --latin-ttf /path/NotoSans-Latin-VF.ttf

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("请先安装: pip install pillow", file=sys.stderr)
    sys.exit(1)

UFNT_MAGIC = 0x314E4655
UFNT_VER = 1
CELL = 16
# 高倍栅格 + BOX 缩小；不用 MedianFilter（易把中文糊成一团）
SUPER_SAMPLE = 5
THRESH = 136


def raster_char(font_big: ImageFont.FreeTypeFont, ch: str) -> bytes:
    big = CELL * SUPER_SAMPLE
    im = Image.new("L", (big, big), 0)
    dr = ImageDraw.Draw(im)
    bbox = dr.textbbox((0, 0), ch, font=font_big)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    ox = (big - tw) // 2 - bbox[0]
    oy = (big - th) // 2 - bbox[1]
    dr.text((ox, oy), ch, font=font_big, fill=255)
    try:
        _box = Image.Resampling.BOX
    except AttributeError:
        _box = Image.BOX
    im = im.resize((CELL, CELL), _box)
    out = bytearray(32)
    for y in range(CELL):
        wrow = 0
        for x in range(CELL):
            bit = 1 if im.getpixel((x, y)) >= THRESH else 0
            wrow = (wrow << 1) | bit
        out[y * 2] = (wrow >> 8) & 0xFF
        out[y * 2 + 1] = wrow & 0xFF
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser(description="生成 UEFINes UFNT.BIN（CJK + 拉丁无衬线）")
    ap.add_argument("cjk_ttf", type=Path, help="中文主字体 .ttf/.otf（如 Noto Sans CJK SC）")
    ap.add_argument("out", type=Path, help="输出 UFNT.BIN")
    ap.add_argument(
        "--latin-ttf",
        type=Path,
        default=None,
        help="英文无衬线 TTF（如 Noto Sans VF）；默认同 cjk_ttf",
    )
    ap.add_argument("--size", type=int, default=12, help="栅格字号基数（默认 12，略小于格内显得清晰）")
    ap.add_argument("--latin-only", action="store_true", help="仅 U+0020–007E（仅用拉丁字体）")
    args = ap.parse_args()

    if not args.cjk_ttf.is_file():
        print(f"找不到 CJK 字体: {args.cjk_ttf}", file=sys.stderr)
        sys.exit(1)

    latin_path = args.latin_ttf if args.latin_ttf is not None else args.cjk_ttf
    if not latin_path.is_file():
        print(f"找不到拉丁字体: {latin_path}", file=sys.stderr)
        sys.exit(1)

    font_latin = ImageFont.truetype(str(latin_path), args.size * SUPER_SAMPLE)
    if args.latin_only:
        font_cjk = font_latin
    else:
        font_cjk = ImageFont.truetype(str(args.cjk_ttf), args.size * SUPER_SAMPLE)

    codepoints: list[int] = []
    if args.latin_only:
        codepoints.extend(range(0x20, 0x7F))
    else:
        codepoints.extend(range(0x20, 0x7F))
        codepoints.extend(range(0x4E00, 0xA000))

    glyphs: list[tuple[int, bytes]] = []
    seen: set[int] = set()
    for cp in codepoints:
        if cp in seen:
            continue
        seen.add(cp)
        ch = chr(cp)
        try:
            if 0x20 <= cp < 0x7F:
                bmp = raster_char(font_latin, ch)
            else:
                bmp = raster_char(font_cjk, ch)
        except Exception:
            continue
        if bmp == bytes(32) and cp != 0x20:
            continue
        glyphs.append((cp, bmp))

    if 0x20 not in {g[0] for g in glyphs}:
        glyphs.append((0x20, bytes(32)))

    glyphs.sort(key=lambda x: x[0])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as f:
        f.write(struct.pack("<III", UFNT_MAGIC, UFNT_VER, len(glyphs)))
        for cp, bmp in glyphs:
            f.write(struct.pack("<I", cp))
            f.write(bmp)
    print(f"写入 {args.out}：{len(glyphs)} 个字形（拉丁: {latin_path.name}，CJK: {args.cjk_ttf.name}）")


if __name__ == "__main__":
    main()
