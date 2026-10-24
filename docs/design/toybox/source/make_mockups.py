#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the Toybox Pixel mockups for Word Jewels Solver: toybox.css, screens/*.html and gallery.html.

The mockups are design material, not production code. Values come from tokens.json (Snake, unchanged) and
wjs-tokens.json (what Word Jewels adds). Strings for the Chinese and Hebrew variants are read from the
project's own catalogues (../wordjewelssolver/i18n/*.ts) so the layouts are checked with real text.

usage: python3 source/make_mockups.py
"""
import html
import json
import pathlib
import xml.etree.ElementTree as ET

HERE = pathlib.Path(__file__).resolve().parent.parent
import os


def _find_catalogues():
    # WJS_I18N overrides; otherwise look for the repository's catalogues above this directory
    if os.environ.get('WJS_I18N'):
        return pathlib.Path(os.environ['WJS_I18N'])
    for parent in HERE.parents:
        for rel in ('i18n', 'apps/app/translations'):
            if list((parent / rel).glob('wjs_*.ts')):
                return parent / rel
    return HERE / 'i18n'


REPO_I18N = _find_catalogues()
TOK = json.loads((HERE / 'tokens.json').read_text())
C = {k: v['value'] for k, v in TOK['color'].items()}

# ---------------------------------------------------------------- translations
_CAT = {}


def tr(lang, text):
    if lang == 'en':
        return text
    if lang not in _CAT:
        cat = {}
        f = REPO_I18N / f'wjs_{lang}.ts'
        if f.exists():
            for m in ET.parse(f).getroot().iter('message'):
                s, t = m.find('source'), m.find('translation')
                if s is not None and t is not None and t.text:
                    cat[s.text] = t.text
        _CAT[lang] = cat
    return _CAT[lang].get(text, text)


# ---------------------------------------------------------------- pixel art
def pix(rows, fill='currentColor', cell=3):
    h, w = len(rows), max(len(r) for r in rows)
    rects = ''.join(f'<rect x="{x*cell}" y="{y*cell}" width="{cell}" height="{cell}"/>'
                    for y, r in enumerate(rows) for x, ch in enumerate(r) if ch == '#')
    return (f'<svg width="{w*cell}" height="{h*cell}" viewBox="0 0 {w*cell} {h*cell}" fill="{fill}" '
            f'shape-rendering="crispEdges" aria-hidden="true">{rects}</svg>')


ICON = {
    'check': ['.......#', '......##', '#....##.', '##..##..', '.####...', '..##....'],
    'back': ['...#', '..##', '.##.', '##..', '.##.', '..##', '...#'],
    'x': ['##...##', '###.###', '.#####.', '..###..', '.#####.', '###.###', '##...##'],
    'play': ['#...', '##..', '###.', '####', '###.', '##..', '#...'],
    'person': ['...###...', '..#####..', '..#####..', '..#####..', '...###...', '.#######.', '#########', '#########', '#########'],
    'camera': ['...####.....', '#############', '##.........##', '##...###...##', '##..#####..##', '##..#####..##', '##...###...##', '##.........##', '#############'],
    'boards': ['####.####', '####.####', '####.####', '.........', '####.####', '####.####', '####.####'],
    'retry': ['..####..', '.#....#.', '#......#', '#....###', '#.....#.', '.#...#..', '..###...'],
    'shake': ['..#......#..', '.#..####..#.', '#...#..#...#', '#...#..#...#', '.#..####..#.', '..#......#..'],
    'q': ['.####.', '#....#', '.....#', '...##.', '..#...', '......', '..#...'],
    'hint': ['..###..', '.#...#.', '#..#..#', '#.###.#', '#..#..#', '.#...#.', '..###..'],
}


def icon(name, color='currentColor', cell=3):
    return pix(ICON[name], color, cell)


def _star_mask(n=15):
    import math
    pts = []
    for i in range(10):
        r = 7.3 if i % 2 == 0 else 3.6
        a = -math.pi / 2 + i * math.pi / 5
        pts.append((7.0 + r * math.cos(a), 7.6 + r * math.sin(a)))

    def inside(px, py):
        c = False
        for i in range(len(pts)):
            x1, y1 = pts[i]
            x2, y2 = pts[(i + 1) % len(pts)]
            if (y1 > py) != (y2 > py) and px < (x2 - x1) * (py - y1) / (y2 - y1) + x1:
                c = not c
        return c
    return {(x, y) for y in range(n) for x in range(n) if inside(x + 0.5, y + 0.5)}


def gem(tier=1, color=None, cell=3):
    color = color or C['accent']
    if tier == 1:
        m = {(x, y) for y in range(9) for x in range(9) if abs(x - 4) + abs(y - 4) <= 4}
    elif tier == 2:
        spans = [(3, 7), (1, 9), (0, 10), (0, 10), (1, 9), (2, 8), (3, 7), (4, 6), (5, 5)]
        m = {(x, y) for y, (a, b) in enumerate(spans) for x in range(a, b + 1)}
    else:
        m = _star_mask()
    xs, ys = [p[0] for p in m], [p[1] for p in m]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    w, h = max(xs) + 1, max(ys) + 1
    out = []
    for (x, y) in sorted(m, key=lambda p: (p[1], p[0])):
        edge = any((x + dx, y + dy) not in m for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if edge:
            fill, op = C['ink'], 1
        else:
            fill, op = color, 1
        out.append(f'<rect x="{x*cell}" y="{y*cell}" width="{cell}" height="{cell}" fill="{fill}"/>')
        if not edge:
            d = (x - cx) + (y - cy)
            if d < -h * 0.22:
                out.append(f'<rect x="{x*cell}" y="{y*cell}" width="{cell}" height="{cell}" fill="#fff" opacity=".6"/>')
            elif d > h * 0.22:
                out.append(f'<rect x="{x*cell}" y="{y*cell}" width="{cell}" height="{cell}" fill="{C["ink"]}" opacity=".28"/>')
    return (f'<svg class="gem" width="{w*cell}" height="{h*cell}" viewBox="0 0 {w*cell} {h*cell}" '
            f'shape-rendering="crispEdges" aria-hidden="true">{"".join(out)}</svg>')


JEWEL = [C['accent'], C['food'], C['leaf'], C['tongue'], C['background']]

# ---------------------------------------------------------------- CSS
def css():
    return f"""/* Generated by source/make_mockups.py from tokens.json (Snake, unchanged) and wjs-tokens.json. Do not edit. */
