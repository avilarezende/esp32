#!/usr/bin/env python3
"""Palette-indexed RLE sprites for the CYD calico cat.

The sitting pose is the same cat as the web SVG (cream body, black left ear
and cap, orange right ear and cheek, pink inner ears). Edges are supersampled
and stored already blended with the panel background, so the firmware only
blits indices. Run from the repo root:

    python3 scripts/gen_cyd_cat.py
"""

import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

W, H = 96, 88
SS = 4
BG = np.array([10, 14, 24], np.float32)

CREAM = (246, 241, 232)
CREAM_SH = (206, 184, 160)
WHITE = (255, 252, 248)
ORANGE = (239, 138, 54)
ORANGE_D = (188, 92, 28)
BLACK = (42, 36, 31)
PINK = (243, 166, 184)
PINK_D = (206, 112, 134)
NOSE = (214, 108, 128)
PUPIL = (32, 26, 30)
HILITE = (255, 255, 255)
WHISKER = (214, 208, 198)
MOUTH = (54, 44, 40)
BLUSH = (226, 140, 150)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_INC = os.path.join(ROOT, "main", "cyd_cat.inc")


def rgb565(r, g, b):
    return ((int(r) & 0xF8) << 8) | ((int(g) & 0xFC) << 3) | (int(b) >> 3)


def from565(v):
    r = (v >> 11) & 31
    g = (v >> 5) & 63
    b = v & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def mix(rgb, t):
    return tuple(int(round(BG[i] * (1 - t) + rgb[i] * t)) for i in range(3))


class Surf:
    def __init__(self):
        self.a = np.zeros((H * SS, W * SS), np.float32)
        self.pm = np.zeros((H * SS, W * SS, 3), np.float32)

    def _over(self, mask, color):
        m = mask.astype(np.float32)
        if m.shape != self.a.shape:
            raise ValueError(m.shape)
        inv = 1.0 - m
        col = np.array(color, np.float32)
        self.pm *= inv[..., None]
        self.pm += col * m[..., None]
        self.a *= inv
        self.a += m

    def ellipse(self, cx, cy, rx, ry, color):
        if rx <= 0.2 or ry <= 0.2:
            return
        x0 = max(int((cx - rx) * SS) - 1, 0)
        x1 = min(int((cx + rx) * SS) + 2, W * SS)
        y0 = max(int((cy - ry) * SS) - 1, 0)
        y1 = min(int((cy + ry) * SS) + 2, H * SS)
        if x1 <= x0 or y1 <= y0:
            return
        ys, xs = np.mgrid[y0:y1, x0:x1]
        xf = (xs + 0.5) / SS
        yf = (ys + 0.5) / SS
        inside = ((xf - cx) / rx) ** 2 + ((yf - cy) / ry) ** 2 <= 1.0
        mask = np.zeros_like(self.a)
        mask[y0:y1, x0:x1] = inside
        self._over(mask, color)

    def erase_ellipse(self, cx, cy, rx, ry):
        if rx <= 0.2 or ry <= 0.2:
            return
        x0 = max(int((cx - rx) * SS) - 1, 0)
        x1 = min(int((cx + rx) * SS) + 2, W * SS)
        y0 = max(int((cy - ry) * SS) - 1, 0)
        y1 = min(int((cy + ry) * SS) + 2, H * SS)
        ys, xs = np.mgrid[y0:y1, x0:x1]
        xf = (xs + 0.5) / SS
        yf = (ys + 0.5) / SS
        inside = ((xf - cx) / rx) ** 2 + ((yf - cy) / ry) ** 2 <= 1.0
        self.a[y0:y1, x0:x1] *= ~inside
        self.pm[y0:y1, x0:x1] *= (~inside)[..., None]

    def polygon(self, pts, color):
        pts = np.asarray(pts, np.float64)
        if len(pts) < 3:
            return
        minx = max(int(pts[:, 0].min() * SS) - 1, 0)
        maxx = min(int(pts[:, 0].max() * SS) + 2, W * SS)
        miny = max(int(pts[:, 1].min() * SS) - 1, 0)
        maxy = min(int(pts[:, 1].max() * SS) + 2, H * SS)
        if maxx <= minx or maxy <= miny:
            return
        ys, xs = np.mgrid[miny:maxy, minx:maxx]
        xf = (xs + 0.5) / SS
        yf = (ys + 0.5) / SS
        inside = np.zeros(xf.shape, bool)
        n = len(pts)
        for i in range(n):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % n]
            cond = ((y0 <= yf) & (y1 > yf)) | ((y1 <= yf) & (y0 > yf))
            denom = (y1 - y0) if abs(y1 - y0) > 1e-9 else 1e-9
            xinter = x0 + (yf - y0) * (x1 - x0) / denom
            inside ^= cond & (xf < xinter)
        mask = np.zeros_like(self.a)
        mask[miny:maxy, minx:maxx] = inside
        self._over(mask, color)

    def discs_along(self, pts, rad, color):
        if rad <= 0:
            return
        # Step about half a pixel so the stroke is solid.
        last = None
        for p in pts:
            if last is not None:
                dx = p[0] - last[0]
                dy = p[1] - last[1]
                dist = (dx * dx + dy * dy) ** 0.5
                steps = max(int(dist / 0.45), 1)
                for s in range(steps + 1):
                    t = s / steps
                    self.ellipse(last[0] + dx * t, last[1] + dy * t, rad, rad, color)
            last = p

    def downsample(self):
        a = self.a.reshape(H, SS, W, SS).mean(axis=(1, 3))
        pm = self.pm.reshape(H, SS, W, SS, 3).mean(axis=(1, 3))
        rgba = np.zeros((H, W, 4), np.uint8)
        good = a > 0.004
        straight = np.zeros_like(pm)
        straight[good] = pm[good] / a[good, None]
        rgba[..., 3] = np.clip(a * 255, 0, 255).astype(np.uint8)
        rgba[..., :3] = np.clip(straight, 0, 255).astype(np.uint8)
        return rgba, a, straight


