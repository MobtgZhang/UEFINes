# UEFINes — 使用 EDK2 构建 Uefines.efi
#
# 用法：
#   make                    # RELEASE / X64 / GCC，EDK2 路径由 scripts/edk2-workspace.sh 自动探测
#   make EDK2_WORKSPACE=~/edk2   # 手动指定（跳过探测）
#   make BUILD_TYPE=DEBUG
#   make install            # 复制到 esp/EFI/Boot/bootx64.efi
#
# 依赖：已克隆的 tianocore/edk2、BaseTools（至少能运行 GenFw）、nasm、gcc、uuid-dev、python3

UEFINES_ROOT := $(abspath .)

# 未指定 EDK2_WORKSPACE 时：仅「clean / distclean / help / show-edk2」用 --resolve-only，绝不自动 git clone
ifeq ($(MAKECMDGOALS),)
  EDK2_MAKE_GOALS := all
else
  EDK2_MAKE_GOALS := $(MAKECMDGOALS)
endif
EDK2_SAFE_GOALS := clean distclean help show-edk2 img
EDK2_NEED_CLONE := $(strip $(filter-out $(EDK2_SAFE_GOALS),$(EDK2_MAKE_GOALS)))

ifndef EDK2_WORKSPACE
  ifneq ($(EDK2_NEED_CLONE),)
    EDK2_WORKSPACE := $(shell "$(UEFINES_ROOT)/scripts/edk2-workspace.sh")
  else
    EDK2_WORKSPACE := $(shell "$(UEFINES_ROOT)/scripts/edk2-workspace.sh" --resolve-only)
  endif
endif

ARCH         ?= X64
TOOLCHAIN    ?= GCC
BUILD_TYPE   ?= RELEASE
PACKAGES_PATH = $(EDK2_WORKSPACE):$(UEFINES_ROOT)

# 与 UefinesPkg.dsc 中 OUTPUT_DIRECTORY=Build/Uefines 一致
UEFI_OUT      := $(EDK2_WORKSPACE)/Build/Uefines/$(BUILD_TYPE)_$(TOOLCHAIN)/$(ARCH)/Uefines.efi
GENFW         := $(EDK2_WORKSPACE)/BaseTools/Source/C/bin/GenFw
ESP_BOOT      := $(UEFINES_ROOT)/esp/EFI/Boot/bootx64.efi
EDK2_RESOLVER := $(UEFINES_ROOT)/scripts/edk2-workspace.sh

.PHONY: all uefines clean distclean install check-edk2 genfw help show-edk2 img

all: uefines

help:
	@echo "目标："
	@echo "  make / make uefines  构建 Uefines.efi"
	@echo "  make show-edk2       打印当前探测到的 EDK2_WORKSPACE（或报错与提示）"
	@echo "  make genfw           编译 GenFw（会先构建 libCommon）"
	@echo "  make install         安装到 esp/EFI/Boot/bootx64.efi"
	@echo "  make clean           删除 EDK2 Build/Uefines、varstore/、esp/（并尝试 git 恢复 esp 内受控文件）"
	@echo "  make distclean       与 clean 相同（保留别名以兼容旧用法）"
	@echo "  make img             优先生成 rom/ROMDISK.img；失败则 rom/ROMDISK.iso（需 dosfstools、mtools；ISO 需 xorriso 或 genisoimage）"
	@echo ""
	@echo "变量：EDK2_WORKSPACE（可选；不填则用 scripts/edk2-workspace.sh） ARCH TOOLCHAIN BUILD_TYPE"

show-edk2:
	@$(EDK2_RESOLVER)

img:
	@bash "$(UEFINES_ROOT)/scripts/mk-rom-disk-img.sh"

check-edk2:
	@if [ -z "$(strip $(EDK2_WORKSPACE))" ]; then \
		echo "错误: 未能解析 EDK2_WORKSPACE（edk2 目录不存在或未克隆）。" >&2; \
		echo "请运行: $(EDK2_RESOLVER)" >&2; \
		exit 1; \
	fi
	@if [ ! -d "$(EDK2_WORKSPACE)/MdePkg" ]; then \
		echo "错误: EDK2_WORKSPACE 无效（未找到 MdePkg）: $(EDK2_WORKSPACE)" >&2; \
		echo "可重新探测: make show-edk2   或手动: make EDK2_WORKSPACE=/你的/edk2路径" >&2; \
		exit 1; \
	fi
	@if [ ! -f "$(EDK2_WORKSPACE)/edksetup.sh" ]; then \
		echo "错误: 未找到 edksetup.sh"; exit 1; \
	fi

genfw: check-edk2
	@echo "==> 构建 BaseTools libCommon + GenFw"
	$(MAKE) -C "$(EDK2_WORKSPACE)/BaseTools/Source/C/Common"
	$(MAKE) -C "$(EDK2_WORKSPACE)/BaseTools/Source/C/GenFw"
	@test -x "$(GENFW)" || (echo "未生成 $(GENFW)"; exit 1)
	@echo "=> $(GENFW)"

