#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""Find click targets in a screenshot for the Linux smoke test (Pillow only).

The wizard draws its buttons with QML, so there are no widget handles to click.
This helper looks at the pixels instead, which works in every language and in
right-to-left layouts:

  locate.py primary IMG X Y W H [HEX]
      centre of the largest area in the colour HEX (default #2563EB, the
      NoDrive primary button) inside the window rectangle X,Y,W,H.
      Prints "cx cy area bx by bw bh" (screen coordinates) or exits 1.
  locate.py footer IMG X Y W H [BAND]
      centre of the content (text) in the bottom BAND pixels (default 90) of
      the window: the wizard footer holds only the "Proxy settings" button on
      the first page. Prints "cx cy" or exits 1.
  locate.py count IMG HEX [X Y W H]
      number of pixels in colour HEX (tolerance 6 per channel).
  locate.py pixel IMG X Y
      colour at X,Y as "#RRGGBB luminance" (0-255).
"""

import sys
from collections import deque

from PIL import Image

TOL = 6


def parse_hex(h: str):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def crop(img, x, y, w, h):
    W, H = img.size
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(W, x + w), min(H, y + h)
    return img.crop((x0, y0, x1, y1)).convert("RGB"), x0, y0


def components(mask, w, h):
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
                    np_ = ny * w + nx
                    if mask[np_] and not seen[np_]:
                        seen[np_] = 1
                        q.append(np_)
        yield n, minx, miny, maxx - minx + 1, maxy - miny + 1


def colour_mask(im, rgb):
    r0, g0, b0 = rgb
    return bytearray(
        1 if abs(r - r0) <= TOL and abs(g - g0) <= TOL and abs(b - b0) <= TOL else 0
        for (r, g, b) in im.getdata()
    )


def cmd_primary(args):
    img = Image.open(args[0])
    x, y, w, h = map(int, args[1:5])
    rgb = parse_hex(args[5] if len(args) > 5 else "#2563EB")
    im, ox, oy = crop(img, x, y, w, h)
    cw, ch = im.size
    best = None
    for comp in components(colour_mask(im, rgb), cw, ch):
        n, bx, by, bw, bh = comp
        # a button: wider than tall, at least 40x20 pixels
        if bw < 40 or bh < 20 or bw < bh:
            continue
        if best is None or n > best[0]:
            best = comp
    if not best:
        return 1
    n, bx, by, bw, bh = best
    print(ox + bx + bw // 2, oy + by + bh // 2, n, ox + bx, oy + by, bw, bh)
    return 0


def cmd_footer(args):
    img = Image.open(args[0])
    x, y, w, h = map(int, args[1:5])
    band = int(args[5]) if len(args) > 5 else 90
    im, ox, oy = crop(img, x, y + h - band, w, band)
    cw, ch = im.size
    data = list(im.getdata())
    # background = most common colour in the band
    counts = {}
    for p in data:
        counts[p] = counts.get(p, 0) + 1
    bg = max(counts, key=counts.get)
    mask = bytearray(1 if sum(abs(a - b) for a, b in zip(p, bg)) > 60 else 0 for p in data)
    boxes = [c for c in components(mask, cw, ch) if c[3] < 0.8 * cw and c[0] >= 3]
    if not boxes:
        return 1
    minx = min(b[1] for b in boxes)
    miny = min(b[2] for b in boxes)
    maxx = max(b[1] + b[3] for b in boxes)
    maxy = max(b[2] + b[4] for b in boxes)
    print(ox + (minx + maxx) // 2, oy + (miny + maxy) // 2)
    return 0


def cmd_count(args):
    img = Image.open(args[0])
    rgb = parse_hex(args[1])
    if len(args) >= 6:
        im, _, _ = crop(img, *map(int, args[2:6]))
    else:
        im = img.convert("RGB")
    print(sum(colour_mask(im, rgb)))
    return 0


def cmd_pixel(args):
    img = Image.open(args[0]).convert("RGB")
    r, g, b = img.getpixel((int(args[1]), int(args[2])))
    print(f"#{r:02X}{g:02X}{b:02X} {round(0.2126 * r + 0.7152 * g + 0.0722 * b)}")
    return 0


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, args = sys.argv[1], sys.argv[2:]
    cmds = {"primary": cmd_primary, "footer": cmd_footer, "count": cmd_count, "pixel": cmd_pixel}
    return cmds[cmd](args)


if __name__ == "__main__":
    sys.exit(main())