def bezier(p0, p1, p2, p3, n=28):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        x = u**3 * p0[0] + 3 * u**2 * t * p1[0] + 3 * u * t**2 * p2[0] + t**3 * p3[0]
        y = u**3 * p0[1] + 3 * u**2 * t * p1[1] + 3 * u * t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    return pts


def quad(p0, p1, p2, n=16):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        x = u**2 * p0[0] + 2 * u * t * p1[0] + t**2 * p2[0]
        y = u**2 * p0[1] + 2 * u * t * p1[1] + t**2 * p2[1]
        pts.append((x, y))
    return pts


def build_palette():
    """Index 0 is transparent. Solids first so interiors snap to them."""
    specs = [
        (CREAM, (1.0, 0.58, 0.30)),
        (CREAM_SH, (1.0, 0.55)),
        (WHITE, (1.0, 0.5)),
        (ORANGE, (1.0, 0.62, 0.34)),
        (ORANGE_D, (1.0, 0.55)),
        (BLACK, (1.0, 0.62, 0.34)),
        (PINK, (1.0, 0.55, 0.32)),
        (PINK_D, (1.0,)),
        (NOSE, (1.0,)),
        (PUPIL, (1.0, 0.7)),
        (HILITE, (1.0,)),
        (WHISKER, (1.0, 0.45)),
        (MOUTH, (1.0, 0.6)),
        (BLUSH, (1.0, 0.45)),
    ]
    pal = [0]
    rgb = [(0, 0, 0)]
    seen = {0}
    # Exact cream must exist (host tests count this 565 value).
    cream_v = rgb565(*CREAM)
    pal.append(cream_v)
    rgb.append(from565(cream_v))
    seen.add(cream_v)
    for base, levels in specs:
        for t in levels:
            c = base if t >= 0.999 else mix(base, t)
            v = rgb565(*c)
            if v in seen:
                continue
            seen.add(v)
            pal.append(v)
            rgb.append(from565(v))
    return pal, rgb


PAL, PAL_RGB = build_palette()
CREAM_I = PAL.index(rgb565(*CREAM))


