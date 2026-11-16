#!/usr/bin/env python3
"""把 Fluent Emoji 3D PNG 转成 LVGL RGB565A8 C 数组。

用法：
  python3 png_to_lvgl.py <输入目录> <输出目录>

输入目录需包含（208x208 RGBA PNG，正脸猫表情）：
  idle happy excited thinking speaking sleepy sad love

输出：
  pip_faces.c / pip_faces.h
"""
import os
import struct
import sys
import zlib

NAMES = ["idle", "happy", "excited", "thinking", "speaking", "sleepy", "sad", "love"]


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos = 8
    idat = b""
    width = height = depth = color_type = None
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        chunk_type = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            width, height, depth, color_type, _, _, _ = struct.unpack(">IIBBBBB", chunk)
        elif chunk_type == b"IDAT":
            idat += chunk
        elif chunk_type == b"IEND":
            break
    assert depth == 8 and color_type == 6, "expect 8-bit RGBA"
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray(height * stride)
    prev = bytearray(stride)
    p = 0

    def paeth(a, b, c):
        pa = abs(b - c)
        pb = abs(a - c)
        pc = abs(a + b - 2 * c)
        if pa <= pb and pa <= pc:
            return a
        return b if pb <= pc else c

    for y in range(height):
        filt = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if filt == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                line[i] = (line[i] + paeth(a, b, c)) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, out


def to_rgb565a8(width, height, rgba):
    pixels = bytearray(width * height * 2)
    alpha = bytearray(width * height)
    for i in range(width * height):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        pixels[i * 2] = value & 0xFF
        pixels[i * 2 + 1] = (value >> 8) & 0xFF
        alpha[i] = a
    return bytes(pixels) + bytes(alpha)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    src_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    with open(os.path.join(out_dir, "pip_faces.h"), "w") as f:
        f.write("/* Fluent Emoji 3D (MIT) - 见 resources/ATTRIBUTION-fluent-emoji.md */\n")
        f.write("#pragma once\n#include \"lvgl.h\"\n\n")
        for name in NAMES:
            f.write(f"extern const lv_image_dsc_t pip_face_{name};\n")

    with open(os.path.join(out_dir, "pip_faces.c"), "w") as f:
        f.write("/* Microsoft Fluent Emoji 3D, MIT License. RGB565A8. */\n")
        f.write("#include \"lvgl.h\"\n#include \"pip_faces.h\"\n\n")
        for name in NAMES:
            width, height, rgba = read_png(os.path.join(src_dir, f"{name}.png"))
            data = to_rgb565a8(width, height, rgba)
            f.write(f"static const uint8_t pip_face_{name}_map[] LV_ATTRIBUTE_MEM_ALIGN = {{\n")
            for i in range(0, len(data), 16):
                f.write("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
            f.write("};\n\n")
            f.write(f"const lv_image_dsc_t pip_face_{name} = {{\n")
            f.write("  .header.magic = LV_IMAGE_HEADER_MAGIC,\n")
            f.write("  .header.cf = LV_COLOR_FORMAT_RGB565A8,\n")
            f.write(f"  .header.w = {width},\n  .header.h = {height},\n")
            f.write(f"  .header.stride = {width * 2},\n")
            f.write(f"  .data_size = {len(data)},\n  .data = pip_face_{name}_map,\n}};\n\n")
            print(name, width, height, len(data))


if __name__ == "__main__":
    main()
