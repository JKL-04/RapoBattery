#!/usr/bin/env python3
"""
svg2cpp.py — 把图标 SVG 转成 C++ 多边形数据。

源：游戏图标包 v1.4 SVG（24x24 viewBox，单条 path，纯填充）
出：glyphs_gen.h —— 每个字形一组多边形轮廓（viewBox 坐标）+ 包围盒

支持的 path 指令：M m L l H h V v C c S s Q q T t A a Z z
用 de Casteljau / 圆弧参数化展平为折线，容差可控。
"""
import os
import re
import sys
import math
import json

# 脚本位于 <repo>/tools/，上一级即仓库根目录
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

SVG_DIR = os.path.join(REPO, "assets", "svg")
OUT_H   = os.path.join(REPO, "src", "glyphs_gen.h")

# 允许命令行覆盖：--svg <目录> --out <文件>
for _i, _a in enumerate(sys.argv):
    if _a == "--svg" and _i + 1 < len(sys.argv): SVG_DIR = sys.argv[_i + 1]
    if _a == "--out" and _i + 1 < len(sys.argv): OUT_H   = sys.argv[_i + 1]

# 逻辑名 -> 源文件名（相对于 SVG_DIR）
FILES = {
    "0": "digit-0.svg",
    "1": "digit-1.svg",
    "2": "digit-2.svg",
    "3": "digit-3.svg",
    "4": "digit-4.svg",
    "5": "digit-5.svg",
    "6": "digit-6.svg",
    "7": "digit-7.svg",
    "8": "digit-8.svg",
    "9": "digit-9.svg",
    "bolt": "bolt.svg",
    "warn": "warn.svg",
}

NUM = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
TOKEN = re.compile(r"([MmLlHhVvCcSsQqTtAaZz])|([-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?)")


def parse_path(d):
    """把 path 的 d 属性解析成 [(cmd, [args...]), ...]"""
    out = []
    i = 0
    cur = None
    args = []
    for m in TOKEN.finditer(d):
        cmd, num = m.group(1), m.group(2)
        if cmd:
            if cur is not None:
                out.append((cur, args))
            cur = cmd
            args = []
        else:
            args.append(float(num))
    if cur is not None:
        out.append((cur, args))
    return out


def flatten_cubic(p0, p1, p2, p3, tol=0.02):
    """自适应展平三次贝塞尔，返回中间点（不含 p0，含 p3）。"""
    def flat_enough(a, b, c, dd):
        ux = 3 * b[0] - 2 * a[0] - dd[0]
        uy = 3 * b[1] - 2 * a[1] - dd[1]
        vx = 3 * c[0] - 2 * dd[0] - a[0]
        vy = 3 * c[1] - 2 * dd[1] - a[1]
        return max(ux * ux, vx * vx) + max(uy * uy, vy * vy) <= 16 * tol * tol

    pts = []

    def rec(a, b, c, dd, depth):
        if depth > 18 or flat_enough(a, b, c, dd):
            pts.append(dd)
            return
        ab = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        bc = ((b[0] + c[0]) / 2, (b[1] + c[1]) / 2)
        cd = ((c[0] + dd[0]) / 2, (c[1] + dd[1]) / 2)
        abc = ((ab[0] + bc[0]) / 2, (ab[1] + bc[1]) / 2)
        bcd = ((bc[0] + cd[0]) / 2, (bc[1] + cd[1]) / 2)
        mid = ((abc[0] + bcd[0]) / 2, (abc[1] + bcd[1]) / 2)
        rec(a, ab, abc, mid, depth + 1)
        rec(mid, bcd, cd, dd, depth + 1)

    rec(p0, p1, p2, p3, 0)
    return pts