def nearest(color):
    r, g, b = (int(color[0]), int(color[1]), int(color[2]))
    best, bd = 1, 1 << 30
    for i in range(1, len(PAL_RGB)):
        pr, pg, pb = PAL_RGB[i]
        d = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb)
        if d < bd:
            bd = d
            best = i
    return best


def quantize(rgba, alpha):
    idx = np.zeros((H, W), np.uint8)
    for y in range(H):
        for x in range(W):
            a = float(alpha[y, x])
            if a < 0.14:
                continue
            if a > 0.80:
                idx[y, x] = nearest(rgba[y, x, :3])
            else:
                straight = rgba[y, x, :3].astype(np.float32)
                blended = straight * a + BG * (1 - a)
                idx[y, x] = nearest(blended)
    return idx


def rle_rows(idx, x0, y0, x1, y1):
    """Crop [x0,x1) [y0,y1) and RLE each row. Returns (w,h, blob, row_offs)."""
    w = x1 - x0
    h = y1 - y0
    blob = bytearray()
    rows = [0]
    for y in range(y0, y1):
        x = x0
        while x < x1:
            v = int(idx[y, x])
            x2 = x + 1
            while x2 < x1 and int(idx[y, x2]) == v and x2 - x < 127:
                x2 += 1
            n = x2 - x
            if v == 0:
                blob.append(n)
            else:
                blob.append(128 + n)
                blob.extend([v] * n)
            x = x2
        rows.append(len(blob))
    return w, h, blob, rows


def bbox(idx):
    ys, xs = np.nonzero(idx)
    if len(xs) == 0:
        return None
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def draw_tail(sway):
    s = Surf()
    base = (40, 66)
    c1 = (16 + sway, 64)
    c2 = (10 + sway * 0.85, 46)
    end = (22 + sway * 0.45, 28 - sway * 0.15)
    curve = bezier(base, c1, c2, end)
    s.discs_along(curve, 6.2, CREAM)
    s.discs_along(curve[4:], 3.3, ORANGE)
    s.discs_along(curve[18:], 3.4, BLACK)
    # Orange band should not eat the black tip.
    s.discs_along(curve[22:], 3.6, BLACK)
    return s


def draw_ears(style):
    """style: sit, left, right, alert."""
    s = Surf()
    l_tip = [30, 7]
    r_tip = [82, 7]
    if style == "left":
        l_tip = [28, 3]
    elif style == "right":
        r_tip = [84, 3]
    elif style == "alert":
        l_tip = [34, 2]
        r_tip = [78, 2]
    s.polygon([(38, 30), tuple(l_tip), (52, 24)], BLACK)
    s.polygon([(42, 28), (l_tip[0] + 4, l_tip[1] + 8), (50, 24)], PINK)
    s.polygon([(74, 30), tuple(r_tip), (60, 24)], ORANGE)
    s.polygon([(70, 28), (r_tip[0] - 4, r_tip[1] + 8), (62, 24)], PINK_D if style == "alert" else PINK)
    # Inner-ear shade.
    s.polygon([(44, 26), (l_tip[0] + 6, l_tip[1] + 11), (48, 25)], PINK_D)
    return s