:root{{
  --sun:{C['background']};--ink:{C['ink']};--shadow:{C['shadow']};--cream:{C['surface']};--coral:{C['accent']};
  --teal-l:{C['boardLight']};--teal-d:{C['boardDark']};--blue:{C['food']};--leaf:{C['leaf']};--pink:{C['tongue']};
  --dim:#EBD9B4;
  --page:var(--sun);--text:var(--ink);--outline:var(--ink);--panel:var(--cream);--panel-text:var(--ink);--hard:var(--shadow);
  --btn1-bg:var(--ink);--btn1-text:var(--cream);--btn2-bg:var(--cream);--btn2-text:var(--ink);--sel:var(--coral);--sel-text:var(--ink);
  --die-face:var(--cream);--die-ink:var(--ink);--die-edge:var(--ink);
  --cjk:'Microsoft YaHei','PingFang SC','Noto Sans SC','Yu Gothic UI','Noto Sans JP',sans-serif;--chrome:'Pixelify Sans','Segoe UI',var(--cjk),'Courier New',monospace;--reading:'Andika','Segoe UI',var(--cjk),sans-serif;--counter:'Silkscreen','Courier New',monospace;
  --scale:1;
}}
[data-theme=night]{{
  --page:var(--shadow);--text:var(--cream);--outline:var(--cream);--panel:var(--ink);--panel-text:var(--cream);--hard:#0B0618;
  --btn1-bg:var(--sun);--btn1-text:var(--ink);--btn2-bg:var(--ink);--btn2-text:var(--cream);
}}
*{{box-sizing:border-box}}
html,body{{margin:0}}
body{{background:#26232E;font-family:var(--reading);color:var(--text)}}
.shell{{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:56px 32px 32px}}
.bare .shell{{padding:0;min-height:0;display:block}}.bare .back{{display:none}}.bare body{{background:var(--page)}}
.back{{position:fixed;top:12px;left:12px;z-index:10;font:600 13px system-ui,sans-serif;color:#EDEAF5;text-decoration:none;background:rgba(255,255,255,.1);padding:8px 12px;border-radius:8px}}
.app{{position:relative;overflow:hidden;background:var(--page);color:var(--text);display:flex;flex-direction:column;font-size:calc(18px*var(--scale))}}
.app[dir=rtl]{{direction:rtl}}
/* type roles */
.wordmark{{font:700 40px/1 var(--chrome);text-shadow:3px 3px 0 var(--coral);letter-spacing:.02em;margin:0}}
.hero{{font:700 56px/1.05 var(--chrome);margin:0}}
.title{{font:700 34px/1.1 var(--chrome);margin:0}}
.heading{{font:700 24px/1.2 var(--chrome);margin:0}}
.label{{font:500 16px/1.2 var(--chrome)}}
.eyebrow{{font:400 16px/1 var(--counter);letter-spacing:.06em;text-transform:uppercase}}  /* letters only: Silkscreen digits are not used */
.body{{font:400 calc(18px*var(--scale))/1.5 var(--reading);margin:0}}
.caption{{font:400 14px/1.4 var(--reading)}}
.points{{direction:ltr;unicode-bidi:isolate;display:inline-block;font:700 20px/1 var(--reading);font-variant-numeric:tabular-nums;letter-spacing:.04em}}
/* bars and panels */
.bar{{height:72px;flex:none;display:flex;align-items:center;gap:16px;padding:0 20px;border-bottom:4px solid var(--outline);background:var(--panel);color:var(--panel-text)}}
.bar .grow{{flex:1}}
.panel{{background:var(--panel);color:var(--panel-text);border:4px solid var(--outline);border-radius:18px;box-shadow:0 6px 0 var(--hard)}}
.dialog{{background:var(--panel);color:var(--panel-text);border:4px solid var(--outline);border-radius:20px;box-shadow:0 6px 0 var(--hard)}}
.chip{{display:inline-flex;align-items:center;gap:8px;height:32px;padding:0 12px;border:3px solid var(--outline);border-radius:8px;background:var(--panel);color:var(--panel-text);font:700 16px/1 var(--reading);box-shadow:0 3px 0 var(--hard)}}
.chip.sun{{background:var(--sun);color:var(--ink)}}
/* buttons */
.btn{{display:inline-flex;align-items:center;justify-content:center;gap:10px;min-height:48px;padding:0 20px;border:3px solid var(--outline);border-radius:14px;background:var(--btn2-bg);color:var(--btn2-text);font:700 18px/1 var(--chrome);box-shadow:0 4px 0 var(--hard);cursor:pointer}}
.btn.p{{background:var(--btn1-bg);color:var(--btn1-text)}}
.btn.child{{min-height:64px;font-size:22px}}
.btn.primary{{min-height:96px;font-size:26px;border-radius:14px;box-shadow:0 6px 0 var(--hard)}}
.btn.ghost{{background:transparent;box-shadow:none;border-color:transparent}}
.btn.danger{{border:4px solid var(--pink)}} /* ink text on a cream face with a pink outline: ink on pink is 4.16:1, so pink never carries text */
.btn.off{{background:var(--dim);color:var(--ink);border-style:dashed;box-shadow:none}}
.btn.pressed{{transform:translateY(4px);box-shadow:none}}
.btn.focus{{outline:4px dashed var(--outline);outline-offset:3px}}
.seg{{display:flex;flex-wrap:wrap;gap:8px}}
.seg .btn{{min-width:64px}}
.seg .btn.num,.btn.num{{font-family:var(--reading);font-weight:700}}
.seg .btn.sel{{background:var(--sel);color:var(--sel-text);border-width:4px}}
/* dice, tray, path */
.tray{{position:relative;background:var(--ink);padding:14px;border-radius:12px;box-shadow:0 12px 0 var(--hard);border:6px solid var(--outline);display:inline-block}}
[data-theme=night] .tray{{border-color:var(--cream)}}
.well{{display:grid;gap:10px;position:relative}}
.cell{{position:relative;z-index:1;width:var(--tile);height:var(--tile);background:var(--teal-l)}}
.cell.dk{{background:var(--teal-d)}}
.die{{position:absolute;inset:0;background:var(--die-face);color:var(--die-ink);border:3px solid var(--die-edge);border-radius:10px;box-shadow:0 4px 0 var(--shadow);display:grid;place-items:center;font:700 calc(var(--tile)*.55)/1 var(--reading)}}
.die.qu{{font-size:calc(var(--tile)*.42)}}
.die.inpath{{background:var(--coral);transform:translateY(3px);box-shadow:none}}
.die.start{{background:var(--sun);outline:4px dashed var(--cream);outline-offset:3px}}
.die.hint{{background:var(--sun)}}
.die.valid{{background:var(--leaf);transform:translateY(3px);box-shadow:none}}
.die.invalid{{border:4px solid var(--pink)}}
.die.dis{{background:var(--dim);border-style:dashed;box-shadow:none}}
.pip{{position:absolute;width:26px;height:26px;border-radius:50%;display:grid;place-items:center;border:3px solid var(--ink);font:700 15px/1 var(--reading);background:var(--cream);color:var(--ink)}}
.pip.tl{{top:-10px;left:-10px}}.pip.tr{{top:-10px;right:-10px}}
.pip.ink{{background:var(--ink);color:var(--cream);border-color:var(--cream)}}
.pip.sun{{background:var(--sun)}}
.path{{position:absolute;inset:0;pointer-events:none;z-index:3}}
/* word list */
.words{{display:flex;flex-direction:column}}
.word{{display:flex;align-items:center;gap:12px;min-height:48px;padding:6px 14px;border-top:3px dashed rgba(43,29,82,.25)}}
[data-theme=night] .word{{border-top-color:rgba(255,241,214,.25)}}
.word:first-child{{border-top:0}}
.word .w{{flex:1;font:400 calc(24px*var(--scale))/1.2 var(--reading)}}
.word .pt{{direction:ltr;unicode-bidi:isolate;font:700 18px/1 var(--reading)}}
.art{{flex:1 1 0;min-height:48px;display:flex;justify-content:center;align-items:center;overflow:hidden}}.art>svg{{height:100%;width:auto;max-width:100%}}.art>div{{max-height:100%}}
.bar-prog{{display:flex;gap:3px;width:100%}}.bar-prog i{{flex:1;min-width:8px;height:20px;background:var(--dim);border:2px solid var(--outline);display:block}}.bar-prog i.on{{background:var(--coral)}}
.scrim{{position:absolute;inset:0;background:rgba(43,29,82,.8);z-index:10}}
.tabs{{display:flex;gap:8px}}
.tabs .btn{{min-height:48px;box-shadow:none;border-bottom-width:3px}}
.tabs .btn.sel{{background:var(--sel);color:var(--sel-text);border-width:4px}}
.row{{display:flex;align-items:center;gap:24px;padding:16px 20px;border-top:3px dashed rgba(43,29,82,.25)}}
[data-theme=night] .row{{border-top-color:rgba(255,241,214,.25)}}
.row:first-child{{border-top:0}}.row .t{{flex:1;min-width:0}}.row .t b{{font:700 18px/1.2 var(--chrome);display:block;margin-bottom:4px}}
.stat{{padding:12px 20px;flex:1}}.stat .n{{font:700 36px/1 var(--reading);letter-spacing:.06em;margin:6px 0 2px}}
.mono{{font:400 15px/1.6 'Courier New',monospace;white-space:pre}}
@media (prefers-reduced-motion:no-preference){{.bob{{animation:bob .9s ease-in-out infinite}}}}
@keyframes bob{{0%,100%{{transform:translateY(0)}}50%{{transform:translateY(-3px)}}}}
"""


# ---------------------------------------------------------------- components
def page(name, title, body, w, h, theme='day', lang='en', rtl=False, scale=1.0, desc=''):
    t = 'night' if theme == 'night' else 'day'
    return f"""<!doctype html>
<html lang="{lang}"{' dir="rtl"' if rtl else ''}>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Word Jewels Solver: {html.escape(title)}</title>
<link rel="stylesheet" href="../fonts/fonts.css">
<link rel="stylesheet" href="../toybox.css">
<script>if(location.hash==='#bare')document.documentElement.classList.add('bare')</script>
</head>
<body>
<a class="back" href="../gallery.html">&#8592; All screens</a>
<div class="shell">
<div class="app" data-theme="{t}" dir="{'rtl' if rtl else 'ltr'}" style="width:{w}px;height:{h}px;--scale:{scale}">
{body}
</div>
</div>
</body>
</html>
"""


def btn(label, cls='', ic=None, extra=''):
    i = icon(ic, cell=3) if ic else ''
    if any(ch.isdigit() for ch in label):
        cls += ' num'
    return f'<span class="btn {cls}" {extra}>{i}{html.escape(label)}</span>'


def seg(options, selected, cls=''):
    out = []
    for o in options:
        s = o == selected
        out.append(f'<span class="btn {cls} {"num" if any(ch.isdigit() for ch in str(o)) else ""} {"sel" if s else ""}">{icon("check") if s else ""}{html.escape(str(o))}</span>')
    return f'<div class="seg">{"".join(out)}</div>'


def wordmark(lang='en', hero=False):
    return f'<h1 class="wordmark" style="{"font-size:76px" if hero else ""}">{html.escape(tr(lang, "Word Jewels"))}</h1>'


def appbar(title='', back=True, right='', lang='en', mark=False):
    l = tr(lang, 'Back')
    left = f'<span class="btn ghost" style="padding:0 8px">{icon("back")}{html.escape(l)}</span>' if back else ''
    mk = f'<span style="display:flex;align-items:center;gap:12px">{gem(2, C["accent"], 3)}{wordmark(lang)}</span>' if mark else ''
    t = f'<h2 class="title" style="font-size:28px">{html.escape(title)}</h2>' if title else ''
    return f'<div class="bar">{left}{mk}{t}<span class="grow"></span>{right}</div>'


def die(letter, state='', order=None, pip=None, qu=False):
    mark = ''
    if order is not None:
        mark = f'<span class="pip tl ink">{order}</span>'
    if state == 'start':
        mark = f'<span class="pip tl sun">{icon("play", cell=2)}</span>'
    if state == 'hint':
        mark = '<span class="pip tl sun"><span style="width:8px;height:8px;background:var(--ink);display:block"></span></span>'
    if pip == 'ok':
        mark += f'<span class="pip tr">{icon("check", cell=2)}</span>'
    if pip == 'x':
        mark += f'<span class="pip tr ink">{icon("x", cell=2)}</span>'
    if pip == 'q':
        mark += f'<span class="pip tr sun" style="font-size:16px">?</span>'
    return f'<div class="die {state} {"qu" if qu else ""}">{html.escape(letter)}{mark}</div>'


def board(letters, cols, tile=180, states=None, pips=None, orders=None, path=None):
    states, pips, orders = states or {}, pips or {}, orders or {}
    cells = []
    for i, ch in enumerate(letters):
        r, c = divmod(i, cols)
        dk = (r + c) % 2
        cells.append(f'<div class="cell {"dk" if dk else ""}">{die(ch, states.get(i, ""), orders.get(i), pips.get(i), qu=(len(ch) > 1))}</div>')
    rows = len(letters) // cols
    svg = ''
    if path:
        pts = ' '.join(f'{(i%cols)*(tile+10)+tile/2},{(i//cols)*(tile+10)+tile/2}' for i in path)
        w, h = cols * tile + (cols - 1) * 10, rows * tile + (rows - 1) * 10
        svg = (f'<svg class="path" width="{w}" height="{h}" viewBox="0 0 {w} {h}" fill="none" shape-rendering="crispEdges">'
               f'<polyline points="{pts}" stroke="{C["ink"]}" stroke-width="16" stroke-dasharray="10 16" stroke-linecap="butt"/>'
               f'<polyline points="{pts}" stroke="{C["surface"]}" stroke-width="8" stroke-dasharray="10 16" stroke-dashoffset="-1" stroke-linecap="butt"/></svg>')
    return (f'<div class="tray" style="--tile:{tile}px"><div class="well" style="grid-template-columns:repeat({cols},{tile}px)" dir="ltr">'
            f'{"".join(cells)}{svg}</div></div>')


def wordrow(word, pts, tier, i):
    return f'<div class="word">{gem(tier, JEWEL[i % 5], 3)}<span class="w">{html.escape(word)}</span><span class="pt">+{pts}</span></div>'


def box_art(open_lid=True, w=420):
    # a pixel box: ink-framed body, teal well with nine cells, a cream lid ajar
    cells = ''.join(f'<rect x="{104+(i%3)*74}" y="{118+(i//3)*66}" width="64" height="56" fill="{"#1F6E7A" if (i%3+i//3)%2==0 else "#1A6170"}" stroke="{C["ink"]}" stroke-width="3"/>' for i in range(9))
    lid = (f'<g transform="rotate(-5 210 100)"><rect x="40" y="36" width="340" height="72" rx="10" fill="{C["surface"]}" stroke="{C["ink"]}" stroke-width="6"/>'
           f'<rect x="40" y="96" width="340" height="12" fill="{C["ink"]}" opacity=".28"/>'
           f'<rect x="184" y="56" width="52" height="24" fill="{C["accent"]}" stroke="{C["ink"]}" stroke-width="3"/></g>') if open_lid else ''
    return (f'<svg width="{w}" height="{int(w*0.8)}" viewBox="0 0 420 336" shape-rendering="crispEdges" aria-hidden="true">'
            f'<rect x="30" y="112" width="360" height="226" rx="12" fill="{C["shadow"]}"/>'
            f'<rect x="30" y="100" width="360" height="226" rx="12" fill="{C["ink"]}" stroke="{C["ink"]}" stroke-width="6"/>'
            f'<rect x="54" y="106" width="312" height="208" fill="{C["boardDark"]}"/>{cells}{lid}</svg>')


# ---------------------------------------------------------------- screens
SCREENS = []  # (file, title, description, builder)


def screen(file, title, desc, w=1160, h=740, **kw):
    def deco(fn):
        SCREENS.append((file, title, desc, w, h, kw, fn))
        return fn
    return deco


LETTERS = list('ENSOAIMPT')


@screen('home', 'Home', 'Two large cards: Solo Play for the child and the real board for a grown-up.')
def home(lang='en', **kw):
    t = lambda s: tr(lang, s)
    right = ''.join([btn(t('My boards'), '', 'boards'), btn(t('My jewels'), '', None), btn(t('For grown-ups'), '', 'person')])
    right = f'<span style="display:flex;gap:12px">{right}</span>'
    card1 = (f'<div class="panel" style="flex:1;padding:20px;display:flex;flex-direction:column;gap:8px"><div class="art">{box_art(True, 236)}</div>'
             f'<span class="eyebrow">{html.escape(t("For children"))}</span><h3 class="heading" style="font-size:30px">{html.escape(t("Play on my own"))}</h3>'
             f'<p class="body">{html.escape(t("Shake the box. Find words. Collect jewels."))}</p>'
             f'<div style="margin-top:auto">{btn(t("Play"), "p primary", None, "style=\"width:100%\"")}</div></div>')
    card2 = (f'<div class="panel" style="flex:1;padding:20px;display:flex;flex-direction:column;gap:8px"><div class="art">'
             f'<div style="border:6px solid var(--ink);border-radius:12px;box-shadow:0 8px 0 var(--shadow);padding:12px;background:var(--cream)"><div style="display:grid;grid-template-columns:repeat(4,30px);gap:5px">'
             + ''.join('<i style="width:30px;height:30px;border:3px solid var(--ink);border-radius:6px;display:block;background:var(--cream)"></i>' for _ in range(16)) +
             f'</div></div></div><span class="eyebrow">{html.escape(t("For grown-ups"))}</span><h3 class="heading" style="font-size:30px">{html.escape(t("Play with a real board"))}</h3>'
             f'<p class="body">{html.escape(t("Use the camera, or type the board. A grown-up sets this up."))}</p>'
             f'<div style="margin-top:auto">{btn(t("Continue"), "child", None, "style=\"width:100%\"")}</div></div>')
    body = (f'<div class="bar" style="background:transparent;border-bottom:0;height:88px"><span style="display:flex;align-items:center;gap:14px">{gem(2, C["accent"], 4)}{wordmark(lang)}</span><span class="grow"></span>{right}</div>'
            f'<div style="padding:0 32px;flex:none"><span class="eyebrow">{html.escape(t("A word-finding game for young readers"))}</span>'
            f'<h2 class="hero" style="margin-top:6px;font-size:52px">{html.escape(t("Shake. Find. Collect."))}</h2></div>'
            f'<div style="display:flex;gap:28px;padding:18px 32px 0;flex:1;min-height:0">{card1}{card2}</div>'
            f'<div style="padding:16px 32px 20px;font:400 14px var(--reading);display:flex;justify-content:space-between"><span class="eyebrow" style="font-size:14px">{html.escape(t("Word Jewels Solver"))}</span><span>{html.escape(t("Everything stays on this device."))}</span></div>')
    return body


@screen('home-zh', 'Home in Chinese', 'The same layout with the project\'s Simplified Chinese catalogue (system CJK fallback in this render).', lang='zh')
def home_zh(lang='zh', **kw):
    return home(lang='zh')


@screen('solo-box', 'Solo Play: the box', 'Choose a board, then shake. The closed box shakes in eight pixel steps; reduced motion keeps it still.')
def solo_box(lang='en', **kw):
    t = lambda s: tr(lang, s)
    steps = ''.join(f'<div style="display:flex;align-items:center;gap:12px;padding:6px 0"><span class="pip ink" style="position:static;width:30px;height:30px;font-size:17px;font-family:var(--reading)">{i}</span><span class="body">{html.escape(t(s))}</span></div>'
                    for i, s in enumerate(['Shake the box.', 'Find a word.', 'Collect the jewels.'], 1))
    side = (f'<div class="panel" style="margin:20px;padding:20px;display:flex;flex-direction:column;gap:16px;flex:1"><h3 class="heading">{html.escape(t("Choose a board"))}</h3>'
            f'<p class="caption" style="margin:0">{html.escape(t("3 x 3 is a good start. Bigger boards hold more words."))}</p>'
            f'{seg(["3", "4"], "3", "child num")}<div class="panel" style="padding:12px 16px;box-shadow:0 4px 0 var(--hard);border-width:3px"><h4 class="heading" style="font-size:20px;margin-bottom:6px">{html.escape(t("How it works"))}</h4>{steps}</div>'
            f'<div style="margin-top:auto">{btn(t("Shake"), "p primary", "shake", "style=\"width:100%\"")}</div></div>')
    return (appbar(t('Solo Play'), True, btn(t('For grown-ups'), '', 'person'), lang)
            + f'<div style="display:flex;flex:1;min-height:0"><div style="flex:1.15;display:grid;place-items:center;align-content:center;gap:16px">{box_art(True, 440)}<p class="body" style="font-size:22px">{html.escape(t("Shake the box to start"))}</p></div><div style="width:410px;display:flex">{side}</div></div>')


def playing(lang='en', state='found', night=False, tile=180):
    t = lambda s: tr(lang, s)
    L = LETTERS
    kw = {}
    msg = ''
    words = [('sit', 13, 1), ('tap', 15, 2)]
    if state == 'empty':
        words = []
    if state == 'path':   # building P-A-T, M-P-T ...
        kw = dict(states={1: '', 2: ''}, orders={})
        path = [7, 4, 8]  # P A T? positions: M P T / O A I -> P(7) A(4) .. fine for the picture
        kw = dict(states={i: 'inpath' for i in path}, orders={i: n + 1 for n, i in enumerate(path)}, path=path)
        msg = f'<div dir="ltr" style="text-align:start"><h4 class="heading" style="font-size:30px">PAT</h4><p class="caption" style="margin:0">{html.escape(t("Tap Done when you have a word."))}</p></div>'
    elif state == 'found':
        path = [4, 7, 8]
        kw = dict(states={i: 'valid' for i in path}, pips={8: 'ok'})
        msg = f'<div dir="ltr" style="text-align:start"><h4 class="heading" style="font-size:30px">TAP!</h4><p class="caption" style="margin:0">{html.escape(t("You found a word."))} +15 {html.escape(t("points"))}</p></div>'
    elif state == 'invalid':
        path = [0, 1, 4]
        kw = dict(states={i: 'invalid' for i in path[:1]} | {i: 'inpath' for i in path[1:]}, pips={0: 'x'}, orders={0: 1, 1: 2, 4: 3})
        msg = f'<div dir="ltr" style="text-align:start"><h4 class="heading" style="font-size:30px">ENA</h4><p class="caption" style="margin:0">{html.escape(t("Not a word yet. Try again."))}</p></div>'
    elif state == 'hint':
        kw = dict(states={5: 'start', 4: 'hint'})
        msg = f'<div><p class="caption" style="margin:0">{html.escape(t("The glowing tile is where a word starts."))}</p></div>'
    rows = ''.join(wordrow(w, p, tr_, i) for i, (w, p, tr_) in enumerate(words))
    if not words:
        rows = f'<div style="display:grid;place-items:center;padding:36px 12px;gap:10px">{gem(2, C["food"], 5)}<span class="caption">{html.escape(t("Your words will land here."))}</span></div>'
    total = sum(p for _, p, _ in words)
    panel = (f'<div style="width:410px;padding:20px 20px 20px 4px;display:flex;flex-direction:column;gap:14px"><div class="panel" style="flex:1;min-height:0;display:flex;flex-direction:column">'
             f'<div style="padding:14px 16px;border-bottom:3px solid var(--outline)"><h3 class="heading" style="font-size:22px">{html.escape(t("Words I found"))}</h3><div style="display:flex;justify-content:space-between;margin-top:6px"><span class="caption">{len(words)} {html.escape(t("words"))}</span><span class="points" style="font-size:16px">{total:06d}</span></div></div>'
             f'<div class="words" style="overflow:hidden">{rows}</div></div>{msg}'
             f'{btn(t("Show me a start"), "child", "hint")}{btn(t("Finish board"), "")}'
             f'<div style="display:flex;gap:10px">{btn(t("Clear"), "child", None, "style=\"flex:1\"")}{btn(t("Done"), "p child", "check", "style=\"flex:1.4\"")}</div></div>')
    stage = f'<div style="flex:1;display:grid;place-items:center">{board(L, 3, tile, **kw)}</div>'
    return (appbar(t('Find a word'), True, btn(t('For grown-ups'), '', 'person'), lang) + f'<div style="display:flex;flex:1;min-height:0">{stage}{panel}</div>')


@screen('solo-playing', 'Solo Play: a word found', 'Valid tiles turn leaf green and carry a check; the word joins the list with its gem.')
def s_found(lang='en', **kw): return playing(lang, 'found')


@screen('solo-playing-empty', 'Solo Play: a new board', 'The empty word list shows one gem and a line of text.')
def s_empty(lang='en', **kw): return playing(lang, 'empty')


@screen('solo-playing-path', 'Solo Play: tracing a word', 'Tiles in the path turn coral, sink 3 px and carry an order number; the stepped path joins them.')
def s_path(lang='en', **kw): return playing(lang, 'path')


@screen('solo-playing-invalid', 'Solo Play: not a word', 'An invalid first tile shows a pink outline and an x mark; text never sits on pink.')
def s_invalid(lang='en', **kw): return playing(lang, 'invalid')


@screen('solo-playing-hint', 'Solo Play: the start-tile help', 'The start tile has a dashed ring and a play mark; the second help tile is sunshine with a dot.')
def s_hint(lang='en', **kw): return playing(lang, 'hint')


@screen('solo-playing-night', 'Solo Play in Toybox Night', 'The dark scheme: dark purple page, cream outlines, sunshine primary buttons. The dice are unchanged.', theme='night')
def s_night(lang='en', **kw): return playing(lang, 'found')


@screen('solo-playing-he', 'Solo Play, Hebrew (right to left)', 'Chrome mirrors and the panel moves to the left; the board never mirrors.', lang='he', rtl=True)
def s_he(lang='he', **kw):
    return playing('he', 'found')


@screen('solo-playing-tablet', 'Solo Play on a tablet in portrait', 'Board above, panel below. 834 x 1112 points (an iPad in portrait).', w=834, h=1112)
def s_tablet(lang='en', **kw):
    t = lambda s: tr(lang, s)
    path = [4, 7, 8]
    words = [('sit', 13, 1), ('tap', 15, 2)]
    rows = ''.join(wordrow(w, p, tr_, i) for i, (w, p, tr_) in enumerate(words))
    return (appbar(t('Find a word'), True, btn(t('For grown-ups'), '', 'person'), lang)
            + f'<div style="display:grid;place-items:center;padding:34px 0 30px">{board(LETTERS, 3, 190, states={i: "valid" for i in path}, pips={8: "ok"})}</div>'
              f'<div style="display:flex;gap:20px;padding:0 24px;flex:1;min-height:0"><div class="panel" style="flex:1.2;display:flex;flex-direction:column"><div style="padding:14px 16px;border-bottom:3px solid var(--outline)"><h3 class="heading" style="font-size:22px">{html.escape(t("Words I found"))}</h3><div style="display:flex;justify-content:space-between;margin-top:6px"><span class="caption">2 {html.escape(t("words"))}</span><span class="points" style="font-size:16px">000028</span></div></div><div class="words">{rows}</div></div>'
              f'<div style="flex:1;display:flex;flex-direction:column;gap:12px"><div><h4 class="heading" style="font-size:30px">TAP!</h4><p class="caption" style="margin:0">+15</p></div>{btn(t("Show me a start"), "child", "hint")}{btn(t("Finish board"), "")}<div style="display:flex;gap:10px">{btn(t("Clear"), "child", None, "style=\"flex:1\"")}{btn(t("Done"), "p child", "check", "style=\"flex:1.4\"")}</div></div></div><div style="height:24px"></div>')


@screen('summary', 'Summary card', 'A dialog over the dimmed board: stars, points, the words, and two buttons.')
def summary(lang='en', **kw):
    t = lambda s: tr(lang, s)
    base = playing(lang, 'found')
    ws = ['sit', 'sat', 'tap', 'pat', 'nip']
    rows = ''.join(wordrow(w, p, 1 if p < 14 else 2, i) for i, (w, p) in enumerate(zip(ws, [13, 13, 15, 15, 15])))
    dlg = (f'<div class="dialog" style="position:absolute;z-index:11;left:50%;top:50%;transform:translate(-50%,-50%);width:560px;padding:24px;display:flex;flex-direction:column;gap:14px">'
           f'<h3 class="heading" style="font-size:30px">{html.escape(t("You found all 5 words!"))}</h3>'
           f'<div style="display:flex;align-items:center;justify-content:space-between"><span style="display:flex;align-items:center;gap:12px" class="bob">{gem(3, C["background"], 5)}<span class="heading" style="font-size:30px">5 {html.escape(t("stars"))}</span></span><span class="points" style="font-size:32px">000071</span></div>'
           f'<div class="panel" style="box-shadow:0 4px 0 var(--hard);border-width:3px"><div class="words" style="max-height:200px;overflow:hidden">{rows}</div></div>'
           f'<div style="display:flex;gap:12px">{btn(t("Play again"), "p child", "retry", "style=\"flex:1.3\"")}{btn(t("Home"), "child", None, "style=\"flex:1\"")}</div></div>')
    return f'{base}<div class="scrim"></div>{dlg}'


@screen('my-jewels', 'My jewels', 'The child\'s collection: totals, the best board for each size, the week, and earlier boards.')
def jewels(lang='en', **kw):
    t = lambda s: tr(lang, s)
    stats = ''.join(f'<div class="panel stat"><span class="eyebrow">{html.escape(t(a))}</span><div class="n">{n}</div><span class="caption">{html.escape(t(b))}</span></div>'
                    for a, n, b in [('Stars', '000123', 'all boards'), ('Points', '001840', 'all boards'), ('Words', '000086', 'found so far')])
    week = ''.join(f'<div style="display:flex;flex-direction:column;align-items:center;gap:6px"><div style="height:{h}px;width:34px;background:var(--coral);border:3px solid var(--outline);box-shadow:0 3px 0 var(--hard)"></div><span class="caption">{d}</span></div>'
                   for d, h in zip(['M', 'T', 'W', 'T', 'F', 'S', 'S'], [30, 80, 54, 110, 20, 0, 66]))
    gems = ''.join(f'<span style="display:inline-block;margin-right:8px">{gem(1 + i % 3, JEWEL[i % 5], 4)}</span>' for i in range(12))
    best = ''.join(f'<div class="panel" style="padding:14px 16px;flex:1;display:flex;align-items:center;gap:14px"><span class="points" style="font-size:30px">{n}</span><span style="display:flex;flex-direction:column"><b class="heading" style="font-size:20px;font-family:var(--reading)">{s}</b><span class="caption">{p}</span></span></div>'
                   for n, s, p in [('3x3', '5 words', '71 points'), ('4x4', '9 words', '142 points')])
    def thumb(letters):
        cells = ''.join(f'<i style="display:grid;place-items:center;width:28px;height:28px;background:var(--cream);border:2px solid var(--ink);border-radius:5px;font:700 16px var(--reading);font-style:normal">{c}</i>' for c in letters)
        return f'<div class="tray" style="padding:8px;border-width:4px;box-shadow:0 6px 0 var(--hard)"><div style="display:grid;grid-template-columns:repeat(3,28px);gap:4px">{cells}</div></div>'
    thumbs = ''.join(thumb(w) for w in ['ENSOAIMPT', 'CATORESDG', 'RABITLNOE', 'DOGSTEPAN'])
    return (appbar(t('My jewels'), True, '', lang)
            + f'<div style="padding:20px 32px;display:flex;flex-direction:column;gap:16px;flex:1;min-height:0"><div style="display:flex;gap:20px">{stats}</div>'
              f'<div style="display:flex;gap:20px;min-height:0"><div style="flex:1;min-width:0;display:flex;flex-direction:column;gap:12px"><h3 class="heading">{html.escape(t("Best board for each size"))}</h3><div style="display:flex;gap:16px">{best}</div>'
              f'<h3 class="heading" style="margin-top:8px">{html.escape(t("My jewels"))}</h3><div class="panel" style="padding:16px">{gems}</div>'
              f'<h3 class="heading" style="margin-top:8px">{html.escape(t("Earlier boards"))}</h3><div style="display:flex;gap:14px">{thumbs}</div></div>'
              f'<div class="panel" style="width:300px;flex:none;padding:16px;align-self:flex-start"><h3 class="heading" style="font-size:22px;margin-bottom:12px">{html.escape(t("This week"))}</h3><div style="display:flex;align-items:flex-end;justify-content:space-between;height:150px">{week}</div></div></div></div>')


@screen('parent-gate', 'Parent gate', 'One press-and-hold for two seconds; the pixel bar fills in 20 steps. A key hold works the same.')
def gate(lang='en', **kw):
    t = lambda s: tr(lang, s)
    prog = '<div class="bar-prog">' + ''.join(f'<i class="{"on" if i < 11 else ""}"></i>' for i in range(20)) + '</div>'
    return (appbar('', True, '', lang, mark=True)
            + f'<div style="flex:1;display:grid;place-items:center"><div class="dialog" style="width:560px;padding:28px;display:flex;flex-direction:column;gap:14px">'
              f'<h3 class="title">{html.escape(t("For grown-ups"))}</h3><p class="body" style="font-size:22px">{html.escape(t("Press and hold for two seconds."))}</p>'
              f'<p class="caption" style="margin:0">{html.escape(t("This keeps little fingers out of the settings and the progress."))}</p>'
              f'<div class="btn p primary pressed" style="flex-direction:column;gap:6px"><span>{html.escape(t("Hold to enter"))}</span><span class="caption" style="color:var(--cream)">{html.escape(t("Keep holding until the bar is full"))}</span></div>{prog}'
              f'<p class="caption" style="margin:0;text-align:center">{html.escape(t("With a keyboard, hold Space or Enter for two seconds."))}</p></div></div>')


def parent_shell(active, content, lang='en'):
    t = lambda s: tr(lang, s)
    tabs = ''.join(f'<span class="btn {"sel" if n == active else ""}">{html.escape(t(n))}</span>' for n in ['Dashboard', 'Settings', 'About', 'Licences'])
    bar = (f'<div class="bar"><span class="btn ghost" style="padding:0 8px">{icon("back")}{html.escape(t("Back"))}</span>'
           f'<span style="display:flex;align-items:center;gap:10px">{gem(2, C["accent"], 3)}<span class="wordmark" style="font-size:28px;text-shadow:2px 2px 0 var(--coral)">{html.escape(t("Word Jewels"))}</span></span>'
           f'<span class="caption">{html.escape(t("Parent area"))}</span><span class="grow"></span><div class="tabs">{tabs}</div></div>')
    return bar + f'<div style="flex:1;min-height:0;overflow:hidden;padding:24px 0"><div style="width:860px;margin:0 auto;display:flex;flex-direction:column;gap:18px">{content}</div></div>'


@screen('parent-dashboard', 'Parent area: dashboard', 'How the play is going, and the way in to the real-board mode.')
def dash(lang='en', **kw):
    t = lambda s: tr(lang, s)
    c = (f'<div class="panel" style="padding:18px 20px;display:flex;flex-direction:column;gap:10px"><h3 class="heading">{html.escape(t("Play with a real board"))}</h3><p class="body">{html.escape(t("Set the tablet beside the board. A grown-up checks the letters before the child plays."))}</p>'
         f'<div style="display:flex;gap:12px">{btn(t("Set up a real board"), "p")}{btn(t("Back to play"), "")}</div></div>'
         f'<span class="eyebrow">{html.escape(t("How the play is going"))}</span>'
         f'<div class="panel" style="padding:16px 20px"><p class="body">{html.escape(t("This week: 1 board cleared, 23 stars, 8 new words."))}</p><p class="body">{html.escape(t("The longest word: paint."))}</p></div>'
         f'<h3 class="heading">{html.escape(t("Sounds"))}</h3>'
         + ''.join(f'<div class="panel" style="padding:12px 16px;display:flex;align-items:center;gap:16px;box-shadow:0 4px 0 var(--hard)"><span class="heading" style="width:56px;text-align:center;font-family:var(--reading);font-style:italic">{s}</span><div style="flex:1"><p class="body" style="font-size:17px">Words with {s} have been on {n} boards. {n} were found.</p></div><span class="chip {"sun" if o else ""}">{o or "Not yet"}</span></div>' for s, n, o in [('a', 4, 'Often found'), ('ai', 5, 'Found'), ('t', 3, '')]))
    return parent_shell('Dashboard', c, lang)


@screen('parent-settings', 'Parent area: settings', 'Each row holds a title, a hint and a choice. The selected choice is coral with a check mark.')
def settings(lang='en', **kw):
    t = lambda s: tr(lang, s)

    def row(title, hint, opts, sel):
        return f'<div class="row"><div class="t"><b>{html.escape(t(title))}</b><span class="caption">{html.escape(t(hint))}</span></div>{seg([t(o) for o in opts], t(sel))}</div>'
    c = (f'<h3 class="title">{html.escape(t("Settings"))}</h3><span class="eyebrow">{html.escape(t("Play"))}</span><div class="panel">'
         + row('Help', 'The start tile glows to show where a word begins.', ['Off', 'A start tile', 'Two letters'], 'Two letters')
         + row('Board', 'Standard: dice and any word. Phonics: the words of a phase.', ['Standard', 'Phonics'], 'Standard')
         + row('Motion', 'System follows the device. Reduced keeps the box still.', ['System', 'Reduced'], 'System')
         + row('Colour scheme', 'Day or night.', ['Light', 'Dark', 'System'], 'System')
         + row('Language', 'The words on the board stay English.', ['English', '中文', 'עברית'], 'English')
         + row('Text size', 'Makes the words larger.', ['100 %', '125 %', '150 %'], '100 %') + '</div>')
    return parent_shell('Settings', c, lang)


@screen('parent-about', 'Parent area: About', 'Version, licence, source, third-party notices and the privacy statement in one panel.')
def about(lang='en', **kw):
    t = lambda s: tr(lang, s)
    build = 'Word Jewels Solver 0.1.0\n  release: v0.1.0-rc.1\n  commit:  fa1159b892\n  branch:  develop\n  target:  Windows x86_64, MSVC 19.51'
    c = (f'<div style="display:flex;align-items:center;gap:16px">{gem(3, C["background"], 5)}<div><h3 class="title">{html.escape(t("Word Jewels Solver"))}</h3><p class="body">{html.escape(t("A word-finding game for young readers, played with a grown-up."))}</p></div></div>'
         f'<div class="panel" style="padding:16px 20px;display:flex;flex-direction:column;gap:12px"><span class="eyebrow">{html.escape(t("Version"))}</span><div class="mono">{html.escape(build)}</div></div>'
         f'<div class="panel" style="padding:16px 20px;display:flex;flex-direction:column;gap:10px"><span class="eyebrow">{html.escape(t("Licence and source"))}</span>'
         f'<p class="body">Source files: GPL-3.0-or-later. The game window links Qt Quick 3D, so a build of it is GPL-3.0 only.</p>'
         f'<p class="body">Source: github.com/iainchesworthlabs/wordjewelssolver, tag v0.1.0-rc.1, commit fa1159b892.</p>'
         f'<div style="display:flex;gap:12px">{btn(t("Licences"), "")}{btn(t("Open the source page"), "")}</div></div>'
         f'<div class="panel" style="padding:16px 20px"><span class="eyebrow">{html.escape(t("Privacy"))}</span><p class="body"><b>{html.escape(t("Kept on this device. No recording, no network."))}</b></p><p class="body">{html.escape(t("Nothing moves between devices. A new device starts fresh."))}</p></div>')
    return parent_shell('About', c, lang)


@screen('scan-setup', 'Real board: set up', 'A three-step stepper, the camera frame with corner marks, and the typed-board way in.')
def scan(lang='en', **kw):
    t = lambda s: tr(lang, s)
    steps = ''.join(f'<span style="display:flex;align-items:center;gap:8px"><span class="pip {"ink" if i == 1 else ""}" style="position:static;width:30px;height:30px;font-size:16px">{i}</span><span class="label" style="{"font-weight:700" if i == 1 else ""}">{html.escape(t(n))}</span></span>' + ('<i style="width:36px;height:0;border-top:4px dashed var(--outline);display:block"></i>' if i < 3 else '')
                    for i, n in enumerate(['Place', 'Check', 'Play'], 1))
    corners = ''.join(f'<i style="position:absolute;{p};width:44px;height:44px;border:5px solid var(--cream);{b}"></i>' for p, b in [('left:16px;top:16px', 'border-right:0;border-bottom:0'), ('right:16px;top:16px', 'border-left:0;border-bottom:0'), ('left:16px;bottom:16px', 'border-right:0;border-top:0'), ('right:16px;bottom:16px', 'border-left:0;border-top:0')])
    return (appbar(t('Set up a board'), True, '', lang)
            + f'<div style="flex:1;display:flex;flex-direction:column;align-items:center;gap:14px;padding:20px 0"><div style="display:flex;align-items:center;gap:12px">{steps}</div><h3 class="heading">{html.escape(t("Put the board in the frame"))}</h3>'
              f'<div style="position:relative;width:560px;height:330px;background:var(--ink);border:6px solid var(--outline);border-radius:12px;box-shadow:0 10px 0 var(--shadow);display:grid;place-items:center;color:var(--cream)">{corners}<div style="display:flex;flex-direction:column;align-items:center;gap:12px">{icon("camera", cell=8)}<b class="heading">{html.escape(t("No camera"))}</b></div></div>'
              f'<p class="body" style="font-size:16px;max-width:620px;text-align:center">{html.escape(t("No camera was found. If this device has one, it may be covered or in use by another app. You can type in your board."))}</p>'
              f'<div style="display:flex;gap:12px">{btn(t("Try camera again"), "", "retry")}</div>{btn(t("Type in your board"), "p child", None, "style=\"width:560px\"")}</div>')


@screen('scan-check', 'Real board: check the letters', 'Doubtful tiles carry a question-mark pip; the grown-up taps one to change the letter.')
def scan_check(lang='en', **kw):
    t = lambda s: tr(lang, s)
    side = (f'<div style="width:410px;padding:20px 20px 20px 4px;display:flex;flex-direction:column;gap:14px"><span class="eyebrow">{html.escape(t("Board size"))}</span>{seg(["2x2", "3x3", "4x4", "5x5", "6x6"], "2x2", "num")}'
            f'{btn(t("Turn"), "ghost", "retry")}<p class="body" style="font-size:16px;margin-top:auto">{html.escape(t("A ringed tile may be wrong. Tap it to change the letter."))}</p>'
            f'<div style="display:flex;gap:10px">{btn(t("Scan again"), "child", None, "style=\"flex:1\"")}{btn(t("Start playing"), "off child", None, "style=\"flex:1.3\"")}</div></div>')
    return (appbar(t('Check the board'), True, '', lang)
            + f'<div style="display:flex;flex:1;min-height:0"><div style="flex:1;display:grid;place-items:center">{board(["C", "A", "", "T"], 2, 230, pips={1: "q", 2: "q", 3: "q"}, states={2: "dis"})}</div>{side}</div>')


@screen('assist', 'Real board: the assistant', 'The confirmed board with the words found so far; the grown-up plays, the child gets hints.')
def assist(lang='en', **kw):
    return playing(lang, 'hint')



@screen('home-de', 'Home in German', 'German has the longest strings of the eight languages; the cards and the bar still fit.', lang='de')
def home_de(lang='de', **kw):
    return home(lang='de')


@screen('home-text150', 'Home at 150 % text', 'The parent\'s Text size at its largest: reading text grows, chrome keeps its pixel sizes.', scale=1.5)
def home_150(lang='en', **kw):
    return home(lang='en')


@screen('settings-de', 'Settings in German', 'Long row titles and hints wrap beside the choices.', lang='de')
def settings_de(lang='de', **kw):
    return settings(lang='de')


@screen('tile-states', 'Tile states', 'Every state differs in shape or position as well as colour.', w=1160, h=640)
def tile_states(lang='en', **kw):
    items = [('normal', '', None, None, 'Normal'), ('pressed / in path', 'inpath', 2, None, 'In path, with order'), ('start tile', 'start', None, None, 'Start tile (help)'),
             ('hint tile', 'hint', None, None, 'Second help tile'), ('valid word', 'valid', None, 'ok', 'Valid'), ('invalid', 'invalid', None, 'x', 'Not a word'),
             ('doubtful', '', None, 'q', 'Doubtful (scan)'), ('disabled', 'dis', None, None, 'Disabled')]
    cells = ''.join(f'<div style="display:flex;flex-direction:column;align-items:center;gap:20px"><div class="cell" style="--tile:96px;background:var(--teal-l)">{die("A", s, o, p)}</div><span class="label" style="text-align:center;width:112px;font-size:15px">{html.escape(lab)}</span></div>'
                    for _, s, o, p, lab in items)
    return (f'<div style="padding:28px 36px;display:flex;flex-direction:column;gap:20px"><h2 class="title">Tile states</h2>'
            f'<div class="tray" style="padding:34px 30px 24px;--tile:96px"><div style="display:flex;gap:24px;color:var(--cream);align-items:flex-start">{cells}</div></div>'
            f'<p class="body" style="max-width:900px">Letters are Andika Bold. Ink on coral 5.32:1, on leaf 7.69:1, on sunshine 9.76:1, on cream 13.43:1. Pink carries no text (ink on pink is 4.16:1), so the invalid state uses a pink outline, an x mark and a sideways shake.</p></div>')


@screen('gems', 'Gems', 'Three tiers by shape (1, 2 and 5 stars) and five palette colours; colour is decoration.', w=1160, h=520)
def gems(lang='en', **kw):
    rows = ''.join(f'<div style="display:flex;align-items:center;gap:22px"><span class="label" style="width:150px">{n} ({s})</span>{"".join(gem(tier, c, cell) for c in JEWEL)}</div>'
                   for tier, n, s, cell in [(1, 'Tier 1', '1 star', 6), (2, 'Tier 2', '2 stars', 6), (3, 'Tier 3', '5 stars', 5)])
    return (f'<div style="padding:28px 36px;display:flex;flex-direction:column;gap:28px"><h2 class="title">Gems</h2><div class="panel" style="padding:24px;display:flex;flex-direction:column;gap:26px">{rows}</div>'
            f'<div style="display:flex;gap:24px;align-items:center"><span class="label">In the list</span>{gem(1, JEWEL[0], 3)}{gem(2, JEWEL[1], 3)}{gem(3, JEWEL[2], 3)}<span class="label" style="margin-left:24px">Mark</span>{icon("check", cell=4)}{icon("x", cell=4)}{icon("play", cell=4)}{icon("person", cell=4)}{icon("camera", cell=3)}{icon("hint", cell=4)}</div></div>')


@screen('components', 'Components', 'Buttons in the three sizes and their states, choices, chips, panel and the gate\'s progress bar.', w=1160, h=820)
def comps(lang='en', **kw):
    prog = '<div class="bar-prog">' + ''.join(f'<i class="{"on" if i < 12 else ""}"></i>' for i in range(20)) + '</div>'
    return (f'<div style="padding:28px 36px;display:flex;flex-direction:column;gap:22px"><h2 class="title">Components</h2>'
            f'<div style="display:flex;gap:14px;flex-wrap:wrap;align-items:center">{btn("Open", "")}{btn("Compact", "ghost")}{btn("Show me a start", "child", "hint")}{btn("Done", "p child", "check")}{btn("Disabled", "off child")}{btn("Reset everything", "danger", "x")}{btn("Focused", "child focus")}{btn("Pressed", "p child pressed")}</div>'
            f'<div style="display:flex;gap:14px;align-items:center">{btn("Shake", "p primary", "shake", "style=\"width:360px\"")}<span class="caption">48 / 64 / 96 px targets</span></div>'
            f'<div style="display:flex;gap:32px"><div class="panel" style="padding:18px;flex:1;display:flex;flex-direction:column;gap:14px"><h3 class="heading">Choice</h3>{seg(["Off", "A start tile", "Two letters"], "A start tile")}{seg(["3", "4"], "3", "child num")}</div>'
            f'<div class="panel" style="padding:18px;flex:1;display:flex;flex-direction:column;gap:14px"><h3 class="heading">Chips and counters</h3><div style="display:flex;gap:10px"><span class="chip sun">Often found</span><span class="chip">Found</span><span class="chip">000071</span></div>{prog}</div></div>'
            f'<div class="dialog" style="padding:18px 22px;width:560px"><h3 class="heading">Dialog</h3><p class="body">Four pixel outline, 20 px radius, 6 px hard drop. The scrim is ink at 80 percent.</p></div></div>')


# ---------------------------------------------------------------- build
def main():
    (HERE / 'toybox.css').write_text(css())
    (HERE / 'screens').mkdir(exist_ok=True)
    cards = []
    for file, title, desc, w, h, kw, fn in SCREENS:
        lang = kw.get('lang', 'en')
        out = fn(lang=lang)
        theme = kw.get('theme', 'day')
        rtl = kw.get('rtl', False)
        (HERE / 'screens' / f'{file}.html').write_text(page(file, title, out, w, h, theme, lang, rtl, kw.get('scale', 1.0), desc))
        cards.append(f'<a class="card" href="screens/{file}.html"><img src="screenshots/{file}.png" alt="{html.escape(title)}" loading="lazy"><b>{html.escape(title)}</b><span>{html.escape(desc)}</span></a>')
    index = f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Word Jewels Solver: Toybox Pixel mockups</title><link rel="stylesheet" href="fonts/fonts.css">
<style>body{{margin:0;background:#26232E;color:#EDEAF5;font:16px/1.5 system-ui,sans-serif}}main{{max-width:1280px;margin:0 auto;padding:32px}}
h1{{font:700 34px 'Pixelify Sans',monospace;margin:0 0 6px}}p{{margin:0 0 24px;max-width:70ch}}.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(360px,1fr));gap:20px}}
.card{{display:flex;flex-direction:column;gap:6px;background:#312D3D;border-radius:10px;padding:12px;color:inherit;text-decoration:none}}.card:hover{{background:#3B3649}}.card img{{width:100%;height:auto;border-radius:6px;background:#111}}
.card b{{font-weight:600}}.card span{{font-size:14px;opacity:.8}}</style></head><body><main>
<h1>Word Jewels Solver: Toybox Pixel</h1>
<p>Design mockups for the restyle. They are standalone HTML driven by <code>tokens.json</code> (Snake, unchanged) and <code>wjs-tokens.json</code> (the additions). Not production code. Click a card for the live page.</p>
<div class="grid">{''.join(cards)}</div></main></body></html>"""
    (HERE / 'gallery.html').write_text(index)
    print(len(SCREENS), 'screens')
    for file, title, desc, w, h, kw, fn in SCREENS:
        print(f'{file} {w} {h}')


if __name__ == '__main__':
    main()
