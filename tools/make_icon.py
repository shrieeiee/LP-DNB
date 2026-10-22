#!/usr/bin/env python3
"""Draws the LP-DNB icon (assets/icon/lp-dnb.ico + lp-dnb.png). Run from the repo root. Needs Pillow."""
import pathlib
from PIL import Image, ImageDraw, ImageFilter

root = pathlib.Path(__file__).resolve().parent.parent
out = root / "assets" / "icon"
out.mkdir(parents=True, exist_ok=True)

BG_TOP, BG_BOT = (58, 46, 92), (30, 25, 46)
OFF = (72, 64, 100)
# A little Launchpad: lit pads in the app's candy colours, a diagonal "beat" running through.
LIT = {
    4: {(0, 0): (255, 112, 128), (1, 1): (255, 150, 100), (2, 2): (255, 196, 92), (3, 3): (110, 225, 150),
        (0, 3): (190, 160, 255), (3, 0): (100, 215, 215), (1, 2): (255, 160, 210), (2, 1): (250, 226, 112)},
    3: {(0, 0): (255, 112, 128), (1, 1): (255, 196, 92), (2, 2): (110, 225, 150), (0, 2): (190, 160, 255),
        (2, 0): (100, 215, 215)},
}


def draw(size):
    S = 1024  # draw big, scale down for smooth edges
    n = 4 if size >= 40 else 3
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # body: rounded square with a vertical gradient
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        k = y / (S - 1)
        gd.line([(0, y), (S, y)], fill=tuple(int(a + (b - a) * k) for a, b in zip(BG_TOP, BG_BOT)) + (255,))
    mask = Image.new("L", (S, S), 0)
    m = 40 if size >= 32 else 0
    ImageDraw.Draw(mask).rounded_rectangle([m, m, S - m, S - m], radius=int(S * 0.23), fill=255)
    img.paste(grad, (0, 0), mask)

    inner = S - 2 * m
    pad_area = inner * (0.76 if n == 4 else 0.78)
    gap = pad_area * (0.075 if n == 4 else 0.1)
    p = (pad_area - gap * (n - 1)) / n
    x0 = m + (inner - pad_area) / 2
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pads = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gl, pd = ImageDraw.Draw(glow), ImageDraw.Draw(pads)
    for gy in range(n):
        for gx in range(n):
            a = (x0 + gx * (p + gap), x0 + gy * (p + gap))
            b = (a[0] + p, a[1] + p)
            col = LIT[n].get((gx, gy))
            r = p * 0.26
            if col:
                gl.rounded_rectangle([a[0] - p * 0.1, a[1] - p * 0.1, b[0] + p * 0.1, b[1] + p * 0.1], radius=r * 1.4,
                                     fill=col + (200,))
            pd.rounded_rectangle([a, b], radius=r, fill=(col or OFF) + (255,))
            if col and size >= 32:  # little shine
                pd.rounded_rectangle([a[0] + p * 0.16, a[1] + p * 0.1, b[0] - p * 0.16, a[1] + p * 0.22],
                                     radius=p * 0.06, fill=tuple(int(c + (255 - c) * 0.45) for c in col) + (255,))
    glow = glow.filter(ImageFilter.GaussianBlur(S / 45))
    glow.putalpha(Image.composite(glow.getchannel("A"), Image.new("L", (S, S), 0), mask))
    base = Image.alpha_composite(img, glow)
    base = Image.alpha_composite(base, pads)
    return base.resize((size, size), Image.LANCZOS)


sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
imgs = [draw(s) for s in sizes]
big = draw(512)
big.save(out / "lp-dnb.png")
imgs[-1].save(out / "lp-dnb.ico", sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
print("wrote", out / "lp-dnb.ico", "and lp-dnb.png")