def draw_body():
    s = Surf()
    # Volume shade, then the cream mass (head + chest + belly as one silhouette).
    s.ellipse(58, 66, 26, 18, CREAM_SH)
    s.ellipse(56, 64, 24.5, 16.5, CREAM)
    s.ellipse(56, 54, 15, 11, CREAM)
    s.ellipse(57, 40, 18, 16, CREAM_SH)
    s.ellipse(56, 39, 17.5, 15.5, CREAM)
    # Calico patches. Cap and cheek sit on the head; hips on the body.
    s.ellipse(44, 30, 13, 8.5, BLACK)
    s.ellipse(70, 46, 9.5, 7.2, ORANGE)
    s.ellipse(42, 64, 11, 8, ORANGE)
    s.ellipse(44, 66, 7, 5, ORANGE_D)
    s.ellipse(72, 58, 10, 8.5, BLACK)
    # Muzzle lightens the centre of the face without erasing the cheek.
    s.ellipse(56, 46, 10, 7.2, WHITE)
    s.ellipse(56, 47, 7, 5, CREAM)
    # Paws, with a shade line so they separate from the chest.
    s.ellipse(45, 77, 8.2, 5.2, CREAM_SH)
    s.ellipse(67, 77, 8.2, 5.2, CREAM_SH)
    s.ellipse(45, 76.2, 7.6, 4.6, CREAM)
    s.ellipse(67, 76.2, 7.6, 4.6, CREAM)
    s.ellipse(42, 77.2, 1.5, 1.1, PINK)
    s.ellipse(46.5, 77.6, 1.5, 1.1, PINK)
    s.ellipse(64, 77.6, 1.5, 1.1, PINK)
    s.ellipse(68.5, 77.2, 1.5, 1.1, PINK)
    # Nose.
    s.polygon([(56, 43.2), (52.6, 47.2), (59.4, 47.2)], NOSE)
    s.ellipse(55.2, 44.6, 1.1, 0.7, HILITE)
    # Whiskers.
    s.discs_along([(45, 46), (24, 41)], 0.55, WHISKER)
    s.discs_along([(45, 48.5), (22, 49)], 0.55, WHISKER)
    s.discs_along([(45, 51), (25, 56)], 0.5, WHISKER)
    s.discs_along([(67, 46), (88, 41)], 0.55, WHISKER)
    s.discs_along([(67, 48.5), (90, 49)], 0.55, WHISKER)
    s.discs_along([(67, 51), (87, 56)], 0.5, WHISKER)
    return s


def draw_eyes(mode, pdx=0, pdy=0):
    s = Surf()
    centers = (47.5, 65.5)
    ey = 39.5
    for ex in centers:
        if mode == "happy":
            s.discs_along(quad((ex - 4.2, ey + 1.2), (ex, ey - 3.4), (ex + 4.2, ey + 1.2)), 0.95, PUPIL)
            s.discs_along(quad((ex - 3.2, ey + 0.6), (ex, ey - 2.2), (ex + 3.2, ey + 0.6)), 0.45, HILITE)
            continue
        if mode == "shut":
            s.discs_along(quad((ex - 4.4, ey - 0.4), (ex, ey + 2.2), (ex + 4.4, ey - 0.4)), 0.85, PUPIL)
            continue
        if mode == "half":
            s.ellipse(ex, ey + 0.8, 4.3, 2.3, PUPIL)
            s.ellipse(ex - 1.3, ey, 1.05, 0.9, HILITE)
            continue
        alert = mode == "alert"
        rx = 5.0 if alert else 4.35
        ry = 6.0 if alert else 5.15
        s.ellipse(ex, ey, rx, ry, WHITE)
        s.ellipse(ex + pdx, ey + pdy + ( -0.6 if alert else 0.3), rx - 1.35, ry - 1.25, PUPIL)
        s.ellipse(ex + pdx - 1.35, ey + pdy - (2.2 if alert else 1.7), 1.25, 1.45, HILITE)
        # Upper lid: punch back to transparent so the cream face shows through.
        lid = 0.55 if alert else 1.15
        s.erase_ellipse(ex, ey - ry + 0.2, rx + 0.3, lid)
    return s


def draw_mouth(kind):
    s = Surf()
    if kind == "smile":
        s.discs_along(quad((50.5, 49.5), (56, 54.2), (61.5, 49.5)), 0.7, MOUTH)
        s.discs_along([(56, 47.4), (56, 49.6)], 0.45, MOUTH)
    elif kind == "purr":
        s.discs_along(quad((49, 49.2), (56, 56.4), (63, 49.2)), 0.85, MOUTH)
        s.discs_along([(56, 47.4), (56, 49.8)], 0.45, MOUTH)
        s.ellipse(42, 46, 3.2, 2.0, BLUSH)
        s.ellipse(70, 46, 3.2, 2.0, BLUSH)
    elif kind == "yawn":
        s.ellipse(56, 52, 4.6, 3.6, MOUTH)
        s.ellipse(56, 53.4, 2.5, 1.8, PINK)
        s.ellipse(55.2, 51.2, 1.2, 0.7, HILITE)
    elif kind == "yawn_small":
        s.ellipse(56, 51, 3.2, 2.4, MOUTH)
        s.ellipse(56, 51.8, 1.6, 1.1, PINK)
    return s


