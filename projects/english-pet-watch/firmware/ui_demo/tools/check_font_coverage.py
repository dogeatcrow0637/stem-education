#!/usr/bin/env python3
"""检查中文字体是否覆盖界面文案 + 豆包动态台词所需的字。

背景（V0.7 方框问题）：
  1. 字体子集原先只从 ui.c 的字符串字面量生成，改了文案却忘了重跑脚本，
     新字就会渲染成方框（tofu）。例如宠物页新增的「聊天」的「聊」。
  2. 豆包桥接返回的 text 是服务端动态生成的，静态子集永远追不上，
     生僻字 / emoji 必然变方框。

所以现在字体固定包含 GB2312 全集（6763 字），ui.c 里的字再额外补齐；
运行时 ui.c 的 pet_safe_text() 还会过滤字体里确实没有的码点。

用法：python3 tools/check_font_coverage.py
退出码：0 = 全覆盖；1 = 有缺字。
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI_C = ROOT / "main" / "ui.c"
MOCK_PY = ROOT / "tools" / "doubao_bridge_mock.py"
FONTS = [ROOT / "main" / "assets" / "epw_cjk_16.c",
         ROOT / "main" / "assets" / "epw_cjk_20.c"]

# 豆包常见回复里的口语字，防止只检查静态 UI 漏掉动态场景
DYNAMIC_SAMPLES = [
    "我们一起学英语吧！",
    "你真棒，给你一个大大的拥抱！",
    "嗯嗯，我明白啦，再试一次好吗？",
    "小猫咪今天想听什么故事呀？",
    "蝴蝶飞呀飞，小鸟叽叽喳喳。",
]


def literals_cjk(path: Path) -> set:
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".py":
        text = re.sub(r"(?m)^\s*#.*$", "", text)
    else:
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
    literals = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))
    literals += "".join(re.findall(r"'((?:[^'\\]|\\.)*)'", text))
    return {c for c in literals if "\u4e00" <= c <= "\u9fff"}


def gb2312_cjk() -> set:
    chars = set()
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if "\u4e00" <= ch <= "\u9fff":
                chars.add(ch)
    return chars


def _array(src: str, name: str) -> list:
    m = re.search(rf"{name}\[\] = \{{(.*?)\}};", src, re.S)
    if m is None:
        return []
    body = m.group(1)
    return [int(x, 16) if x.lower().startswith("0x") else int(x)
            for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body)]


def font_codepoints(path: Path) -> set:
    """解析 lv_font_conv 生成的 cmaps[]：符号多时会被拆成多个连续/稀疏表。"""
    src = path.read_text(encoding="utf-8")
    start = src.index("static const lv_font_fmt_txt_cmap_t cmaps[]")
    seg = src[start:src.index("};", start)]
    entries = re.findall(r"\{([^{}]*)\}", seg, re.S)
    if not entries:
        raise SystemExit(f"{path.name}: 找不到 cmaps[]")

    cps = set()
    for ent in entries:
        def field(name):
            m = re.search(rf"\.{name} = (\w+)", ent)
            return m.group(1) if m else None

        lo = int(field("range_start"))
        ln = int(field("range_length"))
        type_name = field("type")
        ulist = field("unicode_list")
        ofs = field("glyph_id_ofs_list")

        if "SPARSE" in type_name:
            for v in _array(src, ulist):
                cps.add(lo + v)
        elif "FORMAT0_FULL" in type_name:
            table = _array(src, ofs) if ofs and ofs != "NULL" else []
            # LVGL 约定：区间首字总是有效（offset=0）；其它位置 offset=0 表示缺字
            for i, v in enumerate(table):
                if i == 0 or v != 0:
                    cps.add(lo + i)
        else:  # FORMAT0_TINY
            cps.update(range(lo, lo + ln))
    return cps


def main() -> int:
    needed = set()
    needed |= literals_cjk(UI_C)
    if MOCK_PY.exists():
        needed |= literals_cjk(MOCK_PY)
    needed |= {c for s in DYNAMIC_SAMPLES for c in s if "\u4e00" <= c <= "\u9fff"}

    gb = gb2312_cjk()
    print(f"界面/动态样例汉字: {len(needed)}")
    print(f"GB2312 常用汉字:   {len(gb)}")

    ok = True
    for font in FONTS:
        have = font_codepoints(font)
        missing_ui = sorted(c for c in needed if ord(c) not in have)
        missing_gb = sorted(c for c in gb if ord(c) not in have)
        if missing_ui or missing_gb:
            ok = False
            if missing_ui:
                print(f"[FAIL] {font.name}: 界面/动态缺 {len(missing_ui)} 字 -> {''.join(missing_ui)}")
            if missing_gb:
                print(f"[FAIL] {font.name}: GB2312 缺 {len(missing_gb)} 字 -> {''.join(missing_gb[:60])}"
                      f"{'…' if len(missing_gb) > 60 else ''}")
        else:
            print(f"[OK]   {font.name}: {len(have)} 字形，界面/动态 + GB2312 全覆盖")
    if not ok:
        print("请重跑 ./tools/gen_ui_assets.sh 后再编译。")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
