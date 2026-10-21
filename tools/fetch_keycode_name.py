"""Fetch the MIT-licensed HID keycode name table and adapt it for the
128x64 dongle display (longer labels, PageUp/PageDn split over two lines).

Run with GH_TOKEN set to a GitHub token that can read public repos.
"""

import base64
import json
import os
import urllib.request
from pathlib import Path

TOKEN = os.environ["GH_TOKEN"]
URL = (
    "https://api.github.com/repos/zzuse/zmk-nice-oled-zz/contents/"
    "boards/shields/nice_oled/widgets/keycode_name.c"
)

HEADER = """/*
 * HID usage -> short display label.
 *
 * Adapted from zzuse/zmk-nice-oled-zz
 * (boards/shields/nice_oled/widgets/keycode_name.c), MIT licensed.
 */

"""

REPLACEMENTS = {
    '"SPAC"': '"Space"',
    '"ENTR"': '"Enter"',
    '"PGUP"': '"Page\\nUp"',
    '"PGDN"': '"Page\\nDn"',
    '"BSPC"': '"Bspc"',
    '"ESC"': '"Esc"',
    '"CAPS"': '"Caps"',
    '"HOME"': '"Home"',
    '"END"': '"End"',
    '"INS"': '"Ins"',
    '"DEL"': '"Del"',
    '"TAB"': '"Tab"',
    '"UNKN"': '"?"',
    '"PSCR"': '"PrtSc"',
    '"SLCK"': '"ScLk"',
    '"NLCK"': '"NumLk"',
}


def main() -> None:
    req = urllib.request.Request(
        URL,
        headers={
            "Authorization": "Bearer " + TOKEN,
            "Accept": "application/vnd.github+json",
            "User-Agent": "dsh",
        },
    )
    payload = json.load(urllib.request.urlopen(req, timeout=30))
    text = base64.b64decode(payload["content"]).decode("utf-8")

    for old, new in REPLACEMENTS.items():
        text = text.replace(old, new)

    root = Path(__file__).resolve().parent.parent
    (root / "src" / "keycode_name.c").write_text(
        HEADER + text, encoding="utf-8", newline="\n"
    )
    (root / "src" / "keycode_name.h").write_text(
        "#pragma once\n\n#include <stdint.h>\n\n"
        "const char *keycode_to_string(uint32_t keycode, uint16_t usage_page);\n",
        encoding="utf-8",
        newline="\n",
    )
    print("wrote", root / "src" / "keycode_name.c", len(text))


if __name__ == "__main__":
    main()