def draw_stretch(t):
    """t = 0 sit-like, 1 full stretch with a yawn."""
    s = Surf()
    head_y = 40 + 9 * t
    body_y = 63 + 5 * t
    body_rx = 23 + 6 * t
    paw_y = 76 + 2 * t
    reach = 16 * t
    # Tail drops and straightens.
    sway = -4 * (1 - t)
    base = (40, body_y + 2)
    end = (20 + 8 * t, 30 + 16 * t)
    curve = bezier(base, (18 + sway, body_y), (14, 50 + 8 * t), end)
    s.discs_along(curve, 5.6, CREAM)
    s.discs_along(curve[6:], 2.8, ORANGE)
    s.discs_along(curve[20:], 3.2, BLACK)

    # Ears follow the head down. Tips stay put longer than the head so the
    # stretch reads as the shoulders dropping.
    ear_drop = 6 * t
    s.polygon([(40, 32 + ear_drop), (32, 10 + ear_drop * 0.3), (52, 26 + ear_drop)], BLACK)
    s.polygon([(43, 30 + ear_drop), (36, 16 + ear_drop * 0.3), (50, 26 + ear_drop)], PINK)
    s.polygon([(72, 32 + ear_drop), (80, 10 + ear_drop * 0.3), (60, 26 + ear_drop)], ORANGE)
    s.polygon([(69, 30 + ear_drop), (76, 16 + ear_drop * 0.3), (62, 26 + ear_drop)], PINK)

    s.ellipse(58, body_y + 3, body_rx + 1.5, 16, CREAM_SH)
    s.ellipse(56, body_y, body_rx, 15, CREAM)
    s.ellipse(56, head_y + 12, 14, 10, CREAM)
    s.ellipse(56, head_y, 17, 14.5, CREAM)
    s.ellipse(44, head_y - 8, 12, 7.5, BLACK)
    s.ellipse(69, head_y + 6, 9, 6.5, ORANGE)
    s.ellipse(40, body_y, 10, 7, ORANGE)
    s.ellipse(72, body_y - 4, 9, 7, BLACK)
    s.ellipse(56, head_y + 7, 9, 6, WHITE)

    # Front paws slide outward along the floor.
    s.ellipse(44 - reach, paw_y, 8 + reach * 0.35, 4.6, CREAM)
    s.ellipse(68 + reach, paw_y, 8 + reach * 0.35, 4.6, CREAM)
    s.ellipse(40 - reach, paw_y + 0.6, 1.4, 1.0, PINK)
    s.ellipse(72 + reach, paw_y + 0.6, 1.4, 1.0, PINK)

    s.polygon([(56, head_y + 4), (53, head_y + 8), (59, head_y + 8)], NOSE)
    # Whiskers stay, a little lower with the muzzle.
    my = head_y + 8
    s.discs_along([(46, my), (26, my - 4)], 0.5, WHISKER)
    s.discs_along([(46, my + 3), (24, my + 3)], 0.5, WHISKER)
    s.discs_along([(66, my), (86, my - 4)], 0.5, WHISKER)
    s.discs_along([(66, my + 3), (88, my + 3)], 0.5, WHISKER)

    if t < 0.35:
        mode, pdy = "open", 0.4
    elif t < 0.72:
        mode, pdy = "half", 0
    else:
        mode, pdy = "shut", 0
    # Draw eyes into this surf by compositing a second pass.
    eyes = draw_eyes(mode, 0, pdy)
    # Shift eyes down with the head (draw_eyes uses a fixed ey).
    return s, eyes, head_y - 39.5, "yawn" if t >= 0.72 else ("yawn_small" if t >= 0.4 else "smile")


