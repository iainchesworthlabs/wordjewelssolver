# Design

The board design, the game design and the application design for
WordJewelsSolver, worked from the [product requirements](../product-requirements.md)
including its MVP scope (section 12). Written 2026-10-16.

These pages are **proposals** for the author to accept or change. Nothing here
is decided. Each says what is proposed, why, what it rests on, and what would
change it. No production code was written; the only programs were throw-away
measurements, which are not committed.

## How to read these pages

| Mark | Meaning |
| --- | --- |
| **Fact** | Checked in this repository, in a sibling project, or by a measurement named in the text |
| **Read** | Taken from a source's text (a header, a document, a search summary) and not exercised here |
| **Estimate** | A multiple or a guess, with its basis |
| **Unverified** | Not checked; said so that it is not taken as known |
| **MVP** | In the first release, section 12 of the brief |
| *Proposal* | Everything else: the default reading of this directory |

Where a page says "after the MVP" the feature is in the brief and is deferred,
not removed.

## The pages

| Page | What it settles |
| --- | --- |
| [Answers](answers.md) | All 69 questions of section 11, with a recommendation and who confirms |
| [Game](game.md) | Mode B (Solo Play) and Mode A (the scanner): flow, words, stars, help, the resting screen, the check screen |
| [Phonics](phonics.md) | The mechanism: phase data, seeding, grapheme blocks, "sound it out", the review gate. **No word list** |
| [Board](board.md) | Sizes, where letters come from, layout and scaling, the tile, the shake, the licence cost of Qt Quick 3D |
| [Shake prototype](shake-prototype.md) | The board scene built in Qt Quick 3D behind its seam, what it cost on one desktop, and what a 2D swap-in must keep |
| [Application](application.md) | Screens, touch targets, type and school fonts, colour, motion, accessibility, sound, privacy built in and checked, Qt modules |
| [Dashboard](dashboard.md) | The parent area: the gate, what is shown and how it is worded, settings, what is deferred |
| [Performance](performance.md) | Budgets, the Tracy zones, the macro-free route, the performance tier |
| [Path](path.md) | The route from the MVP to the full product across several releases, and what the MVP must leave open |
| [Layouts](layouts.svg) | The two-thirds / one-third arrangement at four window shapes, to scale |

## The design in one page

1. **Two modes, one board component.** Mode B (Solo Play) rolls a board and the
   child finds words; Mode A (the scanner) reads a real board and helps. They
   share the board, the solver, the tray and the help. Separately, there are two
   **board modes** (decided 2026-10-16): the **standard** board, letters on dice,
   is the default; the **phonics** board is a parent's switch.
2. **The MVP is small on purpose.** An iPad, 3x3 and 4x4, a standard board with
   the phonics board as the parent's switch, one help (the start tile), stars by
   tier, and a calm resting screen when the board is cleared. Nothing pulls the
   child back. (The brief's 12.4 reads as the phonics board alone; the author
   chose the standard board as the default.)
3. **A phonics board is made, not shaken.** The library **seeds** it: plants words
   of the active phase (three letters at most), fills the rest with a
   solver-guided fill that prefers the phase's letters and lets others in, and
   **verifies** with the solver that it holds three to five. Measured: blind draws
   land there about one time in four at best; the guided fill and short targets
   land 90% or more on a 4x4 and larger. A standard board is dice, not seeded.
4. **A traced word is one of three things**: viable (counts), real but not
   today's (a soft tick, no word shown), or not a word (the path fades). A real
   word is never called "not a word", and no unsuitable dictionary word is ever
   read or printed.
5. **Phonics is data.** Phases, graphemes and words are reviewed files the
   build refuses to ship unreviewed. The code binds a trace into blocks and
   sounds a word out; it knows no letter or sound.
6. **The shake is scripted, not simulated**, and the scene is one component
   with a narrow interface. 3D (Qt Quick 3D) for the MVP's private build,
   **decided by the author**, with the licence decision (GPL-3.0 only for a
   build that links it) taken at D2 before any store.
7. **Privacy is built in and checked in CI from the first build**: no audio
   input, no network capability, no export, a local store of a few kilobytes,
   no profiler in what ships.
8. **Touch is for small hands**: 64 dp for a child's tap and 96 dp for the main
   action, trace and tap-in-order both always on, a button for every shake.
