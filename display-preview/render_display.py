#!/usr/bin/env python3
"""Render a faithful mock of the Charybdis dongle OLED (SH1106 128x64).

The bitmaps (bongo cat, USB/BT/modifier icons) are parsed straight out of the
firmware sources, and the text uses the actual LVGL `unscii_8` glyph bitmaps
that the firmware selects in custom_status_screen.c.  Only the layout offsets
are re-created by hand from the widget alignment code.

Outputs 128x64 PNGs scaled 5x with nearest neighbour.
"""

from __future__ import annotations

import re
from pathlib import Path

from PIL import Image

ROOT = Path(r"D:\测试\charybdis-zmk")
WIDGETS = ROOT / "modules/zmk-dongle-display/boards/shields/dongle_display/widgets"
FONT_C = Path(r"D:\测试\zmk_work\modules\lib\gui\lvgl\src\font\lv_font_unscii_8.c")
OUT = Path(__file__).resolve().parent
OUT.mkdir(exist_ok=True)


def parse_bitmap(path: Path, name: str, w: int, h: int) -> list[list[int]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"uint8_t\s+{name}_map\[\]\s*=\s*\{{(.*?)\}};", text, re.S)
    if not m:
        raise SystemExit(f"bitmap {name} not found in {path}")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    data = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{1,2}", body)]
    data = data[8:]  # skip 2-colour palette
    stride = (w + 7) // 8
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            byte = data[y * stride + x // 8]
            row.append(1 if byte & (0x80 >> (x % 8)) else 0)
        rows.append(row)
    return rows


def load_font():
    text = FONT_C.read_text(encoding="utf-8", errors="replace")
    bitmap_body = text.split("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {", 1)[1]
    bitmap_body = bitmap_body.split("};", 1)[0]
    # re.split returns [pre, cp, body, cp, body, ...]
    parts = re.split(r"/\* U\+([0-9A-Fa-f]{4}).*?\*/", bitmap_body)[1:]
    entries = []
    for i in range(0, len(parts), 2):
        cp = int(parts[i], 16)
        body = re.sub(r"/\*.*?\*/", "", parts[i + 1], flags=re.S)
        data = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{1,2}", body)]
        entries.append((cp, data))

    dsc_body = text.split("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {", 1)[1]
    dsc_body = dsc_body.split("};", 1)[0]
    dscs = []
    for m in re.finditer(
        r"\{\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*(\d+),\s*\.box_w\s*=\s*(\d+),"
        r"\s*\.box_h\s*=\s*(\d+),\s*\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)\}",
        dsc_body,
    ):
        dscs.append(tuple(int(x) for x in m.groups()))

    bitmaps = {cp: data for cp, data in entries}
    # cmap: range_start 32, glyph_id_start 1
    glyphs = {}
    for cp, data in bitmaps.items():
        gid = cp - 32 + 1
        if gid >= len(dscs):
            continue
        bitmap_index, adv_w, box_w, box_h, ofs_x, ofs_y = dscs[gid]
        rows = []
        # LVGL stores each glyph's 1bpp bitmap bit-packed, row after row,
        # without padding each row to a byte boundary. Glyphs themselves are
        # byte-aligned, so start at data[0].
        for y in range(box_h):
            row = []
            for x in range(box_w):
                bit_index = y * box_w + x
                byte = data[bit_index // 8] if bit_index // 8 < len(data) else 0
                row.append(1 if byte & (0x80 >> (bit_index % 8)) else 0)
            rows.append(row)
        glyphs[chr(cp)] = (rows, box_w, box_h, ofs_x, ofs_y, adv_w / 16.0)
    return glyphs


FONT = load_font()


def draw_text(img: Image.Image, x: int, y: int, text: str, letter_space: int = 1) -> None:
    px = img.load()
    cx = x
    for ch in text:
        g = FONT.get(ch)
        if g is None:
            cx += 8 + letter_space
            continue
        rows, bw, bh, ofs_x, ofs_y, adv = g
        ox, oy = cx + ofs_x, y + ofs_y
        for yy, row in enumerate(rows):
            for xx, bit in enumerate(row):
                if bit and 0 <= ox + xx < img.width and 0 <= oy + yy < img.height:
                    px[ox + xx, oy + yy] = 255
        cx += int(round(adv)) + letter_space


def blit(img: Image.Image, x: int, y: int, rows: list[list[int]]) -> None:
    px = img.load()
    for yy, row in enumerate(rows):
        for xx, bit in enumerate(row):
            if bit and 0 <= x + xx < img.width and 0 <= y + yy < img.height:
                px[x + xx, y + yy] = 255


def battery(img: Image.Image, x: int, y: int, level: int, usb: bool = False) -> None:
    px = img.load()
    px[x, y] = 255
    px[x + 4, y] = 255
    # body outline
    for xx in range(1, 4):
        px[x + xx, y + 1] = 255
        px[x + xx, y + 7] = 255
    px[x, y + 1] = 255
    px[x + 4, y + 1] = 255
    px[x, y + 7] = 255
    px[x + 4, y + 7] = 255
    for yy in range(2, 7):
        px[x, yy + y] = 255
        px[x + 4, yy + y] = 255
    if usb:
        return
    fill = 0 if level <= 10 else 1 if level <= 30 else 2 if level <= 50 else 3 if level <= 70 else 4
    for yy in range(7 - fill, 7):
        for xx in range(1, 4):
            px[x + xx, y + yy] = 255


def draw_key(img: Image.Image, text: str) -> None:
    """Last-key overlay: 46x26 box at (31, 19), centred, max two lines."""
    lines = text.split("\n")
    line_h = 9
    y0 = 19 + (26 - line_h * len(lines)) // 2
    for i, line in enumerate(lines):
        width = len(line) * 8 + (len(line) - 1) * 1
        draw_text(img, 31 + (46 - width) // 2, y0 + i * line_h, line)


def base_screen(layer: str, mods: set[str], mouse_active: bool, usb_selected: bool,
                profile: int, profile_state: str, batteries: list[int],
                key_text: str = "A") -> Image.Image:
    img = Image.new("L", (128, 64), 0)

    usb = parse_bitmap(WIDGETS / "output_status_sym.c", "sym_usb", 9, 14)
    bt = parse_bitmap(WIDGETS / "output_status_sym.c", "sym_bt", 9, 14)
    ok = parse_bitmap(WIDGETS / "output_status_sym.c", "sym_ok", 5, 5)
    nok = parse_bitmap(WIDGETS / "output_status_sym.c", "sym_nok", 5, 5)
    opened = parse_bitmap(WIDGETS / "output_status_sym.c", "sym_open", 5, 5)
    num = parse_bitmap(WIDGETS / "output_status_sym.c", f"sym_{profile}", 5, 6)

    blit(img, 1, 4, usb)
    blit(img, 3, 6, ok if usb_selected else nok)
    blit(img, 16, 4, bt)
    blit(img, 27, 11, num)
    blit(img, 27, 5, ok if profile_state == "ok" else opened if profile_state == "open" else nok)

    # selection underline (2px) above the selected output icon
    px = img.load()
    line_y = 1
    x0, x1 = (4, 15) if usb_selected else (4, 22)
    for yy in (line_y, line_y + 1):
        for xx in range(x0, x1):
            px[xx, yy] = 255

    # batteries, top right; 5x8 icon, label right aligned at -7
    for i, level in enumerate(batteries):
        y = i * 10
        battery(img, 123, y, level)
        label = f"{level:4d}%"
        # right aligned to x=116
        width = len(label) * 8 + (len(label) - 1) * 1
        draw_text(img, 116 - width, y, label)

    # modifiers bottom left, 4 x 14px icons
    icons = [("ctrl", "control"), ("opt", "opt"), ("cmd", "cmd"), ("shift", "shift")]
    for i, (key, icon) in enumerate(icons):
        rows = parse_bitmap(WIDGETS / "modifiers_sym.c", icon, 14, 14)
        blit(img, 1 + 15 * i, 48, rows)
        if key in mods:
            px = img.load()
            for xx in range(1 + 15 * i, 1 + 15 * i + 14):
                for yy in (62, 63):
                    px[xx, yy] = 255

    # bongo cat bottom right (50x26), 7px above bottom
    cat_name = "bongo_cat_both1_open" if not mouse_active else "bongo_cat_both1"
    cat = parse_bitmap(WIDGETS / "bongo_cat_images.c", cat_name, 50, 26)
    blit(img, 78, 31, cat)

    # layer label under the cat, right aligned to the cat's right edge
    width = len(layer) * 8 + (len(layer) - 1) * 1
    draw_text(img, 128 - width, 54, layer)

    # last key overlay (red box lower half)
    draw_key(img, key_text)

    return img


def save(img: Image.Image, name: str) -> None:
    scale = 5
    big = img.resize((128 * scale, 64 * scale), Image.NEAREST)
    canvas = Image.new("L", (big.width + 24, big.height + 24), 40)
    canvas.paste(big, (12, 12))
    canvas.save(OUT / name)
    img.save(OUT / name.replace(".png", "-1x.png"))


from PIL import ImageDraw, ImageFont  # noqa: E402


def compose(base: Image.Image, mouse: Image.Image) -> None:
    scale = 5
    panel_w, panel_h = 128 * scale + 24, 64 * scale + 24
    canvas = Image.new("RGB", (760, 840), (28, 28, 30))
    draw = ImageDraw.Draw(canvas)
    try:
        title_font = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 24)
        cap_font = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 18)
    except OSError:
        title_font = cap_font = ImageFont.load_default()

    draw.text((24, 18), "Charybdis dongle OLED 128x64 - firmware mock", font=title_font,
              fill=(235, 235, 235))

    for idx, (label, img) in enumerate([
        ("idle: base layer, last key A, both halves 87% / 92%", base),
        ("trackball moving: layer mouse, last key PageUp", mouse),
    ]):
        y = 70 + idx * 390
        big = img.resize((128 * scale, 64 * scale), Image.NEAREST)
        frame = Image.new("RGB", (panel_w, panel_h), (40, 40, 44))
        frame.paste(big.convert("RGB"), (12, 12))
        canvas.paste(frame, (24, y))
        draw.text((26, y + panel_h + 10), label, font=cap_font, fill=(180, 180, 185))

    canvas.save(OUT / "dongle-display-preview.png")


if __name__ == "__main__":
    base = base_screen(
        layer="base",
        mods=set(),
        mouse_active=False,
        usb_selected=True,
        profile=1,
        profile_state="open",
        batteries=[87, 92],
        key_text="A",
    )
    save(base, "dongle-display-base.png")

    mouse = base_screen(
        layer="mouse",
        mods={"cmd"},
        mouse_active=True,
        usb_selected=True,
        profile=1,
        profile_state="open",
        batteries=[87, 92],
        key_text="Page\nUp",
    )
    save(mouse, "dongle-display-mouse.png")
    compose(base, mouse)
    print("written", OUT)
