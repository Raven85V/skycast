#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Генератор цветных объёмных иконок погоды (RGB565, 110x64) на ЧИСТОМ Python.
НЕ требует установки pip / Pillow!
"""

import math
import os
import struct

W, H = 110, 64

ICONS = [
    ("01d.h", "image_data_day_clearsky",          "day_clearsky",          "01d"),
    ("01n.h", "image_data_night_clearsky",        "night_clearsky",        "01n"),
    ("02d.h", "image_data_day_fewclouds",         "day_fewclouds",         "02d"),
    ("02n.h", "image_data_night_fewclouds",       "night_fewclouds",       "02n"),
    ("03d.h", "image_data_day_scatteredclouds",   "day_scatteredclouds",   "03d"),
    ("03n.h", "image_data_night_scatteredclouds", "night_scatteredclouds", "03n"),
    ("04d.h", "image_data_day_brokenclouds",      "day_brokenclouds",      "04d"),
    ("04n.h", "image_data_night_brokenclouds",    "night_brokenclouds",    "04n"),
    ("09d.h", "image_data_day_rain",              "day_rain",              "09d"),
    ("09n.h", "image_data_night_rain",            "night_rain",            "09n"),
    ("10d.h", "image_data_day_showerrain",        "day_showerrain",        "10d"),
    ("10n.h", "image_data_night_showerrain",      "night_showerrain",      "10n"),
    ("11d.h", "image_data_day_thunderstorm",      "day_thunderstorm",      "11d"),
    ("11n.h", "image_data_night_thunderstorm",    "night_thunderstorm",    "11n"),
    ("13d.h", "image_data_day_snow",              "day_snow",              "13d"),
    ("13n.h", "image_data_night_snow",            "night_snow",            "13n"),
    ("50d.h", "image_data_day_mist",              "day_mist",              "50d"),
    ("50n.h", "image_data_night_mist",            "night_mist",            "50n"),
    ("weather.h", "image_data_weather",           "weather",               "weather"),
]


def clamp(v, lo=0.0, hi=1.0):
    return max(lo, min(hi, v))


def lerp(a, b, t):
    return a + (b - a) * clamp(t)


def lerp_col(c1, c2, t):
    t = clamp(t)
    return (
        lerp(c1[0], c2[0], t),
        lerp(c1[1], c2[1], t),
        lerp(c1[2], c2[2], t),
    )


def blend(dst, src, alpha):
    a = clamp(alpha)
    return (
        dst[0] * (1.0 - a) + src[0] * a,
        dst[1] * (1.0 - a) + src[1] * a,
        dst[2] * (1.0 - a) + src[2] * a,
    )


def sd_Segment(px, py, ax, ay, bx, by):
    vx, vy = px - ax, py - ay
    ux, uy = bx - ax, by - ay
    denom = ux * ux + uy * uy
    t = clamp((vx * ux + vy * uy) / denom) if denom > 0 else 0.0
    dx = vx - ux * t
    dy = vy - uy * t
    return math.hypot(dx, dy)


def sd_cloud(x, y, cx, cy, s):
    """Знаковое расстояние до фигуры облака (отрицательное внутри)"""
    nx = (x - cx) / s
    ny = (y - cy) / s
    # Три круглых купола + скруглённое основание
    d1 = math.hypot(nx + 0.52, ny - 0.06) - 0.36
    d2 = math.hypot(nx - 0.02, ny + 0.14) - 0.46
    d3 = math.hypot(nx - 0.54, ny - 0.04) - 0.34
    # Капсула основания
    bx = max(abs(nx) - 0.56, 0.0)
    by = ny - 0.14
    db = math.hypot(bx, by) - 0.28
    return min(d1, d2, d3, db) * s


def paint_sun(buf, cx, cy, r):
    outer = r * 1.48
    inner = r * 1.10
    for y in range(max(0, int(cy - outer - 2)), min(H, int(cy + outer + 3))):
        for x in range(max(0, int(cx - outer - 2)), min(W, int(cx + outer + 3))):
            dx, dy = x - cx, y - cy
            dist = math.hypot(dx, dy)
            col = buf[y][x]

            # 12 лучей
            if inner - 1.0 <= dist <= outer + 1.0:
                ang = math.atan2(dy, dx)
                sector = (ang % (math.pi / 6.0)) - (math.pi / 12.0)
                half_w = 0.20 * (1.0 - (dist - inner) / max(1.0, outer - inner))
                ray_d = (abs(sector) - half_w) * dist
                ray_a = clamp(0.8 - ray_d) * clamp(outer - dist + 0.5) * clamp(dist - inner + 0.8)
                if ray_a > 0:
                    col = blend(col, (255, 185, 35), ray_a)

            # Диск солнца с радиальным градиентом
            disk_a = clamp(r + 0.6 - dist)
            if disk_a > 0:
                t = clamp(dist / r)
                if t < 0.5:
                    sc = lerp_col((255, 248, 205), (255, 210, 74), t * 2.0)
                else:
                    sc = lerp_col((255, 210, 74), (255, 150, 24), (t - 0.5) * 2.0)
                col = blend(col, sc, disk_a)

            buf[y][x] = col


def paint_moon(buf, cx, cy, r, stars=True):
    cut_cx = cx + r * 0.44
    cut_cy = cy - r * 0.22
    cut_r = r * 0.84
    for y in range(max(0, int(cy - r - 2)), min(H, int(cy + r + 3))):
        for x in range(max(0, int(cx - r - 2)), min(W, int(cx + r + 3))):
            d1 = math.hypot(x - cx, y - cy) - r
            d2 = cut_r - math.hypot(x - cut_cx, y - cut_cy)
            d = max(d1, d2)
            a = clamp(0.6 - d)
            if a > 0:
                t = (y - (cy - r)) / (2.0 * r)
                mc = lerp_col((255, 244, 180), (255, 170, 30), t)
                buf[y][x] = blend(buf[y][x], mc, a)

    if stars:
        for sx, sy, sr in [(cx + r * 1.15, cy - r * 0.50, 2.3),
                           (cx - r * 1.10, cy - r * 0.30, 1.8),
                           (cx + r * 0.85, cy + r * 0.70, 1.6)]:
            for y in range(max(0, int(sy - 4)), min(H, int(sy + 5))):
                for x in range(max(0, int(sx - 4)), min(W, int(sx + 5))):
                    d = (abs(x - sx) + abs(y - sy)) - sr
                    a = clamp(0.7 - d)
                    if a > 0:
                        buf[y][x] = blend(buf[y][x], (255, 232, 130), a)


def paint_cloud(buf, cx, cy, s, style="day"):
    if style == "day":
        top_c, bot_c, edge_c, hl_a = (242, 250, 255), (105, 182, 240), (62, 134, 196), 0.42
    elif style == "day_back":
        top_c, bot_c, edge_c, hl_a = (195, 225, 248), (76, 146, 210), (50, 110, 168), 0.25
    elif style == "night":
        top_c, bot_c, edge_c, hl_a = (152, 184, 224), (44, 92, 158), (28, 62, 112), 0.28
    else:
        top_c, bot_c, edge_c, hl_a = (110, 142, 190), (32, 68, 126), (20, 46, 88), 0.18

    y_top = cy - s * 0.60
    y_bot = cy + s * 0.44
    for y in range(max(0, int(cy - s * 0.8)), min(H, int(cy + s * 0.65))):
        for x in range(max(0, int(cx - s * 1.1)), min(W, int(cx + s * 1.1))):
            d = sd_cloud(x, y, cx, cy, s)
            outer_a = clamp(0.7 - (d - 1.0))
            if outer_a <= 0:
                continue
            inner_a = clamp(0.6 - d)
            t = (y - y_top) / max(1.0, (y_bot - y_top))
            fill_c = lerp_col(top_c, bot_c, t)

            # Мягкий блик в верхней части облака
            hl_d = math.hypot((x - (cx - s * 0.05)) / (s * 0.38), (y - (cy - s * 0.24)) / (s * 0.22))
            if hl_d < 1.0:
                fill_c = blend(fill_c, (255, 255, 255), (1.0 - hl_d) * hl_a)

            c = blend(edge_c, fill_c, inner_a)
            buf[y][x] = blend(buf[y][x], c, outer_a)


def paint_raindrop(buf, cx, cy, r=3.8):
    ax, ay = cx + r * 0.45, cy - r * 1.35
    bx, by = cx - r * 0.35, cy + r * 0.55
    for y in range(max(0, int(cy - r * 2)), min(H, int(cy + r * 2 + 2))):
        for x in range(max(0, int(cx - r * 2)), min(W, int(cx + r * 2 + 2))):
            vx, vy = x - ax, y - ay
            ux, uy = bx - ax, by - ay
            t = clamp((vx * ux + vy * uy) / (ux * ux + uy * uy))
            rad = r * (0.22 + 0.78 * t)
            d = math.hypot(vx - ux * t, vy - uy * t) - rad
            a = clamp(0.65 - d)
            if a > 0:
                col = lerp_col((216, 242, 255), (26, 132, 216), t)
                buf[y][x] = blend(buf[y][x], col, a)


def paint_snowflake(buf, cx, cy, r=4.6):
    for y in range(max(0, int(cy - r - 2)), min(H, int(cy + r + 3))):
        for x in range(max(0, int(cx - r - 2)), min(W, int(cx + r + 3))):
            dmin = 999.0
            for i in range(3):
                ang = i * math.pi / 3.0
                dx, dy = math.cos(ang) * r, math.sin(ang) * r
                dmin = min(dmin, sd_Segment(x, y, cx - dx, cy - dy, cx + dx, cy + dy))
            a = clamp(1.15 - dmin)
            if a > 0:
                t = (y - (cy - r)) / (2.0 * r)
                col = lerp_col((255, 255, 255), (168, 224, 250), t)
                buf[y][x] = blend(buf[y][x], col, a)


def paint_lightning(buf, cx, cy):
    segs = [
        (cx + 2.0, cy - 10.5, cx - 4.5, cy - 0.5),
        (cx - 4.5, cy - 0.5,  cx + 1.5, cy - 0.5),
        (cx + 1.5, cy - 0.5,  cx - 3.5, cy + 11.5),
    ]
    for y in range(max(0, int(cy - 13)), min(H, int(cy + 14))):
        for x in range(max(0, int(cx - 8)), min(W, int(cx + 8))):
            dmin = min(sd_Segment(x, y, x1, y1, x2, y2) for x1, y1, x2, y2 in segs)
            a = clamp(1.75 - dmin)
            if a > 0:
                t = (y - (cy - 10.5)) / 22.0
                col = lerp_col((255, 246, 176), (255, 163, 16), t)
                buf[y][x] = blend(buf[y][x], col, a)


def paint_mist(buf, night=False):
    top_c = (198, 216, 238) if night else (245, 252, 255)
    bot_c = (86, 133, 196) if night else (139, 197, 240)
    bars = [(28, 39, 82), (22, 46, 88), (30, 53, 80)]
    for x1, yc, x2 in bars:
        for y in range(yc - 3, yc + 4):
            for x in range(x1 - 3, x2 + 4):
                if 0 <= x < W and 0 <= y < H:
                    d = sd_Segment(x, y, x1, yc, x2, yc) - 1.8
                    a = clamp(0.65 - d)
                    if a > 0:
                        t = (y - 36) / 20.0
                        buf[y][x] = blend(buf[y][x], lerp_col(top_c, bot_c, t), a)


def render_icon(kind):
    buf = [[(0.0, 0.0, 0.0) for _ in range(W)] for _ in range(H)]
    night = kind.endswith("n")

    if kind == "01d":
        paint_sun(buf, 55, 32, 17.0)
    elif kind == "01n":
        paint_moon(buf, 55, 32, 18.0, stars=True)
    elif kind in ("02d", "02n", "weather"):
        if night:
            paint_moon(buf, 41, 24, 13.0, stars=False)
            paint_cloud(buf, 61, 37, 25.0, "night")
        else:
            paint_sun(buf, 40, 24, 13.0)
            paint_cloud(buf, 61, 37, 25.0, "day")
    elif kind in ("03d", "03n"):
        paint_cloud(buf, 55, 34, 28.0, "night" if night else "day")
    elif kind in ("04d", "04n"):
        paint_cloud(buf, 46, 26, 22.0, "night_back" if night else "day_back")
        paint_cloud(buf, 62, 37, 25.5, "night" if night else "day")
    elif kind in ("09d", "09n"):
        paint_cloud(buf, 55, 26, 25.0, "night" if night else "day")
        for dx, dy in [(41, 50), (54, 53), (67, 50)]:
            paint_raindrop(buf, dx, dy, 3.6)
    elif kind in ("10d", "10n"):
        if night:
            paint_moon(buf, 38, 20, 11.0, stars=False)
        else:
            paint_sun(buf, 38, 20, 11.0)
        paint_cloud(buf, 60, 28, 24.0, "night" if night else "day")
        for dx, dy in [(44, 51), (56, 54), (68, 51)]:
            paint_raindrop(buf, dx, dy, 3.6)
    elif kind in ("11d", "11n"):
        paint_cloud(buf, 55, 25, 25.0, "night" if night else "day")
        paint_lightning(buf, 56, 48)
        paint_raindrop(buf, 41, 50, 3.2)
        paint_raindrop(buf, 70, 50, 3.2)
    elif kind in ("13d", "13n"):
        paint_cloud(buf, 55, 25, 25.0, "night" if night else "day")
        for sx, sy in [(42, 50), (55, 53), (68, 50)]:
            paint_snowflake(buf, sx, sy, 4.5)
    elif kind in ("50d", "50n"):
        paint_cloud(buf, 55, 23, 23.0, "night" if night else "day")
        paint_mist(buf, night=night)

    return buf


def to_rgb565(r, g, b):
    ri, gi, bi = int(round(r)), int(round(g)), int(round(b))
    if ri < 6 and gi < 6 and bi < 6:
        return 0x0000
    return ((ri & 0xF8) << 8) | ((gi & 0xFC) << 3) | (bi >> 3)


def write_header(filepath, arr_name, struct_name, buf):
    lines = [
        "/*",
        " * Colour weather icon (RGB565, 110x64)",
        " */",
        "#include <stdint.h>",
        "",
        f"static const uint16_t {arr_name}[7040] = {{",
    ]
    for y in range(H):
        row = [f"0x{to_rgb565(*buf[y][x]):04x}" for x in range(W)]
        comma = "," if y < H - 1 else ""
        lines.append("    " + ", ".join(row) + comma)
    lines += [
        "};",
        f"const tImage {struct_name} = {{ {arr_name}, 110, 64, 16 }};",
        "",
    ]
    with open(filepath, "wb") as f:
        f.write("\r\n".join(lines).encode("utf-8"))


def save_bmp(filename, sheet_w, sheet_h, sheet):
    """Сохранение превью в стандартный 24-битный BMP без сторонних библиотек"""
    row_pad = (4 - (sheet_w * 3) % 4) % 4
    img_size = (sheet_w * 3 + row_pad) * sheet_h
    file_size = 54 + img_size
    with open(filename, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", file_size, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, sheet_w, sheet_h, 1, 24, 0, img_size, 2835, 2835, 0, 0))
        for y in range(sheet_h - 1, -1, -1):
            row_bytes = bytearray()
            for x in range(sheet_w):
                r, g, b = sheet[y][x]
                row_bytes.extend((int(round(b)), int(round(g)), int(round(r))))
            row_bytes.extend(b"\x00" * row_pad)
            f.write(row_bytes)


def main():
    out_dir = os.path.join("src", "icons")
    os.makedirs(out_dir, exist_ok=True)

    cols, rows = 5, 4
    sheet_w, sheet_h = W * cols, H * rows
    sheet = [[(0, 0, 0) for _ in range(sheet_w)] for _ in range(sheet_h)]

    for idx, (fname, arr_name, struct_name, kind) in enumerate(ICONS):
        buf = render_icon(kind)
        write_header(os.path.join(out_dir, fname), arr_name, struct_name, buf)
        ox, oy = (idx % cols) * W, (idx // cols) * H
        for y in range(H):
            for x in range(W):
                sheet[oy + y][ox + x] = buf[y][x]
        print(f"Готово: {fname} ({struct_name})")

    save_bmp("preview_icons.bmp", sheet_w, sheet_h, sheet)
    print("\nВсе 19 файлов записаны в src/icons/. Превью сохранено в preview_icons.bmp")


if __name__ == "__main__":
    main()