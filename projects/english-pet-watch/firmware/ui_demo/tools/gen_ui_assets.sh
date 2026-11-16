#!/usr/bin/env bash
# 重新生成 UI 素材：GB2312 中文字体 + 150px Fredoka 时间字体 + Fluent Emoji 3D 表情（LVGL C 数组）
#
# 依赖：curl、sips（macOS）、node/npx、python3
# 用法：cd firmware/ui_demo && ./tools/gen_ui_assets.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

FONT_URL="https://cdn.jsdelivr.net/gh/notofonts/noto-cjk@main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
FONT="${TMP}/NotoSansSC-Regular.otf"
# Fredoka SemiBold（Google Fonts 静态实例）：圆润、略胖、数字辨识度高。
FREDOKA_URL="https://fonts.gstatic.com/s/fredoka/v17/X7nP4b87HvSqjb_WIi2yDCRwoQ_k7367_B-i2yQag0-mac3OLyXMFQ.woff"
FREDOKA_FALLBACK_URL="https://cdn.jsdelivr.net/gh/google/fonts@main/ofl/fredoka/Fredoka%5Bwdth%2Cwght%5D.ttf"
FREDOKA="${TMP}/Fredoka-SemiBold.woff"
FREDOKA_FALLBACK="${TMP}/Fredoka-Variable.ttf"
EMOJI_BASE="https://cdn.jsdelivr.net/gh/microsoft/fluentui-emoji@main/assets"

echo "[1/5] 下载 Noto Sans SC + Fredoka"
curl -fsSL -m 120 -o "${FONT}" "${FONT_URL}"
if ! curl -fsSL -m 120 -o "${FREDOKA}" "${FREDOKA_URL}"; then
  echo "  Fredoka 静态实例下载失败，回退到可变字体"
  curl -fsSL -m 120 -o "${FREDOKA_FALLBACK}" "${FREDOKA_FALLBACK_URL}"
  FREDOKA="${FREDOKA_FALLBACK}"
fi

echo "[2/5] 下载 6 个 Fluent Emoji 3D"
download_emoji() {
  local dir="$1" file="$2" out="$3"
  local enc
  enc="$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1]))' "${dir}")"
  curl -fsSL -m 60 -o "${TMP}/${out}.png" "${EMOJI_BASE}/${enc}/3D/${file}_3d.png"
  sips -z 208 208 "${TMP}/${out}.png" --out "${TMP}/${out}.png" >/dev/null
}
# 统一选正脸猫，表情差异清楚，适合 5 岁小朋友；208px 素材页内 1:1 显示，避免运行时缩放
download_emoji "Cat face" "cat_face" "idle"
download_emoji "Smiling cat with heart-eyes" "smiling_cat_with_heart-eyes" "happy"
download_emoji "Grinning cat with smiling eyes" "grinning_cat_with_smiling_eyes" "excited"
download_emoji "Cat with wry smile" "cat_with_wry_smile" "thinking"
download_emoji "Cat with tears of joy" "cat_with_tears_of_joy" "speaking"
download_emoji "Weary cat" "weary_cat" "sleepy"
download_emoji "Crying cat" "crying_cat" "sad"
download_emoji "Kissing cat" "kissing_cat" "love"

echo "[3/5] 生成中文字体（GB2312 6763 字 + 界面文案，覆盖豆包动态台词）"
SYMBOLS="$(python3 - "${ROOT}/main/ui.c" <<'PY'
import re
import sys

# 1) 界面文案里的字，保证静态 UI 一定覆盖
text = open(sys.argv[1], encoding="utf-8").read()
# 只从代码里取字，注释里的中文不应进子集（否则改注释就会把字体撑大）
text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
text = re.sub(r'//[^\n]*', '', text)
chars = {c for c in text if "\u4e00" <= c <= "\u9fff"}

# 2) GB2312 全集（一级 3755 + 二级 3008）：豆包/服务端返回的常见中文都在里面，
#    避免动态台词出现方框。生僻字/emoji 由 ui.c 的 pet_safe_text() 运行时过滤。
for hi in range(0xA1, 0xF8):
    for lo in range(0xA1, 0xFF):
        try:
            ch = bytes((hi, lo)).decode("gb2312")
        except UnicodeDecodeError:
            continue
        if "\u4e00" <= ch <= "\u9fff":
            chars.add(ch)

# 3) 额外标点：温度 °、乘号 ×、中点 ·，以及界面台词常用全角标点
chars |= set("°×·！～…：，。？（）、“”；《》【】—")
print("".join(sorted(chars)), end="")
PY
)"
echo "  字体符号数: $(python3 -c 'import sys;print(len(sys.argv[1]))' "${SYMBOLS}")"

for size in 16 20; do
  npx --yes lv_font_conv@1.5.3 \
    --font "${FONT}" \
    --size "${size}" \
    --bpp 4 \
    --format lvgl \
    --lv-include lvgl.h \
    --lv-font-name "epw_cjk_${size}" \
    --range 0x20-0x7E \
    --symbols "${SYMBOLS}" \
    --no-compress \
    -o "${ROOT}/main/assets/epw_cjk_${size}.c"
done

echo "[4/5] 生成 150px 圆润时间字体（Fredoka SemiBold，数字 + 冒号）"
npx --yes lv_font_conv@1.5.3 \
  --font "${FREDOKA}" \
  --size 150 \
  --bpp 4 \
  --format lvgl \
  --lv-include lvgl.h \
  --lv-font-name "epw_time_150" \
  --symbols "0123456789:" \
  --no-compress \
  -o "${ROOT}/main/assets/epw_time_150.c"

echo "[5/5] 转换表情 PNG -> LVGL RGB565A8"
python3 "${ROOT}/tools/png_to_lvgl.py" "${TMP}" "${ROOT}/main/assets"

echo "[check] 校验 ui.c 文案是否全部有字形"
python3 "${ROOT}/tools/check_font_coverage.py"

echo "done: main/assets/epw_cjk_16.c, epw_cjk_20.c, epw_time_150.c, pip_faces.c"
