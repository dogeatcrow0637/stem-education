#!/usr/bin/env python3
"""把串口日志里的 SHOTMETA/SHOT 行还原成 BMP 截图。

固件端（CONFIG_EPW_BOOT_SHOTS=y）以 RGB565 每行 base64 输出：
  SHOTMETA:<label>:<w>:<h>:<stride>
  SHOT:<label>:<row>:<base64>
  SHOTEND:<label>

用法：
  python3 tools/decode_shots.py /tmp/epw-monitor*.log -o screenshots/
  （可传多份日志；同一行有多个副本时取第一份长度正确的）
"""
from __future__ import annotations

import argparse
import base64
import re
from pathlib import Path

META_RE = re.compile(r"SHOTMETA:([A-Za-z0-9_\-]+):(\d+):(\d+):(\d+)")
ROW_RE = re.compile(r"SHOT:([A-Za-z0-9_\-]+):(\d+):([A-Za-z0-9+/=]+)")


def rgb565_to_bgr888(row: bytes) -> bytes:
    out = bytearray(len(row) // 2 * 3)
    o = 0
    for i in range(0, len(row) - 1, 2):
        v = row[i] | (row[i + 1] << 8)
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[o] = (b * 255 + 15) // 31
        out[o + 1] = (g * 255 + 31) // 63
        out[o + 2] = (r * 255 + 15) // 31
        o += 3
    return bytes(out)


def write_bmp(path: Path, width: int, height: int, rows: dict[int, bytes]) -> None:
    row_size = width * 3
    pad = (4 - row_size % 4) % 4
    pixel_bytes = (row_size + pad) * height
    file_size = 54 + pixel_bytes

    header = bytearray()
    header += b"BM"
    header += file_size.to_bytes(4, "little")
    header += (0).to_bytes(4, "little")
    header += (54).to_bytes(4, "little")
    header += (40).to_bytes(4, "little")
    header += width.to_bytes(4, "little", signed=True)
    header += height.to_bytes(4, "little", signed=True)
    header += (1).to_bytes(2, "little")
    header += (24).to_bytes(2, "little")
    header += (0).to_bytes(4, "little")
    header += pixel_bytes.to_bytes(4, "little")
    header += (2835).to_bytes(4, "little")
    header += (2835).to_bytes(4, "little")
    header += (0).to_bytes(4, "little")
    header += (0).to_bytes(4, "little")

    body = bytearray()
    for y in range(height - 1, -1, -1):  # BMP 自下而上
        body += rows[y]
        body += b"\x00" * pad

    path.write_bytes(bytes(header) + bytes(body))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", type=Path, nargs="+", help="一个或多个 idf.py monitor 输出日志")
    ap.add_argument("-o", "--out", type=Path, default=Path("screenshots"))
    ap.add_argument("--partial", action="store_true",
                    help="缺失行用相邻行补齐，用于中途查看抓取进度")
    args = ap.parse_args()

    meta: dict[str, tuple[int, int, int]] = {}
    rows: dict[str, dict[int, list[bytes]]] = {}

    for log in reversed(args.logs):
        # 后传入的日志优先（通常是最新一次抓取）
        text = log.read_text(errors="ignore")
        for m in META_RE.finditer(text):
            label, w, h, stride = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
            meta.setdefault(label, (w, h, stride))

        for m in ROW_RE.finditer(text):
            label, row = m.group(1), int(m.group(2))
            try:
                data = base64.b64decode(m.group(3), validate=False)
            except Exception:
                continue
            if len(data) < 2:
                continue
            rows.setdefault(label, {}).setdefault(row, []).append(data)

    if not meta:
        print("no SHOTMETA found")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    for label, (w, h, stride) in meta.items():
        got = rows.get(label, {})
        clean: dict[int, bytes] = {}
        for y, candidates in got.items():
            for data in candidates:
                if len(data) == w * 2:
                    clean[y] = data
                    break
        missing = [y for y in range(h) if y not in clean]
        if missing:
            print(f"{label}: incomplete ({h - len(missing)}/{h} clean rows, first missing {missing[0]})")
            if not args.partial:
                continue
            # 用最近的有效行补齐，便于在串口还在传时先看版面
            for y in missing:
                src = None
                for d in range(1, h):
                    if y - d in clean:
                        src = clean[y - d]
                        break
                    if y + d in clean:
                        src = clean[y + d]
                        break
                if src is None:
                    continue
                clean[y] = src
            missing = [y for y in range(h) if y not in clean]
            if missing:
                print(f"{label}: still missing rows {missing[:5]} after fill")
                continue
        bgr_rows = {y: rgb565_to_bgr888(clean[y]) for y in range(h)}
        path = args.out / f"{label}.bmp"
        write_bmp(path, w, h, bgr_rows)
        print(f"{label}: {path} ({w}x{h})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