def draw_sleep(z_i):
    s = Surf()
    # Loaf: a low oval, tail wrapped across the front, head on the paws.
    s.ellipse(54, 62, 30, 16, CREAM_SH)
    s.ellipse(52, 60, 28, 14.5, CREAM)
    s.ellipse(40, 52, 16, 13, CREAM)
    s.ellipse(36, 48, 8, 6, BLACK)          # cap, still the left ear's colour
    s.ellipse(50, 56, 8, 6, ORANGE)         # cheek
    s.ellipse(66, 58, 10, 7, BLACK)         # hip
    s.ellipse(30, 62, 8, 6, ORANGE)
    # Wrapped tail.
    curve = bezier((62, 70), (78, 74), (84, 60), (70, 50))
    s.discs_along(curve, 5.0, CREAM)
    s.discs_along(curve[8:], 2.6, ORANGE)
    s.discs_along(curve[18:], 3.0, BLACK)
    # Relaxed ears, lower than the sitting pose.
    s.polygon([(30, 46), (24, 30), (40, 42)], BLACK)
    s.polygon([(33, 44), (28, 34), (38, 42)], PINK)
    s.polygon([(48, 44), (56, 28), (42, 40)], ORANGE)
    s.polygon([(47, 43), (52, 33), (43, 40)], PINK)
    # Closed eyes and a tiny nose.
    s.discs_along(quad((30, 52), (34, 54.5), (38, 52)), 0.8, PUPIL)
    s.discs_along(quad((42, 53), (46, 55.5), (50, 53)), 0.8, PUPIL)
    s.polygon([(36, 56), (33.5, 59), (38.5, 59)], NOSE)
    # Z's rise toward the empty top-right. Three steps, eased by the caller.
    draw_zs(s, z_i)
    return s


def draw_zs(s, step):
    """Pixel Z's. step 0, 1, 2 lifts them."""
    rise = step * 5

    def glyph(x, y, scale, color):
        # 3x5 Z
        rows = ["###", "..#", ".#.", "#..", "###"]
        for gy, row in enumerate(rows):
            for gx, ch in enumerate(row):
                if ch == "#":
                    s.ellipse(x + gx * scale, y + gy * scale - rise, scale * 0.48, scale * 0.48, color)

    glyph(68, 30, 2.1, (126, 198, 228))
    glyph(78, 18, 2.8, (186, 224, 242))


def to_idx(surf):
    rgba, alpha, _straight = surf.downsample()
    return quantize(rgba, alpha), rgba


def composite_debug(layers):
    """layers: list of RGBA full canvases, painter's order."""
    out = np.zeros((H, W, 4), np.uint8)
    out[..., :3] = BG.astype(np.uint8)
    out[..., 3] = 255
    for rgba in layers:
        a = rgba[..., 3:4].astype(np.float32) / 255.0
        blended = rgba[..., :3].astype(np.float32) * a + out[..., :3].astype(np.float32) * (1 - a)
        out[..., :3] = np.clip(blended, 0, 255).astype(np.uint8)
    return out[..., :3].copy()


def shift_rgba(rgba, dy):
    out = np.zeros_like(rgba)
    if dy == 0:
        return rgba
    if dy > 0:
        out[dy:] = rgba[:-dy]
    else:
        out[:dy] = rgba[-dy:]
    return out


