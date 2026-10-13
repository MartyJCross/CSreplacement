"""Draws Crisp's program icon (prototype/res/crisp.ico): a gold "C" round a crosshair dot on a dark rounded tile,
in the main menu title's gold (0xFFD060). Our own design, drawn from code. Run: python prototype/tools/gen_icon.py
(needs Pillow); writes the .ico with 16-256 px images and a 256 px preview PNG next to it."""
import math, os
from PIL import Image, ImageDraw

N = 1024  # drawn large, then scaled down for each size
GOLD = (255, 208, 96, 255)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'res')


def draw():
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    # The tile: a dark rounded square, lighter at the top.
    tile = Image.new('RGBA', (N, N))
    td = ImageDraw.Draw(tile)
    for y in range(N):
        t = y / (N - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip((44, 48, 58), (16, 18, 23)))
        td.line([(0, y), (N, y)], fill=c + (255,))
    mask = Image.new('L', (N, N), 0)
    pad = int(N * 0.04)
    ImageDraw.Draw(mask).rounded_rectangle([pad, pad, N - pad, N - pad], radius=int(N * 0.2), fill=255)
    im.paste(tile, (0, 0), mask)
    d = ImageDraw.Draw(im)
    # The "C": a thick ring open to the right, with round ends.
    cx = cy = N / 2
    r, w, gap = N * 0.29, N * 0.12, 42  # ring radius (to the middle of the stroke), stroke width, half-gap (degrees)
    box = [cx - r - w / 2, cy - r - w / 2, cx + r + w / 2, cy + r + w / 2]
    d.arc(box, start=gap, end=360 - gap, fill=GOLD, width=int(w))
    for a in (gap, -gap):
        x, y = cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=GOLD)
    # The crosshair: a dot in the middle and a tick pointing out through the gap.
    dot = N * 0.065
    d.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=GOLD)
    tw = N * 0.05
    d.rounded_rectangle([cx + N * 0.16, cy - tw / 2, cx + N * 0.36, cy + tw / 2], radius=tw / 2, fill=GOLD)
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    big = draw()
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, 'crisp_icon.png'))
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, 'crisp.ico'), sizes=[(s, s) for s in sizes])
    print('wrote', os.path.normpath(os.path.join(OUT, 'crisp.ico')))


if __name__ == '__main__':
    main()