9. **Solving is not the cost; loading is.** Worst board tried: 1.7 ms.
   Dictionary load: 55 to 65 ms and 18 MB. Budgets are exact work counts plus
   loose time ratios, as in Cubeulator.
10. **There is a path.** Six releases, each whole: more to find, bigger boards
    and more help, more devices, out of the house, and the deferred
    features each on its own review. The MVP leaves a short list of seams open
    so none is a rewrite.

## Questions for the author

**Decided.** The MVP's shake is 3D, in Qt Quick 3D, behind a one-component
seam, with the licence decision left at D2 before any store (the author,
2026-10-16; [Board](board.md#rendered-3d-or-drawn-in-2d)).

The rest, in the order they are best asked, each with the default this design
uses so nothing waits.

| # | Question | Default |
| --- | --- | --- |
| 1 | **Which are the lowest iPad, tablet and phone** to support, and on which operating system? | Budgets are written for "the lowest device" |
| 2 | **Who is the literacy reviewer, and who writes the Phase A index?** Needed before any audio is recorded and before a child uses it (question 69) | Not named; the mechanism is built first |
| 3 | **Are worlds, the mascot, the vault's art and school fonts in the MVP?** Section 12.5 does not say | Not in the MVP; R2 ([Path](path.md)) |
| 4 | **Should the app's data be excluded from device backups?** "Air-gapped" says yes; a lost iPad would then lose the progress | Undecided |
| 5 | **Which English?** Spelling, the voice's accent and the phonics set (the brief asks about Australian English) | The reviewer's |
| 6 | **How should the history on `feature/product-design` be handled** for the programme names (question 66)? A fresh branch from `develop` holding the final tree needs no force-push | A fresh branch |
| 7 | **Is a scoreboard against other players out for good** (question 5)? | Out |
| 8 | **Is the order of releases right** ([Path](path.md))? | As written |

## Questions for the reviewer

Gate G7 (and section 12.6). None of these is answered here, and none can be by
engineering:

- Which graphemes and words belong to each phase, and what follows Phase B.
- The segmentation of every word, and which words are tricky.
- The order and the wording of the sound-it-out sequence and of the praise.
- The sounds themselves and the accent.
- Whether dead jewels (a board with only the letters taught) are acceptable, or
  a classroom would expect a board with untaught letters.
- Whether linking a vowel team as the finger passes over it helps or distracts.
- The definitions and the wording of what a parent is shown.
- Which inflections count.

## What was measured, and how

All on one Windows 11 desktop, a Release build (MSVC 19.51), by throw-away
programs linking the library's own sources; they are **not committed**, and the
first case of the performance tier replaces them. The raw output is kept with
the session notes outside the repository.

| What | Where used |
| --- | --- |
| ENABLE: 172,823 words; 387,889 trie nodes; load 55 to 65 ms; +17.7 MB | [Performance](performance.md) |
| Solving 200 boards a size, 3x3 to 6x6, letters by ENABLE frequency | [Board](board.md#sizes), [Performance](performance.md) |
| The child-facing lists against the dictionary, by length | [Game](game.md#words-and-what-each-does) |
| Known words per board with two word lists, and the share rejected by a floor | [Board](board.md#dice-after-the-mvp) |
| The 249 SATPIN words: lengths, plurals, words per random board, the share of boards holding three to five with 0 to 12 dead cells, for three index sizes | [Phonics](phonics.md#why-verification-is-the-method) |
| Words of the same indexes on real-looking 4x4 boards (the scanner's yield) | [Game](game.md#playing) |
| The macro-free zone, compiled with stand-ins for Tracy's two types | [Performance](performance.md#a-macro-free-route) |
| Contrast ratios for twelve colour pairs | [Application](application.md#colour-and-contrast) |

## What was not checked

- Anything on a tablet or a phone, and on the author's iPad. Nothing was run on
  a device.
- The glyphs of Andika and OpenDyslexic at the sizes used (neither is installed).
- Qt Multimedia, Qt Sensors and Qt TextToSpeech, and the iOS kit: not in the
  shared tree. Their licence expressions come from documentation and headers
  read, not from SBOMs.
- Tracy's headers were read from source at v0.13.1 and not compiled.
- Whether an iPad has any haptic feedback; whether the stores accept a
  two-spot gate.
- The school fonts' licences; the dice owned by the author.
- Any word list or sound. None was written, by design.
