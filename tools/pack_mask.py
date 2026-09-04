"""Pack mask animation frames onto a black background as PROGMEM RGB565."""
from collections import deque
from pathlib import Path
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
IMG = Path(r"C:\Users\CirusThavirus\.grok\sessions\C%3A%5CUsers%5CCirusThavirus\01a064bb-de87-7ba2-a29a-aa158a636ac0\images")
ASSETS = ROOT / "assets" / "mask"
OUT = ROOT / "src" / "mask_frames.h"
SIZE = 128

FRAMES = [
    ("1.jpg", "idle"),
    ("7.jpg", "look_left"),
    ("6.jpg", "look_right"),
    ("2.jpg", "blink"),
    ("5.jpg", "grin"),
    ("4.jpg", "tilt"),
]


def is_chroma(r, g, b):
    if min(r, g, b) > 215:
        return True
    if r > 165 and b > 165 and g < 175 and abs(int(r) - int(b)) < 80:
        return True
    return False


def flood_bg(im: Image.Image) -> Image.Image:
    im = im.convert("RGB")
    w, h = im.size
    pix = im.load()
    vis = bytearray(w * h)
    q = deque()

    def push(x, y):
        i = y * w + x
        if vis[i]:
            return
        r, g, b = pix[x, y]
        if not is_chroma(r, g, b):
            return
        vis[i] = 1
        q.append((x, y))

    for x in range(w):
        push(x, 0)
        push(x, h - 1)
    for y in range(h):
        push(0, y)
        push(w - 1, y)

    while q:
        x, y = q.popleft()
        if x > 0:
            push(x - 1, y)
        if x + 1 < w:
            push(x + 1, y)
        if y > 0:
            push(x, y - 1)
        if y + 1 < h:
            push(x, y + 1)

    # dilate halo
    extra = []
    for y in range(h):
        row = y * w
        for x in range(w):
            if vis[row + x]:
                continue
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and vis[ny * w + nx]:
                    extra.append((x, y))
                    break
    for x, y in extra:
        vis[y * w + x] = 1

    for y in range(h):
        row = y * w
        for x in range(w):
            if vis[row + x]:
                pix[x, y] = (0, 0, 0)
    return im


def crop_subject(im: Image.Image) -> Image.Image:
    pix = im.load()
    w, h = im.size
    minx, miny, maxx, maxy = w, h, 0, 0
    found = False
    for y in range(h):
        for x in range(w):
            r, g, b = pix[x, y]
            if r > 12 or g > 12 or b > 12:
                found = True
                if x < minx:
                    minx = x
                if y < miny:
                    miny = y
                if x > maxx:
                    maxx = x
                if y > maxy:
                    maxy = y
    if not found:
        return Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    pad = 6
    minx = max(0, minx - pad)
    miny = max(0, miny - pad)
    maxx = min(w - 1, maxx + pad)
    maxy = min(h - 1, maxy + pad)
    cropped = im.crop((minx, miny, maxx + 1, maxy + 1))
    side = max(cropped.size)
    canvas = Image.new("RGB", (side, side), (0, 0, 0))
    canvas.paste(cropped, ((side - cropped.size[0]) // 2, (side - cropped.size[1]) // 2))
    out = canvas.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    # kill leftover fringe after resize
    p = out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = p[x, y]
            if is_chroma(r, g, b) or (r < 18 and g < 18 and b < 18):
                p[x, y] = (0, 0, 0)
    return out


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def resolve(name):
    for folder in (IMG, ASSETS):
        p = folder / name
        if p.exists():
            return p
    raise FileNotFoundError(name)


def pack():
    lines = [
        "#pragma once",
        "#include <Arduino.h>",
        f"#define MASK_W {SIZE}",
        f"#define MASK_H {SIZE}",
        f"#define MASK_FRAMES {len(FRAMES)}",
        "",
        f"static const uint16_t MASK_DATA[MASK_FRAMES][MASK_W * MASK_H] PROGMEM = {{",
    ]
    for idx, (name, tag) in enumerate(FRAMES):
        path = resolve(name)
        im = crop_subject(flood_bg(Image.open(path)))
        pix = im.load()
        lines.append(f"  {{ // {idx} {tag}")
        row = []
        for y in range(SIZE):
            for x in range(SIZE):
                r, g, b = pix[x, y]
                v = rgb565(r, g, b)
                row.append(f"0x{v:04X}")
                if len(row) == 12:
                    lines.append("    " + ", ".join(row) + ",")
                    row = []
        if row:
            lines.append("    " + ", ".join(row) + ",")
        lines.append("  },")
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT} frames={len(FRAMES)} {SIZE}x{SIZE} black-bg")


if __name__ == "__main__":
    pack()
