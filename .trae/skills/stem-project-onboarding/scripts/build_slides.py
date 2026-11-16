#!/usr/bin/env python3
"""课件构建：Marp 风格 Markdown -> 自包含单文件 HTML（零依赖，可全屏放映 / 可打印 PDF）

用法:
  python3 build_slides.py <课件.md> [更多.md ...] [-o 输出.html] [--strict] [--quiet]
  python3 build_slides.py projects/<slug>/docs/儿童课程/*/课件.md

语法（一个 `---` 分隔一页）:
  <!-- layout: title|big|image|two-col -->   页面版式，缺省为要点版式
  # 大标题 / ## 标题 / ### 两栏时的栏标题
  - 要点   1. 编号要点
  > 强调条
  ![说明](图路径)                             本地图片会内嵌进 HTML
  <!-- notes: 讲者备注（放映时按 N 看） -->
  <!-- 其它注释会被忽略 -->

退出码: 0 成功；--strict 且有告警时为 1。
"""

import argparse
import base64
import html
import mimetypes
import os
import re
import sys

MAX_EMBED_BYTES = 5 * 1024 * 1024  # 单张图超过 5MB 不内嵌，保留相对路径
CJK_OR_WORD = re.compile(r"[\u4e00-\u9fff]|[A-Za-z0-9]+")
LAYOUTS = {"title", "big", "image", "two-col", "bullets"}

CSS = """
:root{--bg:#0f1220;--fg:#f7f8fb;--dim:#9aa3b8;--accent:#ffd166;--accent2:#4cc9f0;--ok:#63e6be;}
*{box-sizing:border-box}
html,body{margin:0;height:100%;background:#000;color:var(--fg);
  font-family:"PingFang SC","Hiragino Sans GB","Microsoft YaHei",system-ui,sans-serif}
#deck{position:relative;width:100vw;height:100vh;overflow:hidden;background:var(--bg)}
.slide{position:absolute;inset:0;display:none;flex-direction:column;justify-content:center;
  padding:5vh 6vw;gap:1.4vh}
.slide.on{display:flex}
h1{font-size:7.2vh;line-height:1.15;margin:0 0 1vh}
h2{font-size:5.4vh;line-height:1.2;margin:0 0 2vh}
h3{font-size:3.6vh;margin:0 0 1vh;color:var(--accent2)}
p,li{font-size:4vh;line-height:1.45}
ul,ol{margin:0;padding-left:1.2em}
li{margin:.6vh 0}
strong{color:var(--accent)}
code{background:#ffffff1a;padding:.1em .35em;border-radius:.3em;font-size:.9em}
img{max-width:100%;max-height:58vh;object-fit:contain;border-radius:1.2vh;display:block;margin:0 auto}
.callout{font-size:4.2vh;padding:1.6vh 2vw;border-left:.8vw solid var(--accent);
  background:#ffffff12;border-radius:1vh}
.slide[data-layout=title]{align-items:center;text-align:center}
.slide[data-layout=title] img{max-height:34vh}
.slide[data-layout=title] p{color:var(--dim);font-size:2.8vh}
.slide[data-layout=big]{align-items:center;text-align:center}
.slide[data-layout=big] h1,.slide[data-layout=big] p{font-size:9vh;color:var(--accent)}
.slide[data-layout=image]{align-items:center;text-align:center}
.slide[data-layout=image] img{max-height:66vh}
.cols{display:flex;gap:3vw;align-items:flex-start}
.cols>div{flex:1;min-width:0}
.notes{display:none}
body.notes-on .notes{display:block;position:fixed;left:0;right:0;bottom:0;max-height:32vh;overflow:auto;
  background:#000000e6;border-top:.4vh solid var(--accent2);padding:1.6vh 3vw;font-size:2.3vh;color:#dbe4f0}
body.notes-on .slide{padding-bottom:34vh}
#bar{position:fixed;top:0;left:0;height:.7vh;background:var(--accent);width:0;transition:width .15s;z-index:5}
#num{position:fixed;right:1.6vw;bottom:1.2vh;font-size:2.2vh;color:var(--dim);z-index:5}
#hint{position:fixed;left:1.6vw;bottom:1.2vh;font-size:2vh;color:#6b7488;z-index:5}
@media print{
  @page{size:297mm 167mm;margin:0}
  html,body{background:#fff}
  #bar,#num,#hint,.notes{display:none!important}
  #deck{width:auto;height:auto;background:var(--bg)}
  .slide{display:flex!important;position:relative;inset:auto;width:297mm;height:167mm;
    page-break-after:always;break-after:page}
  body{color:var(--fg);background:var(--bg)}
}
"""

