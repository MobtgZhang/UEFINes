UFNT.BIN — 16×16 位图字体（UEFINes 界面中文与 UTF-16 文件名）

推荐一键生成（仓库根目录）:

  ./scripts/build-ufnt.sh

默认：中文 Noto Sans CJK SC（无衬线）+ 英文 Noto Sans 可变体（google/fonts ofl/notosans，OFL）。

可选环境变量:
  UFNT_TTF=/你的/cjk.otf        CJK 主字体路径（默认 fonts/NotoSansCJKsc-Regular.otf）
  UFNT_LATIN_TTF=/你的/latin.ttf  英文无衬线体（默认 fonts/NotoSans-Latin-VF.ttf）
  UFNT_OUT=/path/UFNT.BIN       输出路径（默认 fonts/UFNT.BIN）
  UFNT_LATIN_ONLY=1             仅生成拉丁，速度快、体积小（无中文）
  NOTO_URL=...                  覆盖 CJK 自动下载 URL
  LATIN_FONT_URL=...            覆盖拉丁字体下载 URL（默认可变 Noto Sans raw）
  LIBERATION_URL=...            同 LATIN_FONT_URL（旧名，兼容）

依赖: python3、pip install pillow；下载字体时需 curl 或 wget。

生成后:
  make install     将 fonts/UFNT.BIN 复制到本目录
  或直接 ./scripts/qemu-ovmf.sh（若存在 fonts/UFNT.BIN 会自动拷到 esp）

无 UFNT.BIN 时界面中文多为方框，拉丁仍用内置字模；ROM 加载不依赖字体文件。
