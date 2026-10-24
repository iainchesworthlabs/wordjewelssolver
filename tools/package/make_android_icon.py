# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the Android application icon from the repository's icon.

    python tools/package/make_android_icon.py

Needs Pillow (`pip install pillow`); the result is committed, so only a person who
changes assets/icon runs it. It writes
libs/platforms/android/packaging/res/mipmap-xxxhdpi/ic_launcher_foreground.png.

An adaptive icon is a 108-dp square of which the launcher shows a mask-shaped part
(at most the middle 72 dp, and the mask may cut anything outside the middle 66 dp). The
background is the frame colour (res/values/colors.xml, wjs_icon_background); this is
the mark on a transparent square, scaled to the safe zone: assets/icon/app.png cropped
to inside its gold rim and laid over the frame colour, as make_ios_icon.py does it, so
that the launcher's mask does the rounding. 432 pixels is 108 dp at the highest density; the system scales it
down for the others.
"""

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/icon/app.png"
OUTPUT = (
    ROOT
    / "libs/platforms/android/packaging/res/mipmap-xxxhdpi/ic_launcher_foreground.png"
)

FRAME = (23, 50, 58)  # the dark frame inside the gold rim (#17323A, Theme.tray)
INSET = 56  # pixels of the 512-pixel tile to drop: the rim and its rounded corners
CANVAS = 432  # 108 dp at 4 pixels per dp
MARK = 264  # 66 dp: the part of the canvas no mask cuts


def main() -> int:
    tile = Image.open(SOURCE).convert("RGBA")
    if tile.size != (512, 512):
        print(f"{SOURCE} is {tile.width}x{tile.height}, expected 512x512", file=sys.stderr)
        return 1
    inner = tile.crop((INSET, INSET, 512 - INSET, 512 - INSET))
    flat = Image.new("RGBA", inner.size, FRAME + (255,))
    flat.alpha_composite(inner)
    mark = flat.resize((MARK, MARK), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    canvas.alpha_composite(mark, ((CANVAS - MARK) // 2, (CANVAS - MARK) // 2))
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(OUTPUT, optimize=True)
    print(f"{OUTPUT.relative_to(ROOT)}: {CANVAS}x{CANVAS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
