#!/usr/bin/env bash
# 优先生成 FAT 镜像 ROMDISK.img；空间不足或 mcopy 失败时改生成 ISO9660（ROMDISK.iso）供 QEMU 挂载。
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM_DIR=${ROM_DIR:-"$ROOT/rom"}
OUT_IMG=${ROM_DISK_IMG:-"$ROM_DIR/ROMDISK.img"}
OUT_ISO=${ROM_DISK_ISO:-"$ROM_DIR/ROMDISK.iso"}

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "缺少命令: $1" >&2
    exit 1
  }
}

need find
need stat

mkdir -p "$ROM_DIR"
ROM_FILE_COUNT=$(find "$ROM_DIR" -maxdepth 1 -type f \( -iname '*.nes' \) | wc -l)
if [[ "$ROM_FILE_COUNT" -eq 0 ]]; then
  echo "未在 $ROM_DIR 顶层找到 .nes，请将 ROM 放入该目录后再执行。" >&2
  exit 1
fi

BYTES=0
while IFS= read -r -d '' f; do
  BYTES=$(( BYTES + $(stat -c%s "$f") ))
done < <(find "$ROM_DIR" -maxdepth 1 -type f \( -iname '*.nes' \) -print0)

if [[ -n "${ROM_DISK_SIZE_MB:-}" ]]; then
  SIZE_MB="$ROM_DISK_SIZE_MB"
else
  SIZE_MB=$(( BYTES / 1048576 + 80 ))
  if [[ "$SIZE_MB" -lt 32 ]]; then
    SIZE_MB=32
  fi
fi

build_iso() {
  local stage
  echo "==> 生成 ISO: $OUT_ISO（约 $ROM_FILE_COUNT 个 ROM，数据约 $(( BYTES / 1048576 ))MB）"
  stage=$(mktemp -d)
  trap 'rm -rf "$stage"' EXIT
  mkdir -p "$stage/roms"
  while IFS= read -r -d '' f; do
    cp -f "$f" "$stage/roms/$(basename "$f")"
  done < <(find "$ROM_DIR" -maxdepth 1 -type f \( -iname '*.nes' \) -print0)
  rm -f "$OUT_IMG"
  if command -v xorriso >/dev/null 2>&1; then
    xorriso -as mkisofs -o "$OUT_ISO" -V UEFINESROM -R -J -l "$stage"
  elif command -v genisoimage >/dev/null 2>&1; then
    genisoimage -o "$OUT_ISO" -V UEFINESROM -r -J -l "$stage" >/dev/null
  else
    echo "生成 ISO 需要安装 xorriso 或 genisoimage（例如: apt install xorriso）" >&2
    exit 1
  fi
  trap - EXIT
  rm -rf "$stage"
  echo "=> 完成: $OUT_ISO（根目录 roms/，QEMU 以光盘方式挂载）"
}

try_img() {
  need truncate
  need mkfs.vfat
  need mcopy
  echo "==> 尝试 FAT 镜像 ${SIZE_MB}MB: $OUT_IMG（约 $ROM_FILE_COUNT 个 ROM，数据约 $(( BYTES / 1048576 ))MB）"
  rm -f "$OUT_ISO"
  truncate -s "${SIZE_MB}M" "$OUT_IMG" || return 1
  # 大卷用 FAT32，避免 FAT16 容量与簇限制
  if [[ "$SIZE_MB" -le 260 ]]; then
    mkfs.vfat -F 16 -n UEFINESROM "$OUT_IMG" >/dev/null || return 1
  else
    mkfs.vfat -F 32 -n UEFINESROM "$OUT_IMG" >/dev/null || return 1
  fi
  export MTOOLS_SKIP_CHECK=1
  mmd -i "$OUT_IMG" ::roms || return 1
  while IFS= read -r -d '' f; do
    base=$(basename "$f")
    echo "    $base"
    mcopy -i "$OUT_IMG" -o "$f" "::roms/$base" || return 1
  done < <(find "$ROM_DIR" -maxdepth 1 -type f \( -iname '*.nes' \) -print0)
  echo "=> 完成: $OUT_IMG（卷内 ::/roms/*.nes）"
  return 0
}

if try_img; then
  exit 0
fi

echo "FAT 镜像写入失败或空间不足，改生成 ISO…" >&2
rm -f "$OUT_IMG"
build_iso
