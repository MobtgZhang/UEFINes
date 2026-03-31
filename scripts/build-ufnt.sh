#!/usr/bin/env bash
# 一键生成 fonts/UFNT.BIN
# - 中文：Noto Sans CJK SC（黑体/无衬线，OFL）
# - 英文：Noto Sans 可变 TTF（google/fonts ofl/notosans，无衬线拉丁，OFL）
#
# 依赖: Python3 + pillow（pip install pillow）
#
# 用法:
#   ./scripts/build-ufnt.sh
#   UFNT_TTF=/path/cjk.otf UFNT_LATIN_TTF=/path/latin.ttf ./scripts/build-ufnt.sh
#   UFNT_OUT=/tmp/UFNT.BIN ./scripts/build-ufnt.sh
#   UFNT_LATIN_ONLY=1 ./scripts/build-ufnt.sh
#
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CJK_TTF=${UFNT_TTF:-"$ROOT/fonts/NotoSansCJKsc-Regular.otf"}
LATIN_TTF=${UFNT_LATIN_TTF:-"$ROOT/fonts/NotoSans-Latin-VF.ttf"}
OUT=${UFNT_OUT:-"$ROOT/fonts/UFNT.BIN"}
NOTO_URL=${NOTO_URL:-https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf}
DEFAULT_LATIN_URL='https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf'
LATIN_FONT_URL="${LATIN_FONT_URL:-${LIBERATION_URL:-$DEFAULT_LATIN_URL}}"

mkdir -p "$(dirname "$OUT")"
mkdir -p "$(dirname "$CJK_TTF")"
mkdir -p "$(dirname "$LATIN_TTF")"

if ! command -v python3 >/dev/null 2>&1; then
  echo "错误: 需要 python3" >&2
  exit 1
fi

if ! python3 -c "import PIL" >/dev/null 2>&1; then
  echo "错误: 需要 pillow，请执行: pip install pillow" >&2
  exit 1
fi

fetch() {
  local dest=$1
  local url=$2
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL -o "$dest" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$dest" "$url"
  else
    echo "错误: 需要 curl 或 wget" >&2
    exit 1
  fi
}

if [[ ! -f "$CJK_TTF" ]]; then
  echo "未找到 CJK 字体: $CJK_TTF"
  echo "正在下载 Noto Sans CJK SC Regular（OFL）…"
  fetch "$CJK_TTF" "$NOTO_URL"
fi

if [[ ! -f "$LATIN_TTF" ]]; then
  echo "未找到拉丁字体: $LATIN_TTF"
  echo "正在下载 Noto Sans（拉丁无衬线，OFL）…"
  fetch "$LATIN_TTF" "$LATIN_FONT_URL"
fi

GEN=(python3 "$ROOT/scripts/gen_ufnt_bin.py" "$CJK_TTF" "$OUT" --latin-ttf "$LATIN_TTF")
if [[ -n "${UFNT_LATIN_ONLY:-}" ]]; then
  GEN+=(--latin-only)
fi

echo "生成: ${GEN[*]}"
"${GEN[@]}"

echo "完成: $OUT"
echo "可执行: make install  或  ./scripts/qemu-ovmf.sh（若已配置复制 UFNT）"
