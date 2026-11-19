#!/usr/bin/env python3
"""A 256x256 PNG icon (a dial on a dark square), standard library only: python3 make-icon.py out.png"""
import math, struct, sys, zlib

N = 256
rows = []
for y in range(N):
    row = bytearray([0])
    for x in range(N):
        dx, dy = x - N / 2 + 0.5, y - N / 2 + 0.5
        r = math.hypot(dx, dy)
        ang = (math.degrees(math.atan2(dy, dx)) + 360 + 90) % 360  # 0 at top, clockwise
        px = (24, 26, 32, 255)
        if r < 112:
            px = (40, 44, 54, 255)
        if 84 <= r <= 100 and 40 <= ang <= 320 and (ang - 40) <= 190:
            px = (96, 200, 255, 255)  # the lit arc
        elif 84 <= r <= 100 and 40 <= ang <= 320:
            px = (70, 76, 90, 255)
        if r < 56 and abs(dx * math.cos(math.radians(30)) + dy * math.sin(math.radians(30)) + 0) < 5 and (dy < 0 or r < 6):
            px = (240, 244, 250, 255)
        row += bytes(px)
    rows.append(bytes(row))
raw = b"".join(rows)

def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
