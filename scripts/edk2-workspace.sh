#!/usr/bin/env bash
# 解析 EDK II（edk2）工作区绝对路径，打印到标准输出一行。
#
# 顺序：
#   1) 环境变量 EDK2_WORKSPACE（若已设置且有效）
#   2) 依次探测候选目录（含本仓库下的 edk2/）
#   3) 若仍没有：自动 git clone 到「本项目/edk2」并打印该路径
#
# 用法:
#   EDK2_WORKSPACE=$(./scripts/edk2-workspace.sh)
#   ./scripts/edk2-workspace.sh --resolve-only   # 仅探测已有 edk2，不 git clone（供 make clean 等）
#   ./scripts/edk2-workspace.sh --help
#
# 可选环境变量：
#   EDK2_GIT_URL   克隆地址（默认 https://github.com/tianocore/edk2.git）
#   EDK2_CLONE_DEPTH  浅克隆深度（默认 1；设为 0 表示完整克隆）

set -euo pipefail

UEFINES_ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOCAL_EDK2="$UEFINES_ROOT/edk2"
EDK2_GIT_URL=${EDK2_GIT_URL:-https://github.com/tianocore/edk2.git}
EDK2_CLONE_DEPTH=${EDK2_CLONE_DEPTH:-1}

usage() {
  sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

is_valid_edk2() {
  local d=$1
  [[ -n "$d" ]] || return 1
  [[ -d "$d/MdePkg" ]] || return 1
  [[ -f "$d/edksetup.sh" ]] || return 1
  return 0
}

try_print() {
  local d=$1
  if is_valid_edk2 "$d"; then
    cd "$d" && pwd
    return 0
  fi
  return 1
}

if [[ "${1:-}" == "--resolve-only" ]]; then
  if [[ -n "${EDK2_WORKSPACE:-}" ]]; then
    try_print "$EDK2_WORKSPACE" && exit 0
    exit 0
  fi

  PARENT=$(cd "$UEFINES_ROOT/.." && pwd)
  CANDIDATES_RESOLVE=(
    "$LOCAL_EDK2"
    "$PARENT/edk2"
    "$HOME/github/edk2"
    "$HOME/src/edk2"
    "$HOME/edk2"
    "/usr/local/src/edk2"
    "/opt/edk2"
  )

  for d in "${CANDIDATES_RESOLVE[@]}"; do
    [[ -d "$d" ]] || continue
    if try_print "$d"; then
      exit 0
    fi
  done

  exit 0
fi

# 浅克隆常缺 MdePkg.dec 引用的子模块目录，补空目录以便本仓库能编过 MdePkg
ensure_mipisyst_stub() {
  local root=$1
  local stub="$root/MdePkg/Library/MipiSysTLib/mipisyst/library/include"
  if [[ ! -d "$stub" ]]; then
    echo "edk2-workspace: 补充 MipiSysT 包含路径占位: $stub" >&2
    mkdir -p "$stub"
    # 空文件即可满足 -I 路径存在
    : >"$stub/.gitkeep"
  fi
}

clone_local_edk2() {
  if ! command -v git >/dev/null 2>&1; then
    echo "edk2-workspace: 未找到 git，无法自动克隆 edk2。" >&2
    return 1
  fi

  if [[ -e "$LOCAL_EDK2" ]]; then
    echo "edk2-workspace: 目录已存在但不是有效 edk2，请删除后重试：" >&2
    echo "  rm -rf \"$LOCAL_EDK2\"" >&2
    return 1
  fi

  echo "edk2-workspace: 正在克隆 edk2 到 \"$LOCAL_EDK2\" …" >&2
  local depth_args=()
  if [[ -n "$EDK2_CLONE_DEPTH" && "$EDK2_CLONE_DEPTH" != "0" ]]; then
    depth_args=(--depth "$EDK2_CLONE_DEPTH")
  fi

  git clone "${depth_args[@]}" "$EDK2_GIT_URL" "$LOCAL_EDK2"
  ensure_mipisyst_stub "$LOCAL_EDK2"
}

if [[ -n "${EDK2_WORKSPACE:-}" ]]; then
  if try_print "$EDK2_WORKSPACE"; then
    exit 0
  fi
  echo "edk2-workspace: 环境变量 EDK2_WORKSPACE 无效（需含 MdePkg 与 edksetup.sh）: $EDK2_WORKSPACE" >&2
  exit 1
fi

PARENT=$(cd "$UEFINES_ROOT/.." && pwd)

# 本仓库 edk2/ 优先于其它路径，便于「开箱」在同一项目内构建
CANDIDATES=(
  "$LOCAL_EDK2"
  "$PARENT/edk2"
  "$HOME/github/edk2"
  "$HOME/src/edk2"
  "$HOME/edk2"
  "/usr/local/src/edk2"
  "/opt/edk2"
)

for d in "${CANDIDATES[@]}"; do
  [[ -d "$d" ]] || continue
  if try_print "$d"; then
    exit 0
  fi
done

if clone_local_edk2; then
  if try_print "$LOCAL_EDK2"; then
    exit 0
  fi
fi

echo "edk2-workspace: 自动克隆失败。可手动执行：" >&2
echo "  git clone --depth 1 $EDK2_GIT_URL \"$LOCAL_EDK2\"" >&2
echo "或设置: export EDK2_WORKSPACE=/已有/edk2" >&2
exit 1
