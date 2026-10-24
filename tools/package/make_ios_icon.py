# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the iOS application icon from the repository's icon.

    python tools/package/make_ios_icon.py

Needs Pillow (`pip install pillow`); the result is committed, so only a person who
changes assets/icon runs it. It writes
libs/platforms/ios/packaging/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png.

iOS wants one square 1024-pixel picture with no transparency and rounds the
corners itself. assets/icon/app.png is already a rounded tile with transparent
corners and a gold rim, and only 512 pixels across, so the picture is made by
cropping to inside the gold rim, laying it over the frame's own colour
(so no corner is transparent) and scaling it to 1024. The scale-up is soft: a
1024-pixel master of the mark, when there is one, replaces this file by hand.
"""

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/icon/app.png"
OUTPUT = ROOT / "libs/platforms/ios/packaging/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png"

FRAME = (23, 50, 58)  # the dark frame inside the gold rim (#17323A, Theme.tray)
INSET = 56  # pixels of the 512-pixel tile to drop: the rim and its rounded corners
SIDE = 1024


def main() -> int:
    tile = Image.open(SOURCE).convert("RGBA")
    if tile.size != (512, 512):
        print(f"{SOURCE} is {tile.width}x{tile.height}, expected 512x512", file=sys.stderr)
        return 1
    inner = tile.crop((INSET, INSET, 512 - INSET, 512 - INSET))
    flat = Image.new("RGBA", inner.size, FRAME + (255,))
    flat.alpha_composite(inner)
    icon = flat.convert("RGB").resize((SIDE, SIDE), Image.Resampling.LANCZOS)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    icon.save(OUTPUT, optimize=True)
    print(f"{OUTPUT.relative_to(ROOT)}: {SIDE}x{SIDE}, no alpha")
    return 0


if __name__ == "__main__":
    sys.exit(main())
