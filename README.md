# UEFINes

在 UEFI 下运行的 NES 模拟器启动器：图形菜单浏览 FAT 卷与目录、选择 `.nes` 后回车加载，支持简易「游戏机 / 模拟器」设置并写入 ESP 上的配置文件。模拟核心与图形字体来自 [NesUEFI](https://github.com/shadlyd15/NesUEFI)（MIT）。

## 功能概要

- 主菜单：浏览 ROM、Controller（键盘映射与按住帧数）、关于、退出固件
- 多卷时先选 `SimpleFileSystem` 卷，再进入目录；`..` 返回上级；支持 PgUp/PgDn 翻页
- 配置：启动介质上 **`Uefines/settings.ini`**（`KEY=VALUE`，带注释行）。开机加载一次；进入 **Controller** 时重新从该文件加载；在 Controller 内每次改 **Hold** 或 **Keys** 后立即写回。缩放、NTSC/PAL、静音等仍保存在同一文件，可手改 ini。
- 游戏中：`Esc` 返回浏览界面；默认 `K`/`J`/`U`/`I` + WASD/方向键（可在 Controller 中切换另一套键位与按住长度）

## 依赖

- [EDK II](https://github.com/tianocore/edk2)（需完整克隆并初始化子模块，或至少满足 `MdePkg.dec` 中的包含路径）
- Linux 上典型工具链：`gcc`、`nasm`、`uuid-dev`、`python3`
- 首次构建前需编译 BaseTools（或至少 `BaseTools/Source/C/GenFw`），以便生成 `.efi`

## 构建示例

### 使用仓库根目录 Makefile（推荐）

未设置 `EDK2_WORKSPACE` 时，[`scripts/edk2-workspace.sh`](scripts/edk2-workspace.sh) 会先探测常见路径；若没有有效 edk2，会**自动**用 `git clone` 拉到本仓库下的 **`edk2/`**（已写入 `.gitignore`）。也可手动 `export EDK2_WORKSPACE=...` 使用其它副本。

```bash
cd /path/to/UEFINes
make show-edk2          # 可选：查看将使用的 edk2 路径
make
make install            # 安装到 esp/EFI/Boot/bootx64.efi
```

其它变量：`ARCH`、`TOOLCHAIN`、`BUILD_TYPE`。首次若缺少 `GenFw`，可执行 `make genfw`。

### 手动调用 build

```bash
export WORKSPACE=/path/to/edk2
cd "$WORKSPACE"
export PACKAGES_PATH=$WORKSPACE:/path/to/UEFINes
# 若 MdePkg 报缺少 mipisyst 头文件，可先按官方文档拉取子模块，或创建对应空目录占位
. ./edksetup.sh
# 若提示找不到 build_rule.txt，可从 Conf 复制到 WORKSPACE 根目录
build -p UefinesPkg/UefinesPkg.dsc -m UefinesPkg/Uefines/Uefines.inf -a X64 -t GCC -b RELEASE
```

产物路径示例：

`$WORKSPACE/Build/Uefines/RELEASE_GCC/X64/Uefines.efi`

## QEMU + OVMF 快速试验

本仓库包含 **`rom/`** 目录（约 262 个 `.nes`，从本机路径同步而来；已在 `.gitignore` 中忽略，避免误提交大文件）。

1. 将构建产物复制为 QEMU 用 ESP：

```bash
mkdir -p esp/EFI/Boot
cp Build/Uefines/RELEASE_GCC/X64/Uefines.efi esp/EFI/Boot/bootx64.efi
# （若在 edk2 工作区内构建，请把上面路径换成你的 Uefines.efi）
```

2. **无参数**启动（推荐）：挂载 `esp/` 与 ROM 盘。若存在 **`rom/ROMDISK.img`**（`make img` 优先生成的 FAT 镜像），脚本优先挂载；否则若有 **`rom/ROMDISK.iso`**（FAT 空间不足或写入失败时 `make img` 会改出 ISO），则按光盘挂载；再否则仍用目录型 `fat:rw:`。启动器会尽量自动打开含 `.nes` 或 **`roms/`** 的卷；根目录仅有一项 `roms` 时会自动进入。

```bash
make img                # dosfstools+mtools；失败时自动生成 ISO（需 xorriso 或 genisoimage）；可用 ROM_DISK_SIZE_MB 指定 FAT 大小
./scripts/qemu-ovmf.sh
```

可选环境变量：`OVMF_CODE`、`OVMF_VARS_SRC`、`ESP_DIR`、`ROM_DIR`、`ROM_DISK_IMG`、`ROM_DISK_ISO`。

3. 仍可使用**单块 raw 镜像**（旧用法）：

```bash
./scripts/qemu-ovmf.sh /path/to/disk.img
```

脚本会在仓库下创建可写 `varstore/OVMF_VARS.fd`，避免改写系统自带的 OVMF_VARS。

## 限制说明

- 当前 ROM 加载逻辑与 NesUEFI 一致，主要支持 **Mapper 0 与 3**；其它 Mapper 会提示错误
- UEFI 无标准音频输出；`MUTE` 仅作占位与后续扩展
- GOP 像素格式依固件而定，若色彩异常需在 `graphics_set_pixel` 中按 `PixelFormat` 转换

## 许可证

仓库内模拟器与 Adafruit GFX 相关文件分别遵循 NesUEFI（MIT）与 Adafruit BSD 许可；请参阅各文件头及 [LICENSE](LICENSE)。
