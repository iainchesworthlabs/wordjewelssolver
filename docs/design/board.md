# Board design

The board: its sizes, how its letters are chosen, how it is laid out on each
kind of screen, how it looks, how it is shaken and dropped, and what drawing it
costs in licence terms. Written from the
[product requirements](../product-requirements.md) sections 3, 5, 7 and 12.
Proposals unless marked **Fact**. What the first release (the MVP, section 12)
takes from this page is marked **MVP**; the rest is the full product.

## Sizes

| Size | Dice | Faces | Mode B, Solo Play | Mode A, the scanner | Words of 3+ letters on a board, any dictionary word |
| --- | --- | --- | --- | --- | --- |
| 3x3 | 9 | 54 | **MVP**: the first board | No | 31 (10th to 90th percentile 9 to 59) |
| 4x4 | 16 | 96 | **MVP**: the default | **MVP**: the physical board | 120 (52 to 196) |
| 5x5 | 25 | 150 | After the MVP, where a tile fits | After the MVP | 254 (134 to 404) |
| 6x6 | 36 | 216 | After the MVP, where a tile fits | After the MVP | 435 (262 to 617) |

**Fact, measured:** the last column is from 200 boards a size, letters drawn at
random in proportion to their counts in ENABLE, solved with the repository's
library. It is a test corpus, not a dice set. It is the number of words
*anywhere in the dictionary*, which the MVP does not use as its word set (see
[Game design](game.md#words-and-what-each-does)). **Fact:** the library takes any
board from 1x1 to 16x16 and a tile of one to three letters.

Notes.

- The minimum word length is 3 on every size.
- A tile is one to three letters. The `Qu` face is one tile of two letters.
  Some larger sets carry other pairs (`Th`, `In`); this is **unverified**,
  read from a search summary of the Wikipedia article, and the library allows
  it. The real 6x6 game's four-letter minimum and four-minute timer are from the
  same summary and are the family's game, not the child's list.
- The 3x3 is the same game on nine cells and is **not** a window onto a 4x4. In
  the scanner there is no 3x3: a real board is 4x4 or larger.

## Where the letters come from

There are two different problems, and the MVP has only the first.

### The phase board (MVP)

In Mode B the letters are **seeded**, not shaken from dice: a board is made so
that it holds three to five words of the active phonics phase and so is quickly
cleared (section 12.3). The method, the measurements behind it and the
alternatives are on [Phonics](phonics.md#seeding). What this page needs from it:

- A board is nine or sixteen cells. Some cells may be **dead jewels**: a tile
  that is drawn as a jewel, has no letter, and cannot be part of a word. They
  hold the count of viable words down without showing the child letters the
  phase has not taught. To the library a dead jewel is a letter that no word of
  the phase uses (the app maps it; the library is unchanged).
- The roll is a pure function of the phase data and a seed, in the library, so a
  board can be replayed and tested.
- The animation is given a finished board (below).

### Dice (the standard board, in the MVP: decided 2026-10-16)

The standard board, the MVP's default, comes from dice, as in the physical game.
The author chose the third source below, the project's own dice, and
`tools/data/make_dice.ps1` and `data/dice` are built. The faces of a set the
author owns are for the scanner mode, where a physical board is on the table and
the app reads it with OCR and does not mix letters.

| Source | What it is | Standing |
| --- | --- | --- |
| The printed faces of a set the author owns | Facts read off an object | Whether a set's letters, as a selection, can belong to someone is a legal question and is **not answered here** ([L]) |
| A list found online | Unknown origin | Not usable. `data/README.md` says "Do not add a list whose origin you cannot state" |
| Made by this project from letter counts | The project's own work, from the list the repository already records as public domain | Clean |

The third is the recommendation. A tool in `tools/data` would count the
letters of `data/dictionaries/en.txt`, deal 6 faces to each die in proportion
to the counts (no die repeating a letter, a vowel on each), check the dice by
rolling them 1,000 times (seeded) against the solver, and write
`data/dice/<n>x<n>.txt` with the source, its SHA-256 and the seed in the header,
as `data/README.md` requires. Nothing here gives a distribution of letters, and
the numbers inside that tool are outputs of its tuning, not inputs. In the
scanner the real dice are read from the table, so no product's distribution is
needed at all. Boggle is another company's name: public text should say "a
word-finding game" ([L]).

**Fact, measured, so the open dictionary can be designed later.** Share of
boards a floor of K known words would reject, on the test corpus:

| Word list | Size | K=1 | K=3 | K=5 | K=8 |
| --- | --- | --- | --- | --- | --- |
| Core and ten categories (689) | 3x3 | 13% | 47% | 73% | 91% |
| | 4x4 | 0% | 3% | 14% | 38% |
| | 5x5 | 0% | 0% | 0% | 6% |
| | 6x6 | 0% | 0% | 0% | 0% |
| Core tier only (237) | 3x3 | 29% | 72% | 91% | 99% |
| | 4x4 | 2% | 23% | 55% | 82% |
| | 5x5 | 0% | 3% | 16% | 48% |
| | 6x6 | 0% | 0% | 0% | 14% |

## Layouts and sizes

### The rule

The window is split by its shape, not by what device it is. **MVP**: this is
the brief's two-thirds / one-third split, everywhere.

- **Wide** (width at least height): the board is in the leading two-thirds of
  the width, the panel in the trailing third, both full height.
- **Tall**: the board is in the top two-thirds of the height, the panel in the
  bottom third, both full width.

The board is the largest square that fits its area less an inset, drawn
centred. The inset is 12 dp when the area's shorter side is under 600 dp, 24 dp
from 600 dp up. The gap between tiles is 6 dp, or 8 dp from 600 dp up. These
are this app's own numbers. Cubeulator's page margins are 24 dp (16 under 360),
32 and 40 by width class, and it singles out a "short" window (height under
480) for a phone on its side; here the inset is chosen from the board area, so
a phone on its side gets the same board as one held upright.

```text
tile = (S - (n - 1) * gap) / n        S = the shorter side of the area - 2 * inset
```

Rotating keeps the game, the path being drawn and the tray's scroll position.
Only the geometry is recomputed.

### Reference windows

**Estimate.** Logical sizes of typical devices, from memory; the author's own
devices have to replace them (the MVP is an iPad; its model is not known to
this repository). dp is Android's unit and about the same as an iOS point.

| Window | Shape | Board area | Panel | 3x3 | 4x4 | 5x5 | 6x6 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Phone 360 x 740 | Tall | 360 x 493 | 360 x 247 | 108 | 80 | 62 | 51 |
| Phone 390 x 800 | Tall | 390 x 533 | 390 x 267 | 118 | 87 | 68 | 56 |
| Phone 320 x 600 | Tall | 320 x 400 | 320 x 200 | 95 | 70 | 54 | **44** |
| Phone on its side 740 x 360 | Wide | 493 x 360 | 247 x 360 | 108 | 80 | 62 | 51 |
| Small tablet 744 x 1133 | Tall | 744 x 755 | 744 x 378 | 227 | 168 | 133 | 109 |
| Tablet 800 x 1280 | Tall | 800 x 853 | 800 x 427 | 245 | 182 | 144 | 119 |
| Tablet 1280 x 800 | Wide | 853 x 800 | 427 x 800 | 245 | 182 | 144 | 119 |
| Desktop window 1100 x 700 | Wide | 733 x 700 | 367 x 700 | 212 | 157 | 124 | 102 |
| Narrow browser 420 x 800 | Tall | 420 x 533 | 420 x 267 | 128 | 95 | 74 | 61 |

[layouts.svg](layouts.svg) draws four of these to scale. On an iPad the MVP's
3x3 and 4x4 are 160 dp or more, which is large for a finger.

### Which sizes are offered

A size is offered only if its tile is at least **48 dp**, Cubeulator's floor for
any target. A size that does not fit is not shown, not shown disabled (the
Cubeulator rule: "if nothing the person can do would enable it, it is not
shown"). 56 dp and over is comfortable; 48 to 55 relies on the tracing
tolerance. In the scanner the real board decides the size, so a replica smaller
than 48 dp is still shown, for reading and for the highlighted tile.

### The panel at each size

| Panel | Where | Words I found | Hints | History |
| --- | --- | --- | --- | --- |
| 300 dp or more on both sides | Tablets, desktop | A tray, 3 or more rows | A button and a display | Three thumbnails inline |
| Anything smaller | Phones | A tray in the space left | One button, 64 dp | One button that opens a sheet |

## The tile

| Part | Rule |
| --- | --- |
| Shape | A square with square corners. Cubeulator's rule: flat, no shadows, no gradients, regions ruled, "the cube is the colour". The dice are plain and the jewels are the colour. In the 3D scene (below) the dice are lit solids and the faces keep this look |
| Letter | Upper case, centred, upright, about 55% of the tile's height (a starting value); `Qu` at 80% of that so it fits. The real board is upper case, so the replica matches it. The typeface and the school-font setting are on [Application design](application.md#type) |
| Rest | Cream face, dark letter, dark edge |
| Lit | On the path: the face turns amber, the edge thickens to 4 dp, and the tile scales to 106%. Not colour alone |
| Grapheme link | Tiles that make one sound share a bar under them and a common edge colour; a split digraph is joined by an arc over the tile between. Shape first, colour second |
| Found | The path's tiles flash once in the order they were traced, 60 ms apart |
| Start tile | A pulsing ring: shape and motion, not colour alone. **MVP**: the only help |
| Dead jewel | A faceted shape with no letter, not part of any path; a touch on it does nothing |
| Doubtful | Scanner check only: a ring and a `?` |

## The shake and the drop

The brief expects "a 3D dice shake in a virtual 3D container", with the
accelerometer as an optional way to shake it, haptics and an audio cue (section
3), and "the same style as the Rubik's cube in Cubeulator" (section 7).

### The stages

All numbers are **starting values to tune on a device**. The 120 ms and 380 ms
are Cubeulator's own motion tokens (`quick`, `turn`).

| Stage | Full motion | Reduced motion |
| --- | --- | --- |
| Shake | The box rattles through about 8 degrees, 0.7 s at least and while the device shakes, 3 s at most. A looped rattle; haptic taps, no more than 8 a second | The box is still; one soft rattle, 0.3 s |
| Lid | Lifts and tilts away, 250 ms | Cross-fades, 120 ms |
| Drop | Each die falls from one tile above, 280 to 420 ms, started in a fixed pseudo-random order. The gap between starts is the smaller of 30 ms and 700 ms divided by the number of dice. A small overshoot on landing. One click each, pitched a little differently | All dice fade in together, 120 ms |
| Settle | Letters straighten, 150 ms | None |
| Layout | The board slides to the leading two-thirds and the panel slides in, 380 ms | Cross-fade, 120 ms |
| Total after the shake | About 1.7 s (4x4) and 1.9 s (6x6) | Under 0.6 s |

### No physics engine

The board is decided by the library first. So:

- A physics engine would decide where each die lands, and the game would have to
  read the board back from the simulation. The game already has the board, and
  in Mode B it is a *seeded* board that must hold three to five words.
- A shake would not repeat. A seeded script replays exactly, which is what a
  test, a recording and a "play this board again" need.
- The rattle can still answer the device. The accelerometer drives the box's
  tilt and the dice's sliding in the box with a few lines of integration of our
  own: this is the "fun" of the brief and it does not decide anything.
- Qt Quick 3D Physics, the engine in Qt's own set, is GPL-only like Qt Quick 3D
  (below) and is not in the shared tree.

### Rendered 3D, or drawn in 2D

| | A: Qt Quick 3D | B: 2D Qt Quick, scripted |
| --- | --- | --- |
| Look | Lit, glossy dice in a box: the brief's "3D container" and Cubeulator's cube | Flat tiles with a faceted edge, tilted by transforms |
| Licence the build can carry | GPL-3.0 only | GPL-3.0-or-later, Qt under the LGPL or GPL |
| Private builds and GPL downloads | Unaffected | Unaffected |
| Apple's store | Open, and the binary can no longer be "or later" | Open, as it is for the project's own GPL |
| Roadmap D2 | Settled toward GPL-3.0 for any build that links it | Stays open |
| Cost | Cubeulator measured about 140 ms to create each of its 3D cube views, and 10 to 16 s of light-probe work on a software renderer before it baked the probe in advance (its `docs/performance.md`) | Small |

**Fact, checked in the Qt 6.12.0 SBOMs of the Windows kit in the shared tree
(`C:\Repos\common\qt\6.12.0\msvc2022_64\sbom`):** `Quick3D` and
`Quick3DParticles` are `LicenseRef-Qt-Commercial OR GPL-3.0-only`; `Quick`,
`QuickShapes`, `QuickParticles` and `QuickEffects` are `LicenseRef-Qt-Commercial
OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. **Read, from Cubeulator's
licensing paper, not re-checked:** Qt Quick 3D Physics is on the same GPL-only
list; Shader Tools' runtime library has the LGPL option; a binary that links
Quick 3D can be conveyed only under GPL-3.0, so "or later" stops being true of
it though the source files stay "or later"; and leaving Quick 3D out does not
cure the Apple store question, because the LGPL has its own conflict with
stores.

**Decided (the author, 2026-10-16): A for the MVP, behind a seam, with the choice
taken at D2.** The MVP is a private ad-hoc build on the author's iPad, which the
roadmap says no licence rule touches. The author expects 3D and a lit solid is
what Cubeulator has. The cost comes only at a store submission, so:

1. The board scene is one component with a narrow interface: *given a finished
   board and a seed, play the shake and the drop, and report when the dice are
   settled*. Nothing else in the application touches the scene; the game rules
   stay in the library.
2. The scene is written in Qt Quick 3D first. A 2D implementation of the same
   interface is the fallback if D2 goes the other way, and the tile, the path and
   the panel are 2D in both.
3. Before any store submission D2 is decided with the interface in hand, and the
   cost of B is a rewrite of one component, not of the game.

What this commits the MVP to: Qt Quick 3D and Shader Tools are linked, so the MVP
binary is GPL-3.0 only, which a private build does not mind and a store
submission would. It does not decide D2: that is taken before any store, with
the seam in hand. Two more modules the product needs are not in the shared tree
at all; see [Application design](application.md#qt-modules).

### How it is drawn

- **3D (A).** One `View3D`; each die a `Model` sharing one box mesh and a
  letter texture atlas; a box and a lid; a light probe **baked in advance**, as
  Cubeulator did (its `tools/lightprobe`, 128 x 128 texels a face, 1 MB), so Qt
  does no prefiltering at run time. No materials beyond a plain lit one. The
  view is created once and kept, not created for each screen (Cubeulator
  measured 140 ms to create each of its views).
- **2D (B).** A die is two items (a rectangle and a text); a shadow is a flat
  ellipse that grows as the die falls. Only `x`, `y`, `rotation`, `scale` and
  `opacity` are animated. No shaders, no effect layers, no blur. At most 36 dice
  are alive at once.

Confetti and stars are a 2D particle emitter (Qt Quick Particles, LGPL set) laid
over the view. **Reduced motion** removes it, and its flash rate is at most
three a second (see [Application design](application.md#motion)).

## Unknowns

- Whether the tumble reads well at a child's eye height, in 3D or in 2D: the
  cheapest thing to prototype and the first thing to show. The 3D scene was
  prototyped and measured on one desktop, not on a device; see
  [Shake prototype](shake-prototype.md).
- The tolerance for tracing on a 51 dp tile with a small finger. On an iPad the
  tiles are large and this matters on phones.
- Whether dead jewels are acceptable to the literacy reviewer, or whether a
  board with untaught letters is what a classroom would expect. It is a
  question for the review (G7).
- Whether Qt's 3D scene runs well on the author's iPad. Nothing was measured on
  a device, and the shared tree has no iOS kit.
