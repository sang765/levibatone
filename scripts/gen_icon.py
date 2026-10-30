#!/usr/bin/env python3
"""Generate assets/levibatone.png: 128x128 icon, dark slate background with a
green block path stepping up to a gold goal block."""
import struct
import sys
import zlib
from pathlib import Path

W = H = 128

# Palette: (r, g, b)
BG = (24, 26, 32)
BG2 = (30, 33, 40)
GREEN = (85, 255, 136)
GREEN_DARK = (34, 102, 51)
GOLD = (250, 213, 82)


def put(px, x, y, color):
    if 0 <= x < W and 0 <= y < H:
        px[y * W + x] = color


def rect(px, x0, y0, x1, y1, color):
    for y in range(y0, y1):
        for x in range(x0, x1):
            put(px, x, y, color)


def block(px, bx, by, top, side):
    s = 18
    x0, y0 = bx * s, by * s
    rect(px, x0, y0, x0 + s, y0 + s, side)
    rect(px, x0, y0, x0 + s, y0 + 4, top)


def main() -> int:
    pixels = [BG] * (W * H)
    # subtle checker backdrop
    for y in range(H):
        for x in range(W):
            if (x // 16 + y // 16) % 2 == 0:
                pixels[y * W + x] = BG2

    # stair path: bottom-left to top-right (3D-ish isometric steps)
    steps = [(1, 4), (2, 3), (3, 2), (4, 1)]
    for i, (bx, by) in enumerate(steps):
        block(pixels, bx, by, GREEN, GREEN_DARK)
        # path line connecting block tops
    # goal block in gold at the top right
    block(pixels, 5, 0, GOLD, (160, 130, 40))

    # connect with a bright line
    for i in range(len(steps) - 1):
        (x0, y0), (x1, y1) = steps[i], steps[i + 1]
        cx0, cy0 = x0 * 18 + 9, y0 * 18 + 2
        cx1, cy1 = x1 * 18 + 9, y1 * 18 + 2
        n = max(abs(cx1 - cx0), abs(cy1 - cy0))
        for t in range(n + 1):
            x = cx0 + (cx1 - cx0) * t // max(n, 1)
            y = cy0 + (cy1 - cy0) * t // max(n, 1)
            put(pixels, x, y, GREEN)
            put(pixels, x, y + 1, GREEN)

    raw = b"".join(
        b"\x00" + b"".join(struct.pack("BBB", *pixels[y * W + x]) for x in range(W))
        for y in range(H)
    )

    def chunk(tag: bytes, data: bytes) -> bytes:
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/levibatone.png")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(png)
    print(f"wrote {out} ({len(png)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