def main():
    os.makedirs("/tmp/cat-art", exist_ok=True)
    parts = []  # (name, idx, full rgba for debug)

    def add(name, surf_or_idx):
        if isinstance(surf_or_idx, Surf):
            idx, rgba = to_idx(surf_or_idx)
        else:
            idx = surf_or_idx
            rgba = None
        parts.append((name, idx))
        return idx

    tails = [add("tail%d" % i, draw_tail(sway)) for i, sway in enumerate((-7, -3.5, 0, 3.5, 7))]
    ears = {k: add("ears_%s" % k, draw_ears(k)) for k in ("sit", "left", "right", "alert")}
    body = add("body", draw_body())
    eyes = {}
    for name, mode, pdx, pdy in (
        ("eyes_c", "open", 0, 0),
        ("eyes_l", "open", -1.5, 0),
        ("eyes_r", "open", 1.5, 0),
        ("eyes_up", "alert", 0, -1.6),
        ("eyes_half", "half", 0, 0),
        ("eyes_shut", "shut", 0, 0),
        ("eyes_happy", "happy", 0, 0),
    ):
        eyes[name] = add(name, draw_eyes(mode, pdx, pdy))
    mouths = {}
    for name in ("smile", "purr", "yawn", "yawn_small"):
        mouths[name] = add("mouth_%s" % name, draw_mouth(name))

    stretch = []
    for i, t in enumerate((0.18, 0.42, 0.66, 0.84, 1.0)):
        body_s, eye_s, shift, mouth = draw_stretch(t)
        # Bake eyes and mouth into the stretch frame, shifted with the head.
        b_rgba, b_a, _ = body_s.downsample()
        e_rgba, e_a, _ = eye_s.downsample()
        m_rgba, m_a, _ = draw_mouth(mouth).downsample()
        e_rgba = shift_rgba(e_rgba, int(round(shift)))
        e_a = shift_rgba(np.dstack([e_a, e_a, e_a, e_a]), int(round(shift)))[..., 0]
        # Re-quantize the flattened image.
        flat_a = np.clip(b_a + (1 - b_a) * 0, 0, 1)  # placeholder
        # Manual over of rgba.
        def over(base_rgba, base_a, top_rgba):
            ta = top_rgba[..., 3:4].astype(np.float32) / 255.0
            # downsample stored straight color and alpha in the 4th channel
            out = base_rgba.copy()
            ba = base_a[..., None]
            # Reconstruct premul-ish: base straight * base_a
            top_a = ta
            # Where top is opaque, take top straight; else blend.
            bs = base_rgba[..., :3].astype(np.float32)
            ts = top_rgba[..., :3].astype(np.float32)
            a = top_a + ba * (1 - top_a)
            straight = np.zeros_like(bs)
            good = a > 0.004
            straight[good[..., 0]] = (
                (ts * top_a + bs * ba * (1 - top_a))[good[..., 0]] / a[good[..., 0]]
            )
            out_rgba = np.zeros_like(base_rgba)
            out_rgba[..., :3] = np.clip(straight, 0, 255).astype(np.uint8)
            out_rgba[..., 3] = np.clip(a[..., 0] * 255, 0, 255).astype(np.uint8)
            return out_rgba, a[..., 0]

        # eye/mouth alpha from their own downsample, not the shifted alpha channel
        # Rebuild tops as proper RGBA from downsample().
        acc_rgba = b_rgba.copy()
        acc_rgba[..., 3] = np.clip(b_a * 255, 0, 255).astype(np.uint8)
        acc_a = b_a
        for top_s, dy in ((eye_s, int(round(shift))), (draw_mouth(mouth), int(round(shift)))):
            tr, ta, _ = top_s.downsample()
            tr = tr.copy()
            tr[..., 3] = np.clip(ta * 255, 0, 255).astype(np.uint8)
            tr = shift_rgba(tr, dy)
            acc_rgba, acc_a = over(acc_rgba, acc_a, tr)
        idx = quantize(acc_rgba, acc_a)
        stretch.append(add("stretch%d" % i, idx))
        Image.fromarray(flatten_preview(acc_rgba, acc_a), "RGB").resize((W * 4, H * 4), Image.NEAREST).save(
            "/tmp/cat-art/stretch%d.png" % i
        )

    sleep = []
    for i in range(3):
        idx, rgba = to_idx(draw_sleep(i))
        sleep.append(add("sleep%d" % i, idx))

    # Debug composites of the sitting loops (tail mid, eyes open).
    def layer_rgba(idx):
        img = np.zeros((H, W, 4), np.uint8)
        for y in range(H):
            for x in range(W):
                v = int(idx[y, x])
                if v:
                    r, g, b = PAL_RGB[v]
                    img[y, x] = (r, g, b, 255)
        return img

    def save_pose(name, names_or_idx):
        layers = []
        for item in names_or_idx:
            if isinstance(item, str):
                idx = dict(parts)[item]
            else:
                idx = item
            layers.append(layer_rgba(idx))
        rgb = composite_debug(layers)
        Image.fromarray(rgb, "RGB").resize((W * 4, H * 4), Image.NEAREST).save("/tmp/cat-art/%s.png" % name)

    save_pose("idle", ["tail2", "ears_sit", "body", "eyes_c", "mouth_smile"])
    save_pose("blink", ["tail2", "ears_sit", "body", "eyes_shut", "mouth_smile"])
    save_pose("half", ["tail2", "ears_sit", "body", "eyes_half", "mouth_smile"])
    save_pose("look_l", ["tail0", "ears_left", "body", "eyes_l", "mouth_smile"])
    save_pose("look_r", ["tail4", "ears_right", "body", "eyes_r", "mouth_smile"])
    save_pose("alert", ["tail3", "ears_alert", "body", "eyes_up", "mouth_smile"])
    save_pose("purr", ["tail2", "ears_sit", "body", "eyes_happy", "mouth_purr"])
    save_pose("sleep", ["sleep1"])
    save_pose("stretch", ["stretch4"])

    # Emit the include.
    blobs = bytearray()
    row_table = []
    part_meta = []
    for name, idx in parts:
        box = bbox(idx)
        if box is None:
            part_meta.append((name, 1, 1, 0, 0, 0, 0))
            row_table.extend([len(blobs), len(blobs)])
            continue
        x0, y0, x1, y1 = box
        w, h, blob, rows = rle_rows(idx, x0, y0, x1, y1)
        rle_off = len(blobs)
        blobs.extend(blob)
        row_off = len(row_table)
        # Store absolute offsets into the blob.
        for rel in rows:
            row_table.append(rle_off + rel)
        part_meta.append((name, w, h, x0, y0, rle_off, row_off))

    names = [p[0] for p in part_meta]
    lines = []
    lines.append("/* Generated by scripts/gen_cyd_cat.py. Do not edit. */")
    lines.append("/* Palette index 0 is transparent. Edge colours are already")
    lines.append(" * blended with the panel background (10, 14, 24). */")
    lines.append("#define CAT_W %d" % W)
    lines.append("#define CAT_H %d" % H)
    lines.append("#define CAT_PAL_N %d" % len(PAL))
    lines.append("#define CAT_PART_N %d" % len(part_meta))
    lines.append("static const uint16_t CAT_PAL[CAT_PAL_N] = {")
    lines.append("    " + ", ".join(str(v) for v in PAL))
    lines.append("};")
    for i, name in enumerate(names):
        lines.append("#define CAT_%s %d" % (name.upper(), i))
    lines.append("typedef struct {")
    lines.append("    uint8_t w, h;")
    lines.append("    uint8_t x, y;")
    lines.append("    uint16_t row; /* index into CAT_ROW; h+1 absolute RLE offsets */")
    lines.append("} cat_part_t;")
    lines.append("static const uint8_t CAT_RLE[] = {")
    for i in range(0, len(blobs), 20):
        chunk = blobs[i:i + 20]
        lines.append("    " + ", ".join(str(b) for b in chunk) + ",")
    lines.append("};")
    lines.append("static const uint16_t CAT_ROW[] = {")
    for i in range(0, len(row_table), 16):
        chunk = row_table[i:i + 16]
        lines.append("    " + ", ".join(str(v) for v in chunk) + ",")
    lines.append("};")
    lines.append("static const cat_part_t CAT_PART[CAT_PART_N] = {")
    for name, w, h, x, y, _rle, row in part_meta:
        lines.append("    {%d, %d, %d, %d, %d}, /* %s */" % (w, h, x, y, row, name))
    lines.append("};")
    text = "\n".join(lines) + "\n"
    with open(OUT_INC, "w", encoding="utf-8") as f:
        f.write(text)
    print("parts", len(part_meta), "rle", len(blobs), "rows", len(row_table), "pal", len(PAL))
    print("cream index", CREAM_I, "file", OUT_INC, "bytes", os.path.getsize(OUT_INC))
    for name, w, h, x, y, _rle, _row in part_meta:
        print(f"  {name:16} {w:3}x{h:<3} at {x:3},{y:<3}")


def flatten_preview(rgba, alpha):
    a = alpha[..., None]
    rgb = rgba[..., :3].astype(np.float32) * a + BG * (1 - a)
    return np.clip(rgb, 0, 255).astype(np.uint8)


if __name__ == "__main__":
    main()
