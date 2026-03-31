#!/usr/bin/env bash
# QEMU + OVMF：默认使用本项目 esp/（EFI 引导）与 rom/（262 套 ROM）两块虚拟 FAT 盘。
# 用法：
#   ./scripts/qemu-ovmf.sh              # 推荐：esp + rom
#   ./scripts/qemu-ovmf.sh /path/disk.img   # 仍支持自定义 raw 镜像（单盘）
#
# 若 esp/EFI/Boot/bootx64.efi 不存在，会尝试从本仓库 edk2 构建产物自动复制（与 make install 同源）。
# 可显式指定：UEFINES_EFI=/path/Uefines.efi ./scripts/qemu-ovmf.sh
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ESP_DIR=${ESP_DIR:-"$ROOT/esp"}
ROM_DIR=${ROM_DIR:-"$ROOT/rom"}
OVMF_CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
OVMF_VARS_SRC=${OVMF_VARS_SRC:-/usr/share/OVMF/OVMF_VARS_4M.fd}
VARSTORE_DIR="$ROOT/varstore"
VARSTORE_FILE="$VARSTORE_DIR/OVMF_VARS.fd"
# QEMU fat: 虚拟盘根目录项有限；ROM 过多时需经单层子目录暴露（见 prepare_rom_fat_staging）
ROM_FAT_ROOT=${ROM_FAT_ROOT:-"$VARSTORE_DIR/qemu-rom-fat"}
# 若存在 make img 产物：优先 FAT 镜像；否则 ISO；再否则 fat:rw: 目录
ROM_DISK_IMG=${ROM_DISK_IMG:-"$ROM_DIR/ROMDISK.img"}
ROM_DISK_ISO=${ROM_DISK_ISO:-"$ROM_DIR/ROMDISK.iso"}

if [[ ! -f "$OVMF_CODE" || ! -f "$OVMF_VARS_SRC" ]]; then
  echo "未找到 OVMF：请安装 ovmf 包，或通过环境变量设置 OVMF_CODE / OVMF_VARS_SRC" >&2
  exit 1
fi

mkdir -p "$VARSTORE_DIR"
if [[ ! -f "$VARSTORE_FILE" ]]; then
  cp -f "$OVMF_VARS_SRC" "$VARSTORE_FILE"
  echo "已初始化可写 NVRAM：$VARSTORE_FILE"
fi

