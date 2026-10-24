# SPDX-License-Identifier: GPL-3.0-or-later
"""Write a macOS .icns file from the repository's PNG icons.

    python -I tools/package/make_icns.py <output.icns>

An .icns file is a short header followed by typed chunks; macOS accepts PNG data
inside a chunk for the sizes from 64 pixels up, so no image library is needed and
the file is the PNGs themselves. The sizes used are the ones assets/icon has:
64 (icp6), 256 (ic08) and 512 (ic09). The file is the same for the same PNGs.
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHUNKS = (
    ("icp6", 64, ROOT / "assets/icon/app-64.png"),
    ("ic08", 256, ROOT / "assets/icon/app-256.png"),
    ("ic09", 512, ROOT / "assets/icon/app.png"),
)


def png_size(data: bytes) -> tuple[int, int]:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    return struct.unpack(">II", data[16:24])


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    body = b""
    for kind, side, path in CHUNKS:
        data = path.read_bytes()
        width, height = png_size(data)
        if (width, height) != (side, side):
            print(f"{path} is {width}x{height}, expected {side}x{side}", file=sys.stderr)
            return 1
        body += kind.encode("ascii") + struct.pack(">I", 8 + len(data)) + data
    output = b"icns" + struct.pack(">I", 8 + len(body)) + body
    Path(argv[1]).write_bytes(output)
    print(f"{argv[1]}: {len(output)} bytes, sizes {', '.join(str(side) for _, side, _ in CHUNKS)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