JS = r"""
const slides=[...document.querySelectorAll('.slide')];
let i=0;
function show(n){
  i=Math.max(0,Math.min(slides.length-1,n));
  slides.forEach((s,k)=>s.classList.toggle('on',k===i));
  document.getElementById('bar').style.width=((i+1)/slides.length*100)+'%';
  document.getElementById('num').textContent=(i+1)+' / '+slides.length;
  history.replaceState(null,'','#'+(i+1));
}
function toggleNotes(){document.body.classList.toggle('notes-on');}
addEventListener('keydown',e=>{
  const k=e.key;
  if(k==='ArrowRight'||k===' '||k==='PageDown'||k==='j'){show(i+1);e.preventDefault();}
  else if(k==='ArrowLeft'||k==='PageUp'||k==='k'){show(i-1);e.preventDefault();}
  else if(k==='Home'){show(0);} else if(k==='End'){show(slides.length-1);}
  else if(k==='n'||k==='N'){toggleNotes();}
  else if(k==='f'||k==='F'){document.fullscreenElement?document.exitFullscreen():document.documentElement.requestFullscreen();}
  else if(k==='?'||k==='/'){alert('← → 翻页 ｜ F 全屏 ｜ N 备注 ｜ Ctrl+P 打印成 PDF');}
});
addEventListener('click',e=>{show(i+(e.clientX>innerWidth/2?1:-1));});
show(parseInt(location.hash.slice(1)||'1',10)-1);
"""

TEMPLATE = """<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title}</title><style>{css}</style></head>
<body>
<div id="bar"></div>
<div id="deck">
{slides}
</div>
<div id="num"></div><div id="hint">← → 翻页 ｜ F 全屏 ｜ N 备注 ｜ Ctrl+P 导出 PDF</div>
<script>{js}</script>
</body></html>
"""


def text_len(s):
    return len(CJK_OR_WORD.findall(s))


def inline(md):
    out = html.escape(md, quote=False)
    out = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", out)
    out = re.sub(r"`([^`]+)`", r"<code>\1</code>", out)
    out = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"<a href='\2'>\1</a>", out)
    return out


def embed_image(src, base_dir, budget):
    """本地图片且不太大时内嵌为 data URI，让 HTML 单文件可拷走放映。"""
    path = src if os.path.isabs(src) else os.path.join(base_dir, src)
    if not os.path.isfile(path):
        return src, budget
    size = os.path.getsize(path)
    if size > MAX_EMBED_BYTES or size > budget:
        return src, budget
    mime = mimetypes.guess_type(path)[0] or "image/png"
    with open(path, "rb") as fh:
        data = base64.b64encode(fh.read()).decode("ascii")
    return "data:%s;base64,%s" % (mime, data), budget - size


def render_body(block, base_dir, warn, page_no, budget):
    """返回 (html, 本页文字数, 是否有图)"""
    layout = "bullets"
    m = re.search(r"<!--\s*layout:\s*([A-Za-z\-]+)\s*-->", block)
    if m:
        layout = m.group(1).strip().lower()
        if layout not in LAYOUTS:
            warn("第 %d 页: 未知版式 '%s'，已按要点版式渲染" % (page_no, layout))
            layout = "bullets"
    block = re.sub(r"<!--\s*layout:[^>]*-->", "", block)
    notes = ""
    mn = re.search(r"<!--\s*notes:\s*(.*?)-->", block, re.S)
    if mn:
        notes = inline(mn.group(1).strip())
        block = block.replace(mn.group(0), "")
    block = re.sub(r"<!--(?!\s*notes:).*?-->", "", block, flags=re.S)

    plain = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", block)
    n_chars = text_len(re.sub(r"[#>\-*\s]", "", plain))

    has_img = False
    if layout == "two-col":
        parts = [p for p in re.split(r"(?m)^###\s*", block) if p.strip()]
        cols = []
        for p in parts:
            head, _, rest = p.partition("\n")
            cols.append("<div><h3>%s</h3>%s</div>" % (inline(head.strip()), render_lines(rest, base_dir, warn, budget)))
        body = '<div class="cols">%s</div>' % "".join(cols)
        has_img = "data:image" in body or "<img" in body
    else:
        body, has_img = render_lines(block, base_dir, warn, budget, collect_img=True)

    note_html = '<div class="notes">备注：%s</div>' % notes if notes else ""
    cls = "slide on" if page_no == 1 else "slide"
    return (('<section class="%s" data-layout="%s">%s%s</section>'
             % (cls, layout, body, note_html)), n_chars, has_img, bool(notes), layout)


