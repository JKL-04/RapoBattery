#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""svg2ico.py — 把单个 SVG 图标转成多尺寸 Windows ICO，用作 exe 的应用图标。

复用 svg2cpp.py 的路径解析与曲线展平逻辑。

填充规则用【奇偶规则】(even-odd)：统计从该点向右的射线与所有子路径的
交点总数，奇数表示在内部。SVG 的默认填充规则就是 nonzero，但这类图标包
的图形等价于 even-odd，且 even-odd 能正确处理“外轮廓 + 内部镂空”的结构。
注意不能对每个子路径单独调用 ImageDraw.polygon —— 那样会把外轮廓整个填实，
丢失内部细节（本脚本第一版就踩了这个坑）。

Windows 应用图标惯例包含这些尺寸：
    16   资源管理器小图标 / 标题栏
    24   125% 缩放的小图标
    32   任务栏 / Alt-Tab
    48   资源管理器图标视图
    64   大图标视图
    256  “超大图标”视图（ICO 内以 PNG 压缩存放）

用法：
    python tools/svg2ico.py <输入.svg> <输出.ico> [RRGGBB]
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import svg2cpp  # noqa: E402

SIZES = [16, 24, 32, 48, 64, 256]
SS = 4          # 超采样倍数（4 倍足够，256 尺寸下每个子像素 1/4 像素）


def render(svg_path, size, rgb):
    from PIL import Image

    box, d = svg2cpp.read_svg(svg_path)
    if not d:
        raise SystemExit("SVG 里找不到 <path d=...>: %s" % svg_path)

    subs = svg2cpp.path_to_subpaths(d)
    pts_all = [p for sub in subs for p in sub]
    if not pts_all:
        raise SystemExit("路径解析后没有任何点: %s" % svg_path)

    minx = min(p[0] for p in pts_all)
    maxx = max(p[0] for p in pts_all)
    miny = min(p[1] for p in pts_all)
    maxy = max(p[1] for p in pts_all)
    bw, bh = maxx - minx, maxy - miny

    S = size * SS
    scale = min(S / bw, S / bh)
    ox = (S - bw * scale) / 2.0
    oy = (S - bh * scale) / 2.0

    # 映射到画布坐标
    mapped = []
    for sub in subs:
        mapped.append([(ox + (x - minx) * scale, oy + (y - miny) * scale) for x, y in sub])

    allx = [p[0] for s in mapped for p in s]
    ally = [p[1] for s in mapped for p in s]
    x0 = max(0, int(min(allx)) - 1)
    x1 = min(S - 1, int(max(allx)) + 1)
    y0 = max(0, int(min(ally)) - 1)
    y1 = min(S - 1, int(max(ally)) + 1)

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    px = img.load()

    r, g, b = rgb
    for y in range(y0, y1 + 1):
        yy = y + 0.5
        for x in range(x0, x1 + 1):
            xx = x + 0.5
            crossings = 0
            for poly in mapped:
                n = len(poly)
                for i in range(n):
                    ax, ay = poly[i]
                    bx, by = poly[(i + 1) % n]
                    if (ay > yy) != (by > yy):
                        t = (yy - ay) / (by - ay)
                        if ax + t * (bx - ax) > xx:
                            crossings += 1
            if crossings & 1:          # 奇偶规则：奇数在内
                px[x, y] = (r, g, b, 255)

    return img.resize((size, size), Image.LANCZOS)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    svg_path = sys.argv[1]
    ico_path = sys.argv[2]
    rgb = (0x20, 0x20, 0x20)
    if len(sys.argv) > 3:
        v = int(sys.argv[3], 16)
        rgb = ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)

    if not os.path.exists(svg_path):
        print("找不到 SVG: %s" % svg_path)
        return 1

    frames = []
    for s in SIZES:
        im = render(svg_path, s, rgb)
        frames.append(im)
        print("  渲染 %dx%d" % (s, s))

    os.makedirs(os.path.dirname(ico_path) or ".", exist_ok=True)
    frames[-1].save(ico_path, format="ICO",
                    sizes=[(f.width, f.height) for f in frames])
    print("已写出 %s（%d 字节，含 %d 种尺寸）"
          % (ico_path, os.path.getsize(ico_path), len(frames)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
