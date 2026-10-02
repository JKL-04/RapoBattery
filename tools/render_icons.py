#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""render_icons.py — 用 Python 渲染托盘图标预览，无需编译 C++。

用途：改配色 / 改描边粗细后，快速看出真实尺寸下的效果。

管线与 src/RapoBattery.cpp 的 MakeIcon() 完全一致：
    glyphs_gen.h -> 多边形 -> 8 倍超采样有符号距离场 -> 盒式降采样

之所以需要这个脚本：某个早期 C++ 预览工具的 BMP 写出函数把 alpha 写死成
255，并把透明像素合成到固定深灰，所以它无法展示浅色任务栏下的效果。
本脚本直接输出带真实 alpha 的 RGBA 数据。

用法：
    python tools/render_icons.py                # 输出到 assets/
    python tools/render_icons.py --out <目录>
"""
import io, os, re, sys, math

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
GEN = os.path.join(REPO, "src", "glyphs_gen.h")

OUT_DIR = os.path.join(REPO, "assets")
for _i, _a in enumerate(sys.argv):
    if _a == "--out" and _i + 1 < len(sys.argv):
        OUT_DIR = sys.argv[_i + 1]

SS = 8                      # 超采样倍数（与 C++ 的 ICON_SS 一致）
SIZE = 20                   # 125% 缩放下的托盘图标像素尺寸
D = SIZE * SS

# 只有红绿两档：<=20% 红，其余绿（未知状态灰）
COL_LOW  = (255, 0, 0)      # #FF0000
COL_HIGH = (97, 201, 18)    # #61C912
COL_UNK  = (165, 165, 165)
OUTLINE  = (0, 0, 0)
OUTLINE_PX = 1.0


# ─────────────────── 解析 glyphs_gen.h ───────────────────
def parse_glyphs(path):
    src = io.open(path, encoding="utf-8").read()
    glyphs = {}
    for m in re.finditer(r"static const float (g_\w+)_pts\[\] = \{(.*?)\};", src, re.S):
        name, body = m.group(1), m.group(2)
        nums = [float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", body)]
        glyphs.setdefault(name, {})["pts"] = list(zip(nums[0::2], nums[1::2]))
    for m in re.finditer(r"static const GlyphPoly (g_\w+)_polys\[\] = \{(.*?)\};", src, re.S):
        name, body = m.group(1), m.group(2)
        glyphs.setdefault(name, {})["polys"] = [
            (int(a) // 2, int(b)) for a, b in
            re.findall(r"\{\s*g_\w+_pts\s*\+\s*(\d+)\s*,\s*(\d+)\s*\}", body)]
    for m in re.finditer(r"static const Glyph (g_\w+) = \{([^}]*)\};", src):
        name, body = m.group(1), m.group(2)
        v = [float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", body)]
        glyphs.setdefault(name, {})["box"] = v[:8]
    return glyphs


G = parse_glyphs(GEN)


def glyph_polys(name):
    g = G[name]
    flat, polys, box = g["pts"], g["polys"], g["box"]
    return [flat[s:s + n] for s, n in polys], (box[4], box[5], box[6], box[7])


# ─────────────────── 单次求值的描边填充 ───────────────────
def blend(px, x, y, col, cov):
    if cov <= 0 or x < 0 or y < 0 or x >= D or y >= D:
        return
    cov = min(1.0, cov)
    sr, sg, sb = col
    d = px[y][x]
    da = d[3] / 255.0
    oa = cov + da * (1 - cov)
    if oa <= 0:
        px[y][x] = (0, 0, 0, 0)
        return
    px[y][x] = (int((sr * cov + d[0] * da * (1 - cov)) / oa + 0.5),
                int((sg * cov + d[1] * da * (1 - cov)) / oa + 0.5),
                int((sb * cov + d[2] * da * (1 - cov)) / oa + 0.5),
                int(oa * 255 + 0.5))


def fill_glyph_outline(px, name, x0, y0, w, h, fill, stroke, stroke_px):
    polys, (minX, minY, maxX, maxY) = glyph_polys(name)
    bw, bh = maxX - minX, maxY - minY
    if bw <= 0 or bh <= 0 or w <= 0 or h <= 0:
        return
    sx, sy = w / bw, h / bh
    mapped = [[(x0 + (x - minX) * sx, y0 + (y - minY) * sy) for x, y in poly]
              for poly in polys]

    xs = [p[0] for poly in mapped for p in poly]
    ys = [p[1] for poly in mapped for p in poly]
    for py in range(int(math.floor(min(ys) - stroke_px)) - 1,
                    int(math.ceil(max(ys) + stroke_px)) + 2):
        yy = py + 0.5
        for pxx in range(int(math.floor(min(xs) - stroke_px)) - 1,
                         int(math.ceil(max(xs) + stroke_px)) + 2):
            xx = pxx + 0.5
            wind = 0
            minD = 1e9
            for poly in mapped:
                n = len(poly)
                for a in range(n):
                    b = (a - 1) % n
                    ax, ay = poly[b]
                    bx, by = poly[a]
                    if (ay > yy) != (by > yy):
                        t = (yy - ay) / (by - ay)
                        if ax + t * (bx - ax) > xx:
                            wind += 1 if by > ay else -1
                    dx, dy = bx - ax, by - ay
                    L2 = dx * dx + dy * dy
                    u = max(0.0, min(1.0, ((xx - ax) * dx + (yy - ay) * dy) / L2)) if L2 else 0.0
                    qx, qy = ax + u * dx - xx, ay + u * dy - yy
                    dist = math.hypot(qx, qy)
                    if dist < minD:
                        minD = dist
            inside = wind != 0
            # 有符号距离覆盖率：边界处两者都是 0.5，内部远离边界时达到 1.0。
            # 注意不能写成 clamp(0.5 - d) + 0.5 —— 那样内部会被压到恒定的 0.5。
            cov = min(1.0, max(0.0, 0.5 + minD)) if inside else min(1.0, max(0.0, 0.5 - minD))
            if cov <= 0:
                continue
            if inside:
                blend(px, pxx, py, fill, cov)
            else:
                if stroke_px <= 0:
                    continue
                t = min(1.0, max(0.0, minD / stroke_px))
                if t >= 0.999:
                    continue
                blend(px, pxx, py, stroke, cov * (1.0 - t))


def round_bar(px, ax, ay, bx, by, r, col):
    for py in range(int(math.floor(min(ay, by) - r - 1)),
                    int(math.ceil(max(ay, by) + r + 1)) + 1):
        for pxx in range(int(math.floor(min(ax, bx) - r - 1)),
                         int(math.ceil(max(ax, bx) + r + 1)) + 1):
            xx, yy = pxx + 0.5, py + 0.5
            dx, dy = bx - ax, by - ay
            L2 = dx * dx + dy * dy
            u = max(0.0, min(1.0, ((xx - ax) * dx + (yy - ay) * dy) / L2)) if L2 else 0.0
            qx, qy = ax + u * dx - xx, ay + u * dy - yy
            blend(px, pxx, py, col, min(1.0, max(0.0, 0.5 - (math.hypot(qx, qy) - r))))


def render_icon(battery, outline_px=OUTLINE_PX):
    """返回一个 20x20 的 RGBA 图标（PIL Image）。"""
    from PIL import Image
    big = [[(0, 0, 0, 0)] * D for _ in range(D)]
    U = D / SIZE

    if battery < 0:
        col = COL_UNK
    elif battery <= 20:
        col = COL_LOW
    else:
        col = COL_HIGH

    text = "--" if battery < 0 else str(battery)
    n = len(text)

    marginX = 0.2 * U
    availW = D - marginX * 2

    uniAR = 0.72
    for i in range(10):
        nm = "g_%d" % i
        if nm in G:
            _, (a, b, c, d) = glyph_polys(nm)
            if d - b > 0:
                uniAR = max(uniAR, (c - a) / (d - b))

    gapRatio, baseDigits = 0.04, 2
    baseH = D - 1.0 * U
    need = baseH * (uniAR * baseDigits + gapRatio * (baseDigits - 1))
    if need > availW:
        baseH = availW / (uniAR * baseDigits + gapRatio * (baseDigits - 1))
    glyphH = baseH
    if glyphH * (uniAR * n + gapRatio * (n - 1)) > availW:
        glyphH = availW / (uniAR * n + gapRatio * (n - 1))

    glyphW = glyphH * uniAR
    gapW = glyphH * gapRatio
    totalW = glyphW * n + gapW * (n - 1)
    ox = max(marginX, (D - totalW) * 0.5)
    oy = (D - glyphH) * 0.5

    for ch in text:
        if ch == "-":
            bx0, bx1 = ox + glyphW * 0.08, ox + glyphW * 0.92
            byy = oy + glyphH * 0.5
            rBar, sOff = 1.25 * U, outline_px * U
            for dx in (-1, 1):
                for dy in (-1, 1):
                    round_bar(big, bx0+dx*sOff, byy+dy*sOff, bx1+dx*sOff, byy+dy*sOff, rBar, OUTLINE)
            for d in (-1, 1):
                round_bar(big, bx0+d*sOff, byy, bx1+d*sOff, byy, rBar, OUTLINE)
                round_bar(big, bx0, byy+d*sOff, bx1, byy+d*sOff, rBar, OUTLINE)
            round_bar(big, bx0, byy, bx1, byy, rBar, col)
        else:
            nm = "g_%s" % ch
            if nm in G:
                _, (a, b, c, d) = glyph_polys(nm)
                gw, gh = c - a, d - b
                w = glyphH * (gw / gh) if gh > 0 else glyphW
                fill_glyph_outline(big, nm, ox + (glyphW - w) * 0.5, oy, w, glyphH,
                                   col, OUTLINE, outline_px * U)
        ox += glyphW + gapW

    img = Image.new("RGBA", (SIZE, SIZE))
    pxl = img.load()
    N = SS * SS
    for y in range(SIZE):
        for x in range(SIZE):
            a = r = g = b = 0
            for sy in range(SS):
                for sx in range(SS):
                    cr, cg, cb, ca = big[y*SS+sy][x*SS+sx]
                    a += ca
                    r += cr * ca; g += cg * ca; b += cb * ca
            pxl[x, y] = (r // a if a else 0, g // a if a else 0, b // a if a else 0, a // N)
    return img


def contact_sheet(bg=(32, 32, 32), gap=6):
    """把 100/92/87/45/20/15/7/未知 拼成一张图。"""
    from PIL import Image
    samples = [100, 92, 87, 45, 20, 15, 7, -1]
    tiles = [render_icon(v) for v in samples]
    w = len(tiles) * SIZE + (len(tiles) + 1) * gap
    h = SIZE + 2 * gap
    sheet = Image.new("RGBA", (w, h), bg + (255,))
    for i, t in enumerate(tiles):
        sheet.alpha_composite(t, (gap + i * (SIZE + gap), gap))
    return sheet


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    for bg, nm in (((32, 32, 32), "dark"), ((243, 243, 243), "light")):
        p = os.path.join(OUT_DIR, "sheet-%s.png" % nm)
        contact_sheet(bg).convert("RGB").save(p)
        print("wrote", p)
