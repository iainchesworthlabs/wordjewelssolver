#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""WCAG 2.x contrast ratios for the Toybox Pixel palette pairs used by Word Jewels (wjs-tokens.json, "contrast")."""
import json, pathlib

def lum(h):
    h = h.lstrip('#')
    r, g, b = (int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))
    f = lambda c: c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)

def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)

PALETTE = {
    'ink': '#2B1D52', 'surface': '#FFF1D6', 'background': '#FFC93C', 'accent': '#FF6B4A', 'leaf': '#5BD16A',
    'tongue': '#FF2E63', 'food (blueberry)': '#4C6BFF', 'boardLight': '#1F6E7A', 'shadow': '#1A1035',
}

if __name__ == '__main__':
    tokens = json.loads((pathlib.Path(__file__).parent.parent / 'wjs-tokens.json').read_text())
    bad = 0
    for group in ('pairs', 'forbidden'):
        for p in tokens['contrast'][group]:
            got = ratio(PALETTE[p['fg']] if p['fg'] in PALETTE else '#FFFFFF', PALETTE[p['bg']])
            ok = abs(got - p['ratio']) < 0.01
            bad += not ok
            print(f"{group:9} {p['fg']:>18} on {p['bg']:<18} stated {p['ratio']:5.2f} computed {got:5.2f} {'ok' if ok else 'MISMATCH'}")
    raise SystemExit(1 if bad else 0)
