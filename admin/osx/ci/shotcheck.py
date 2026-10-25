#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""Screenshot checks for the NoDrive macOS smoke test (tools/screenshots/macos.sh). Pillow only.

macOS screenshots carry the display's colour profile. Every command first converts the image
to sRGB with that embedded profile, so colours can be compared with the NoDrive palette.
Coordinates are image pixels (the caller converts points to pixels).

  shotcheck.py info IMG            "<w> <h> <distinct colours> <blank 0|1>"
  shotcheck.py bg IMG              most common colour: "#RRGGBB <luminance 0-255> <share %>"
  shotcheck.py primary IMG [HEX] [TOL]
                                   largest button-shaped area in colour HEX (default #2563EB,
                                   tolerance TOL per channel, default 24):
                                   "<cx> <cy> <pixels> <bx> <by> <bw> <bh> <measured #RRGGBB>", exit 1 if none
  shotcheck.py footer IMG BAND     centre of the content in the bottom BAND pixels: "<cx> <cy>"
  shotcheck.py crop IMG X Y W H OUT
"""

from __future__ import annotations

import io
import sys
from collections import Counter, deque

from PIL import Image

try:
    from PIL import ImageCms
except ImportError:  # Pillow without littlecms: compare raw values
    ImageCms = None


def load(path: str) -> Image.Image:
    img = Image.open(path)
    icc = img.info.get("icc_profile")
    rgb = img.convert("RGB")
    if icc and ImageCms is not None:
        try:
            src = ImageCms.ImageCmsProfile(io.BytesIO(icc))
            dst = ImageCms.createProfile("sRGB")
            rgb = ImageCms.profileToProfile(rgb, src, dst, outputMode="RGB")
        except Exception as exc:  # keep the raw values, but say so
            print(f"shotcheck: colour conversion skipped: {exc}", file=sys.stderr)
    return rgb


def pixels(img: Image.Image) -> list:
    """Pixel values in row order (get_flattened_data replaces the deprecated getdata)."""
    get = getattr(img, "get_flattened_data", None) or img.getdata
    return list(get())


def parse_hex(h: str) -> tuple[int, int, int]:
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def luminance(c) -> int:
    r, g, b = c
    return round(0.2126 * r + 0.7152 * g + 0.0722 * b)


def components(mask: bytearray, w: int, h: int):
    seen = bytearray(w * h)
    for start in range(w * h):
        if not mask[start] or seen[start]:
            continue
        q = deque([start])
        seen[start] = 1
        n = 0
        minx = miny = 1 << 30
        maxx = maxy = -1
        while q:
            p = q.popleft()
            n += 1
            px, py = p % w, p // w
            minx, maxx = min(minx, px), max(maxx, px)
            miny, maxy = min(miny, py), max(maxy, py)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = px + dx, py + dy
                if 0 <= nx < w and 0 <= ny < h:
                    q2 = ny * w + nx
                    if mask[q2] and not seen[q2]:
                        seen[q2] = 1
                        q.append(q2)
        yield n, minx, miny, maxx - minx + 1, maxy - miny + 1


def cmd_info(a):
    img = load(a[0])
    colours = img.getcolors(maxcolors=1 << 20) or []
    distinct = len(colours)
    top = max(colours)[0] / (img.width * img.height) if colours else 1.0
    blank = 1 if distinct < 8 or top > 0.995 else 0
    print(img.width, img.height, distinct, blank)
    return 0


def cmd_bg(a):
    img = load(a[0])
    counts = Counter(pixels(img))
    colour, n = counts.most_common(1)[0]
    print("#%02X%02X%02X" % colour, luminance(colour), round(100 * n / (img.width * img.height), 1))
    return 0


def cmd_primary(a):
    img = load(a[0])
    want = parse_hex(a[1] if len(a) > 1 else "#2563EB")
    tol = int(a[2]) if len(a) > 2 else 24
    data = pixels(img)
    mask = bytearray(1 if all(abs(p[i] - want[i]) <= tol for i in range(3)) else 0 for p in data)
    best = None
    for comp in components(mask, img.width, img.height):
        n, bx, by, bw, bh = comp
        if bw < 40 or bh < 16 or bw < bh:  # a button: wider than tall
            continue
        if best is None or n > best[0]:
            best = comp
    if not best:
        return 1
    n, bx, by, bw, bh = best
    inside = Counter(data[y * img.width + x] for y in range(by, by + bh) for x in range(bx, bx + bw)
                     if mask[y * img.width + x])
    measured = inside.most_common(1)[0][0]
    print(bx + bw // 2, by + bh // 2, n, bx, by, bw, bh, "#%02X%02X%02X" % measured)
    return 0


def cmd_footer(a):
    img = load(a[0])
    band = int(a[1])
    w, h = img.width, img.height
    im = img.crop((0, max(0, h - band), w, h))
    data = pixels(im)
    bg = Counter(data).most_common(1)[0][0]
    mask = bytearray(1 if sum(abs(x - y) for x, y in zip(p, bg)) > 60 else 0 for p in data)
    boxes = [c for c in components(mask, im.width, im.height) if c[3] < 0.8 * im.width and c[0] >= 3]
    if not boxes:
        return 1
    minx = min(b[1] for b in boxes)
    miny = min(b[2] for b in boxes)
    maxx = max(b[1] + b[3] for b in boxes)
    maxy = max(b[2] + b[4] for b in boxes)
    print((minx + maxx) // 2, max(0, h - band) + (miny + maxy) // 2)
    return 0


def cmd_crop(a):
    img = Image.open(a[0])
    x, y, w, h = map(int, a[1:5])
    img.crop((max(0, x), max(0, y), min(img.width, x + w), min(img.height, y + h))).save(a[5])
    return 0


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    cmds = {"info": cmd_info, "bg": cmd_bg, "primary": cmd_primary, "footer": cmd_footer, "crop": cmd_crop}
    return cmds[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())