def flatten_arc(x1, y1, rx, ry, phi_deg, large_arc, sweep, x2, y2, tol=0.02):
    """SVG 椭圆弧 -> 折线。按 SVG 规范 F.6.5 做端点参数化。"""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    phi = math.radians(phi_deg)
    cosp, sinp = math.cos(phi), math.sin(phi)

    dx2, dy2 = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cosp * dx2 + sinp * dy2
    y1p = -sinp * dx2 + cosp * dy2

    rx, ry = abs(rx), abs(ry)
    lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx *= s
        ry *= s

    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    if den == 0:
        return [(x2, y2)]
    co = math.sqrt(max(0.0, num / den))
    if large_arc == sweep:
        co = -co
    cxp = co * rx * y1p / ry
    cyp = -co * ry * x1p / rx

    cx = cosp * cxp - sinp * cyp + (x1 + x2) / 2.0
    cy = sinp * cxp + cosp * cyp + (y1 + y2) / 2.0

    def ang(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        n = math.hypot(ux, uy) * math.hypot(vx, vy)
        if n == 0:
            return 0.0
        a = math.acos(max(-1.0, min(1.0, dot / n)))
        return -a if (ux * vy - uy * vx) < 0 else a

    th1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dth = ang((x1p - cxp) / rx, (y1p - cyp) / ry,
              (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dth > 0:
        dth -= 2 * math.pi
    elif sweep and dth < 0:
        dth += 2 * math.pi

    # 分段数按弧长估计
    steps = max(4, int(abs(dth) / (2 * math.pi) * 64))
    pts = []
    for i in range(1, steps + 1):
        t = th1 + dth * i / steps
        px = cx + rx * math.cos(t) * cosp - ry * math.sin(t) * sinp
        py = cy + rx * math.cos(t) * sinp + ry * math.sin(t) * cosp
        pts.append((px, py))
    return pts


def path_to_subpaths(d, tol=0.02):
    """把 path 转成若干闭合子路径（折线点列）。"""
    cmds = parse_path(d)
    subs = []
    cur = []
    x = y = 0.0
    sx = sy = 0.0          # 子路径起点
    px_c = py_c = 0.0      # 上一个三次控制点（供 S 使用）
    px_q = py_q = 0.0      # 上一个二次控制点（供 T 使用）
    last_cmd = ""

    def flush():
        nonlocal cur
        if len(cur) >= 3:
            subs.append(cur)
        cur = []

    for cmd, args in cmds:
        rel = cmd.islower()
        C = cmd.upper()
        i = 0
        if C == "M":
            flush()
            while i + 1 < len(args):
                nx, ny = args[i], args[i + 1]
                if rel:
                    nx += x; ny += y
                x, y = nx, ny
                if i == 0:
                    sx, sy = x, y
                    cur = [(x, y)]
                else:
                    cur.append((x, y))
                i += 2
        elif C == "L":
            while i + 1 < len(args):
                nx, ny = args[i], args[i + 1]
                if rel:
                    nx += x; ny += y
                x, y = nx, ny
                cur.append((x, y))
                i += 2
        elif C == "H":
            for v in args:
                x = x + v if rel else v
                cur.append((x, y))
        elif C == "V":
            for v in args:
                y = y + v if rel else v
                cur.append((x, y))
        elif C == "C":
            while i + 5 < len(args):
                x1, y1, x2, y2, x3, y3 = args[i:i + 6]
                if rel:
                    x1 += x; y1 += y; x2 += x; y2 += y; x3 += x; y3 += y
                cur.extend(flatten_cubic((x, y), (x1, y1), (x2, y2), (x3, y3), tol))
                px_c, py_c = x2, y2
                x, y = x3, y3
                i += 6
        elif C == "S":
            while i + 3 < len(args):
                x2, y2, x3, y3 = args[i:i + 4]
                if rel:
                    x2 += x; y2 += y; x3 += x; y3 += y
                if last_cmd.upper() in ("C", "S"):
                    x1, y1 = 2 * x - px_c, 2 * y - py_c
                else:
                    x1, y1 = x, y
                cur.extend(flatten_cubic((x, y), (x1, y1), (x2, y2), (x3, y3), tol))
                px_c, py_c = x2, y2
                x, y = x3, y3
                i += 4
        elif C == "Q":
            while i + 3 < len(args):
                x1, y1, x2, y2 = args[i:i + 4]
                if rel:
                    x1 += x; y1 += y; x2 += x; y2 += y
                # 二次转三次
                c1 = (x + 2.0 / 3 * (x1 - x), y + 2.0 / 3 * (y1 - y))
                c2 = (x2 + 2.0 / 3 * (x1 - x2), y2 + 2.0 / 3 * (y1 - y2))
                cur.extend(flatten_cubic((x, y), c1, c2, (x2, y2), tol))
                px_q, py_q = x1, y1
                x, y = x2, y2
                i += 4
        elif C == "T":
            while i + 1 < len(args):
                x2, y2 = args[i], args[i + 1]
                if rel:
                    x2 += x; y2 += y
                if last_cmd.upper() in ("Q", "T"):
                    x1, y1 = 2 * x - px_q, 2 * y - py_q
                else:
                    x1, y1 = x, y
                c1 = (x + 2.0 / 3 * (x1 - x), y + 2.0 / 3 * (y1 - y))
                c2 = (x2 + 2.0 / 3 * (x1 - x2), y2 + 2.0 / 3 * (y1 - y2))
                cur.extend(flatten_cubic((x, y), c1, c2, (x2, y2), tol))
                px_q, py_q = x1, y1
                x, y = x2, y2
                i += 2
        elif C == "A":
            while i + 6 < len(args):
                rx, ry, rot, la, sw, nx, ny = args[i:i + 7]
                if rel:
                    nx += x; ny += y
                cur.extend(flatten_arc(x, y, rx, ry, rot, int(la), int(sw), nx, ny, tol))
                x, y = nx, ny
                i += 7
        elif C == "Z":
            if cur:
                if cur[0] != cur[-1]:
                    cur.append(cur[0])
                flush()
        last_cmd = cmd

    flush()
    return subs


def read_svg(path):
    with open(path, "r", encoding="utf-8") as f:
        s = f.read()
    vb = re.search(r'viewBox="([^"]+)"', s)
    box = [float(v) for v in vb.group(1).split()] if vb else [0, 0, 24, 24]
    d = re.search(r'<path[^>]*\sd="([^"]+)"', s)
    return box, (d.group(1) if d else "")


def main():
    lines = []
    lines.append("// glyphs_gen.h —— 自动生成，请勿手工修改")
    lines.append("// 源：游戏图标包 v1.4 SVG（24x24 viewBox，纯填充路径）")
    lines.append("// 由 tools/svg2cpp.py 生成")
    lines.append("#pragma once")
    lines.append("")
    lines.append("struct GlyphPoly { const float* pts; int n; };   // pts = x0,y0,x1,y1,...")
    lines.append("struct Glyph {")
    lines.append("    float vbX, vbY, vbW, vbH;      // 原始 viewBox")
    lines.append("    float minX, minY, maxX, maxY;  // 实际内容包围盒（viewBox 坐标）")
    lines.append("    const GlyphPoly* polys; int polyCount;")
    lines.append("};")
    lines.append("")

    meta = {}
    for name, rel in FILES.items():
        src = os.path.join(SVG_DIR, rel)
        if not os.path.exists(src):
            print(f"  !! 缺失 {src}")
            continue
        box, d = read_svg(src)
        subs = path_to_subpaths(d)
        allp = [p for s in subs for p in s]
        if not allp:
            print(f"  !! {name} 无轮廓")
            continue
        minx = min(p[0] for p in allp); maxx = max(p[0] for p in allp)
        miny = min(p[1] for p in allp); maxy = max(p[1] for p in allp)
        meta[name] = dict(vb=box, bbox=[minx, miny, maxx, maxy],
                          polys=len(subs), pts=len(allp))
        print(f"  {name:5s} 轮廓={len(subs):2d} 点={len(allp):5d} "
              f"内容包围盒=({minx:.2f},{miny:.2f})-({maxx:.2f},{maxy:.2f}) "
              f"尺寸={maxx-minx:.2f}x{maxy-miny:.2f}")

        arr = f"g_{name}".replace("-", "_")
        # 收集点与各子路径的 (起点索引, 点数)
        flat = []
        polydefs = []
        for sub in subs:
            st = len(flat) // 2
            for (px, py) in sub:
                flat.append(px); flat.append(py)
            polydefs.append((st, len(sub)))

        lines.append(f"// ---- {name} ----")
        lines.append(f"static const float {arr}_pts[] = {{")
        toks = [f"{flat[i]:.4f}f,{flat[i+1]:.4f}f" for i in range(0, len(flat), 2)]
        for i in range(0, len(toks), 6):
            lines.append("    " + ", ".join(toks[i:i+6]) + ",")
        lines.append("};")
        lines.append(f"static const GlyphPoly {arr}_polys[] = {{")
        for (st, n) in polydefs:
            lines.append(f"    {{ {arr}_pts + {st*2}, {n} }},")
        lines.append("};")
        lines.append(f"static const Glyph {arr} = {{ {box[0]:.3f}f, {box[1]:.3f}f, "
                     f"{box[2]:.3f}f, {box[3]:.3f}f, {minx:.4f}f, {miny:.4f}f, "
                     f"{maxx:.4f}f, {maxy:.4f}f, {arr}_polys, {len(subs)} }};")
        lines.append("")

    # 索引表
    lines.append("// 数字 0-9 的字形表")
    lines.append("static const Glyph* const DIGIT_GLYPHS[10] = {")
    for i in range(10):
        n = str(i)
        if n in meta:
            lines.append(f"    &g_{n},")
        else:
            lines.append("    nullptr,")
    lines.append("};")
    lines.append("")
    lines.append("")

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    total_pts = sum(m["pts"] for m in meta.values())
    print(f"\n已写出 {OUT_H}")
    print(f"  字形 {len(meta)} 个，总点数 {total_pts}")
    with open(os.path.join(REPO, "assets", "glyphs_meta.json"), "w",
              encoding="utf-8") as f:
        json.dump(meta, f, indent=1)


if __name__ == "__main__":
    main()