def render_lines(text, base_dir, warn, budget, collect_img=False):
    html_parts, has_img = [], False
    list_open = None
    for raw in text.split("\n"):
        line = raw.rstrip()
        if not line.strip():
            if list_open:
                html_parts.append("</%s>" % list_open)
                list_open = None
            continue
        img = re.match(r"^\s*!\[([^\]]*)\]\(([^)]+)\)\s*$", line)
        if img:
            if list_open:
                html_parts.append("</%s>" % list_open)
                list_open = None
            src, budget = embed_image(img.group(2).strip(), base_dir, budget)
            html_parts.append('<img src="%s" alt="%s">' % (html.escape(src, quote=True), html.escape(img.group(1))))
            has_img = True
            continue
        head = re.match(r"^(#{1,3})\s+(.*)$", line.strip())
        if head:
            if list_open:
                html_parts.append("</%s>" % list_open)
                list_open = None
            lvl = len(head.group(1))
            html_parts.append("<h%d>%s</h%d>" % (lvl, inline(head.group(2)), lvl))
            continue
        if line.strip().startswith(">"):
            if list_open:
                html_parts.append("</%s>" % list_open)
                list_open = None
            html_parts.append('<div class="callout">%s</div>' % inline(line.strip()[1:].strip()))
            continue
        item = re.match(r"^\s*[-*]\s+(.*)$", line)
        num = re.match(r"^\s*\d+[.)]\s+(.*)$", line)
        if item or num:
            want = "ul" if item else "ol"
            if list_open != want:
                if list_open:
                    html_parts.append("</%s>" % list_open)
                html_parts.append("<%s>" % want)
                list_open = want
            html_parts.append("<li>%s</li>" % inline((item or num).group(1)))
            continue
        if list_open:
            html_parts.append("</%s>" % list_open)
            list_open = None
        html_parts.append("<p>%s</p>" % inline(line.strip()))
    if list_open:
        html_parts.append("</%s>" % list_open)
    if collect_img:
        return "\n".join(html_parts), has_img
    return "\n".join(html_parts)


def build(src, out_path=None, quiet=False):
    base_dir = os.path.dirname(os.path.abspath(src))
    with open(src, encoding="utf-8") as fh:
        text = fh.read()
    # 文件头的说明性注释不算进第一页（不影响 layout/notes 指令）
    text = re.sub(r"\A\s*<!--.*?-->(?=\s*(?:<!--\s*(?:layout|notes):|$))", "", text, flags=re.S)
    blocks = re.split(r"(?m)^---\s*$", text)
    blocks = [b for b in blocks if b.strip()]
    if not blocks:
        print("错误: %s 里没有内容" % src, file=sys.stderr)
        return 1, 0

    warnings = []
    warn = warnings.append
    rendered, budget = [], 30 * 1024 * 1024
    for idx, block in enumerate(blocks, 1):
        sec, n_chars, has_img, has_notes, layout = render_body(block, base_dir, warn, idx, budget)
        rendered.append(sec)
        limit = 60 if layout == "two-col" else 30
        if n_chars > limit:
            warn("第 %d 页: 文字约 %d 字，儿童课件建议每页 ≤15 字（两栏页按每栏 15 字算）" % (idx, n_chars))
        if not has_notes:
            warn("第 %d 页: 缺少讲者备注（<!-- notes: ... -->）" % idx)
        if not has_img and layout not in ("title", "big", "two-col"):
            warn("第 %d 页: 没有图位（![说明](图路径)）—— 除封面/大字/收尾页外都该有图" % idx)
    if len(blocks) > 16:
        warn("共 %d 页，一课时建议 10–14 页" % len(blocks))

    title = re.search(r"(?m)^#\s+(.*)$", blocks[0])
    title = re.sub(r"\*\*", "", title.group(1)).strip() if title else os.path.basename(src)
    out = out_path or os.path.splitext(src)[0] + ".html"
    with open(out, "w", encoding="utf-8") as fh:
        fh.write(TEMPLATE.format(title=html.escape(title), css=CSS, js=JS, slides="\n".join(rendered)))
    if not quiet:
        print("已生成 %s（%d 页，%.1f KB）" % (out, len(blocks), os.path.getsize(out) / 1024.0))
    for w in warnings:
        print("  提示: %s" % w, file=sys.stderr)
    return 0, len(warnings)


def main(argv):
    ap = argparse.ArgumentParser(add_help=True, description="课件 md → 可放映 HTML")
    ap.add_argument("md", nargs="+", help="课件 Markdown 文件")
    ap.add_argument("-o", "--out", help="输出 HTML（仅单个输入时可用）")
    ap.add_argument("--strict", action="store_true", help="有提示时返回非零")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    if args.out and len(args.md) > 1:
        ap.error("-o 只能配一个输入文件")
    warns, rc = 0, 0
    for src in args.md:
        if not os.path.isfile(src):
            print("错误: 找不到 %s" % src, file=sys.stderr)
            rc = 1
            continue
        code, w = build(src, args.out, args.quiet)
        rc = rc or code
        warns += w
    if args.strict and warns:
        return 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