uefines: check-edk2
	@if [ ! -x "$(GENFW)" ]; then \
		echo "提示: 未找到可执行的 GenFw，正在尝试编译 libCommon + GenFw…"; \
		$(MAKE) -C "$(EDK2_WORKSPACE)/BaseTools/Source/C/Common" && \
		$(MAKE) -C "$(EDK2_WORKSPACE)/BaseTools/Source/C/GenFw" || \
		( echo "失败: 请先执行  make genfw  或  make -C \$$EDK2_WORKSPACE/BaseTools/Source/C"; exit 1 ); \
	fi
	@echo "==> EDK2_WORKSPACE=$(EDK2_WORKSPACE)"
	@echo "==> PACKAGES_PATH=$(PACKAGES_PATH)"
	bash -c 'set -e; \
		cd "$(EDK2_WORKSPACE)"; \
		export WORKSPACE="$(EDK2_WORKSPACE)"; \
		export PACKAGES_PATH="$(PACKAGES_PATH)"; \
		export CONF_PATH="$(EDK2_WORKSPACE)/Conf"; \
		mkdir -p Conf; \
		for f in build_rule tools_def target; do \
			test -f "Conf/$$f.txt" || cp -f "BaseTools/Conf/$$f.template" "Conf/$$f.txt"; \
		done; \
		. ./edksetup.sh; \
		build -p UefinesPkg/UefinesPkg.dsc \
			-m UefinesPkg/Uefines/Uefines.inf \
			-a $(ARCH) -t $(TOOLCHAIN) -b $(BUILD_TYPE)'
	@test -f "$(UEFI_OUT)" || (echo "构建未产生: $(UEFI_OUT)"; exit 1)
	@echo "=> $(UEFI_OUT)"

install: uefines
	@mkdir -p "$(dir $(ESP_BOOT))"
	@mkdir -p "$(UEFINES_ROOT)/esp/EFI/Boot/Fonts"
	cp -f "$(UEFI_OUT)" "$(ESP_BOOT)"
	@if [ -f "$(UEFINES_ROOT)/fonts/UFNT.BIN" ]; then \
		cp -f "$(UEFINES_ROOT)/fonts/UFNT.BIN" "$(UEFINES_ROOT)/esp/EFI/Boot/Fonts/UFNT.BIN"; \
		echo "=> $(UEFINES_ROOT)/esp/EFI/Boot/Fonts/UFNT.BIN"; \
	fi
	@echo "=> $(ESP_BOOT)"

# clean 删除 varstore/、esp/ 下安装产物；Fonts/README.txt 优先从 git 恢复，否则复制 scripts/esp-fonts-readme.txt（须与 esp 内说明保持同步）。
clean:
	@rm -rf "$(UEFINES_ROOT)/varstore"; \
	echo "已删除 varstore/（若存在）"; \
	rm -rf "$(UEFINES_ROOT)/esp"; \
	echo "已删除 esp/（若存在）"; \
	if [ -d "$(UEFINES_ROOT)/.git" ]; then \
		git -C "$(UEFINES_ROOT)" checkout HEAD -- esp/EFI/Boot/Fonts/README.txt 2>/dev/null || \
		git -C "$(UEFINES_ROOT)" restore --source=HEAD --worktree -- esp/EFI/Boot/Fonts/README.txt 2>/dev/null || true; \
	fi; \
	if [ ! -f "$(UEFINES_ROOT)/esp/EFI/Boot/Fonts/README.txt" ]; then \
		mkdir -p "$(UEFINES_ROOT)/esp/EFI/Boot/Fonts" && \
		cp -f "$(UEFINES_ROOT)/scripts/esp-fonts-readme.txt" "$(UEFINES_ROOT)/esp/EFI/Boot/Fonts/README.txt" && \
		echo "已用 scripts/esp-fonts-readme.txt 恢复 Fonts/README.txt"; \
	else \
		echo "已恢复 esp/EFI/Boot/Fonts/README.txt"; \
	fi; \
	EDK2="$(strip $(EDK2_WORKSPACE))"; \
	LOCAL_EDK2="$(UEFINES_ROOT)/edk2"; \
	if [ -n "$$EDK2" ] && [ -d "$$EDK2/MdePkg" ]; then \
		rm -rf "$$EDK2/Build/Uefines"; \
		echo "已删除 $$EDK2/Build/Uefines"; \
	fi; \
	if [ "$$EDK2" != "$$LOCAL_EDK2" ] && [ -d "$$LOCAL_EDK2/MdePkg" ]; then \
		rm -rf "$$LOCAL_EDK2/Build/Uefines"; \
		echo "已删除 $$LOCAL_EDK2/Build/Uefines"; \
	fi; \
	if [ -z "$$EDK2" ] || [ ! -d "$$EDK2/MdePkg" ]; then \
		if [ ! -d "$(UEFINES_ROOT)/edk2/MdePkg" ]; then \
			echo "提示: 未找到有效 edk2，未删除 EDK2 Build/Uefines"; \
		fi; \
	fi

distclean: clean
