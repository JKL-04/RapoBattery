#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bmp2png.py — 把 BMP 转成 PNG，可放大以便观察像素。

用途：部分渲染工具只输出 BMP，本脚本转成 PNG
便于在 GitHub 或图片查看器里查看。

用法：
    python tools/bmp2png.py <输入.bmp> [输出.png] [放大倍数]
    python tools/bmp2png.py --all <目录> [放大倍数]

不指定输出时，PNG 与 BMP 同目录同名。
放大使用最近邻插值，保留像素边界（双线性会糊掉细节）。
"""
import os
import sys


def convert(src, dst=None, zoom=1):
    try:
        from PIL import Image
    except ImportError:
        print("需要 Pillow：pip install pillow")
        return False

    if not os.path.exists(src):
        print("找不到文件: %s" % src)
        return False

    im = Image.open(src).convert("RGBA")
    if zoom > 1:
        im = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)

    if dst is None:
        dst = os.path.splitext(src)[0] + ".png"
    im.save(dst)
    print("  %s -> %s  (%dx%d%s)" % (
        os.path.basename(src), os.path.basename(dst), im.width, im.height,
        ", 放大 %dx" % zoom if zoom > 1 else ""))
    return True


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1

    if args[0] == "--all":
        if len(args) < 2:
            print("用法: python tools/bmp2png.py --all <目录> [放大倍数]")
            return 1
        d = args[1]
        zoom = int(args[2]) if len(args) > 2 else 1
        if not os.path.isdir(d):
            print("不是目录: %s" % d)
            return 1
        bmps = sorted(f for f in os.listdir(d) if f.lower().endswith(".bmp"))
        if not bmps:
            print("目录里没有 .bmp 文件: %s" % d)
            return 0
        print("转换 %d 个文件：" % len(bmps))
        for f in bmps:
            convert(os.path.join(d, f), zoom=zoom)
        return 0

    src = args[0]
    dst = args[1] if len(args) > 1 else None
    zoom = int(args[2]) if len(args) > 2 else 1
    return 0 if convert(src, dst, zoom) else 1


if __name__ == "__main__":
    sys.exit(main())