if [[ $# -ge 1 ]]; then
  IMG=$1
  if [[ ! -f "$IMG" ]]; then
    echo "镜像不存在: $IMG" >&2
    exit 1
  fi
  exec qemu-system-x86_64 \
    -machine q35 \
    -m 512 \
    -boot order=d,menu=off \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE" \
    -drive "if=pflash,format=raw,file=$VARSTORE_FILE" \
    -drive "file=$IMG,format=raw,if=virtio" \
    -net none
fi

# 默认：双 virtio FAT（与 UEFINes 内多卷枚举一致，ROM 在第二盘根目录）
if [[ ! -d "$ESP_DIR" ]]; then
  mkdir -p "$ESP_DIR/EFI/Boot"
  echo "已创建 ESP 目录: $ESP_DIR（可再执行 make install 放入 bootx64.efi）" >&2
fi

BOOTX64="$ESP_DIR/EFI/Boot/bootx64.efi"
if [[ ! -f "$BOOTX64" ]]; then
  src=""
  if [[ -n "${UEFINES_EFI:-}" && -f "$UEFINES_EFI" ]]; then
    src=$UEFINES_EFI
  elif [[ -n "${EDK2_WORKSPACE:-}" ]]; then
    for cand in \
      "$EDK2_WORKSPACE/Build/Uefines/RELEASE_GCC/X64/Uefines.efi" \
      "$EDK2_WORKSPACE/Build/Uefines/DEBUG_GCC/X64/Uefines.efi"; do
      if [[ -f "$cand" ]]; then
        src=$cand
        break
      fi
    done
  elif [[ -d "$ROOT/edk2/MdePkg" ]]; then
    for cand in \
      "$ROOT/edk2/Build/Uefines/RELEASE_GCC/X64/Uefines.efi" \
      "$ROOT/edk2/Build/Uefines/DEBUG_GCC/X64/Uefines.efi"; do
      if [[ -f "$cand" ]]; then
        src=$cand
        break
      fi
    done
  fi
  if [[ -n "$src" ]]; then
    mkdir -p "$(dirname "$BOOTX64")"
    cp -f "$src" "$BOOTX64"
    echo "已自动安装引导：$src -> $BOOTX64"
  else
    echo "未找到 $BOOTX64，且未探测到 edk2 下的 Uefines.efi（请先 make 或 make install）。" >&2
    echo "亦可手动复制，或设置 UEFINES_EFI 指向构建好的 Uefines.efi（见 esp/EFI/Boot/README.txt）。" >&2
    exit 1
  fi
fi

if [[ -f "$ROOT/fonts/UFNT.BIN" ]]; then
  mkdir -p "$ESP_DIR/EFI/Boot/Fonts"
  cp -f "$ROOT/fonts/UFNT.BIN" "$ESP_DIR/EFI/Boot/Fonts/UFNT.BIN"
fi

if [[ ! -d "$ROM_DIR" ]]; then
  echo "缺少 ROM 目录: $ROM_DIR" >&2
  exit 1
fi

ROM_COUNT=$(find "$ROM_DIR" -maxdepth 1 -type f \( -iname '*.nes' -o -iname '*.NES' \) | wc -l)

# 将 $ROM_DIR 下顶层 .nes 硬链接（失败则复制）到 $ROM_FAT_ROOT/roms/，使 fat: 根目录只有 roms 一项，规避
# “Too many entries in root directory”（QEMU 对目录型 FAT 的根项数限制）。
prepare_rom_fat_staging() {
  local dst="$ROM_FAT_ROOT/roms"
  mkdir -p "$dst"
  find "$dst" -mindepth 1 -maxdepth 1 -exec rm -f {} +
  local f base
  while IFS= read -r -d '' f; do
    base=$(basename "$f")
    if ! ln -f "$f" "$dst/$base" 2>/dev/null; then
      cp -f "$f" "$dst/$base"
    fi
  done < <(find "$ROM_DIR" -maxdepth 1 -type f \( -iname '*.nes' \) -print0)
}

echo "ESP: $ESP_DIR"
if [[ -f "$ROM_DISK_IMG" ]]; then
  ROM_DRIVE_SPEC="file=$ROM_DISK_IMG,format=raw,if=virtio,index=1,media=disk"
  echo "ROM: 使用 FAT 镜像 $ROM_DISK_IMG（约 $ROM_COUNT 个 .nes 源文件在 $ROM_DIR；更新 ROM 后请重新 make img）"
elif [[ -f "$ROM_DISK_ISO" ]]; then
  ROM_DRIVE_SPEC="file=$ROM_DISK_ISO,format=raw,if=virtio,index=1,media=cdrom,readonly=on"
  echo "ROM: 使用 ISO $ROM_DISK_ISO（FAT 镜像不足或失败时的回退；约 $ROM_COUNT 个 .nes 在 $ROM_DIR）"
else
  prepare_rom_fat_staging
  ROM_DRIVE_SPEC="file=fat:rw:$ROM_FAT_ROOT,format=raw,if=virtio,index=1,media=disk"
  echo "ROM: $ROM_DIR → 虚拟盘 $ROM_FAT_ROOT（根目录 roms/，约 $ROM_COUNT 个 .nes）；可选 make img 生成 $ROM_DISK_IMG 或 $ROM_DISK_ISO"
fi
echo "提示: 若键盘无响应，请用鼠标点一下 QEMU 窗口使其获得焦点。"

# OVMF + q35：用 XHCI + usb-kbd 提供 ConIn（比默认 PS/2 在 QEMU 图形窗口里更稳）。
exec qemu-system-x86_64 \
  -machine q35 \
  -m 512 \
  -boot order=d,menu=off \
  -device qemu-xhci,id=xhci \
  -device usb-kbd,bus=xhci.0 \
  -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE" \
  -drive "if=pflash,format=raw,file=$VARSTORE_FILE" \
  -drive "file=fat:rw:$ESP_DIR,format=raw,if=virtio,index=0,media=disk" \
  -drive "$ROM_DRIVE_SPEC" \
  -net none
