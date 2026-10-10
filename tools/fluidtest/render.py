#!/usr/bin/env python3
"""Turn fluidtest frames (1024-byte page-layout buffers) into one contact-sheet PNG."""
import sys
from PIL import Image, ImageDraw
src, dst = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 3
scale = 3
data = open(src, 'rb').read()
n = len(data) // 1024
tiles = []
for k in range(n):
    buf = data[k*1024:(k+1)*1024]
    img = Image.new('RGB', (128, 64), (8, 8, 12))
    px = img.load()
    for x in range(128):
        for y in range(64):
            if buf[(y >> 3) * 128 + x] >> (y & 7) & 1:
                px[x, y] = (120, 220, 255)       # OLED-ish blue-white
    tiles.append(img.resize((128*scale, 64*scale), Image.NEAREST))
rows = (n + cols - 1) // cols
sheet = Image.new('RGB', (cols * (128*scale + 6), rows * (64*scale + 6)), (60, 60, 60))
for k, t in enumerate(tiles):
    sheet.paste(t, ((k % cols) * (128*scale + 6) + 3, (k // cols) * (64*scale + 6) + 3))
sheet.save(dst)
print(dst, sheet.size, n, 'frames')
