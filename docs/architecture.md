# Architecture

WordJewelsSolver is a small library of game logic with thin front ends on top
of it, and a separate layer for the few things that differ between operating
systems. This page says what is there. What is not built yet is listed at the
end and in the [roadmap](roadmap.md).

## Source layout

```text
src/
  lib/                 wjs:: the game logic. GUI-free and OS-free
    include/wjs/         board.hpp  dice.hpp  dictionary.hpp  scoring.hpp  solver.hpp
                         path.hpp  phase_data.hpp  seeding.hpp
                         grapheme_binder.hpp  shake.hpp  game_rules.hpp
                         solver_work.hpp  profiling_types.hpp
                         version.hpp.in (configured into a generated header)
    src/                 board.cpp  dice.cpp  draw.hpp (internal)  dictionary.cpp
                         scoring.cpp  solver.cpp
                         path.cpp  phase_data.cpp  seeding.cpp
                         grapheme_binder.cpp  shake.cpp  game_rules.cpp
    variants/            profiling-enabled/wjs/profiling.hpp   (Tracy behind it)
                         profiling-disabled/wjs/profiling.hpp  (empty)
  cli/                 main.cpp: wjs-cli
  app/                 the Qt Quick window (built only with WJS_BUILD_APP)
    qml/Main.qml         the window
    solver_model.*       the model the window talks to
  platforms/           everything that differs between operating systems
    interfaces/wjs/platform/
                         console.hpp  paths.hpp
    windows/             console_windows.cpp  paths_windows.cpp  platform.cmake
    linux/               paths_linux.cpp  platform.cmake
    macos/               paths_macos.cpp  platform.cmake
    posix/               console_posix.cpp (shared by Linux and macOS)
cmake/                 Platform.cmake (selector), PlatformContract.cmake,
                       CompilerWarnings.cmake, Sanitizers.cmake, Coverage.cmake,
                       Tracy.cmake, FindQt6.cmake
tests/                 unit/  app/  platform/  perf/  profiling/
tools/checks/          check_platform_isolation.ps1
tools/data/            check_wordlists.ps1, check_phonics.ps1 and its self-test,
                       make_dice.ps1
tools/hooks/, policy/  git hooks and the commit and PR text rule
```

## How the pieces depend on each other

```text
 wjs-cli      WordJewelsSolver (window)      wjs_unit_tests
      \                |                     /
       +---------------+---------------------+
       |                                     |
       v                                     v
   wjs::lib                            wjs::platform
   board, dictionary, scoring,         one implementation of each
   solver, path, phase data,           interfaces/ header, chosen by CMake
   seeding, grapheme binder, shake,
   game rules
```

- **`wjs::lib`** (target `wjs_lib`, a static library) includes no operating
  system header and no Qt. It links nothing but the warning, sanitizer and
  coverage settings. Everything that decides what a board scores lives here.
- **`wjs::platform`** (target `wjs_platform`, a static library) is the only
  target that includes OS headers. Its public interface is the headers in
  `src/platforms/interfaces/`; its sources are whichever files the selected
  platform's `platform.cmake` names. The library does not use it.
- **The front ends** link both. `wjs-cli` and the window ask the platform layer
  where the executable is (to find a word list beside it) and, in the CLI, to
  put the console in UTF-8; they hand everything else to the library.

How CMake picks the platform and keeps the OS out of the source is in
[Platform isolation](platform-isolation.md).

## The library

All of it is in `namespace wjs`.

### Board

`Board::parse` turns text into a rectangular grid of tiles. A tile holds one to
three letters, A to Z, stored upper-case; the Boggle "Qu" face is the
two-letter tile `QU`. Rows are separated by newlines or `/`; within a row, tiles
are separated by whitespace or commas, and a row with no separators is one
letter per tile. A board is at most 16 by 16, and every row has the same number
of tiles. Failures come back as a `BoardError` (`empty`, `ragged`, `bad_tile`,
`too_large`); there is no exception.

### Dictionary

A `Dictionary` is a trie of upper-case words: a node per letter, each with its
outgoing edges kept sorted by letter and looked up by binary search, and a flag
for "a word ends here". Besides `contains` and `has_prefix`, it exposes the trie
itself as a position in it (`root`, `step`, `is_word`), so a search can ask "is this a
word?" and "could this still become one?" one letter at a time without
rebuilding strings.

`insert` refuses an empty word or one with anything but ASCII letters, leaving
the dictionary unchanged. `load` and `load_file` read one word per line,
trimming whitespace and skipping any line `insert` refuses, so an apostrophe or
a hyphen drops the line rather than failing the load. A file that cannot be
opened is `DictionaryError::cannot_open`.

### Scoring

`score_word` has two schemes (`ScoringScheme`):

- `standard_boggle`: 3 or 4 letters score 1, 5 score 2, 6 score 3, 7 score 5,
  8 or more score 11, and anything shorter scores 0.
- `letter_rarity`: the scheme of the original program, kept for continuity. The
  mean over the word's letters of 100 divided by the letter's percentage
  frequency in English, truncated to an `int`.

### Solver

`solve(board, dictionary, options)` returns every dictionary word that can be
traced through neighbouring tiles without using a tile twice. It is a
depth-first walk from every tile that carries a position in the trie alongside
the path:

1. Step the trie by the tile's letters (two for `Qu`). If the dictionary has no
   word with that prefix, give up this branch at once.
2. Mark the tile used and extend the word. If the trie position is a word and the
   word has at least `min_length` letters, record it, unless that word has
   already been recorded: each word appears once, with the first path found.
3. Recurse into the eight neighbours that are on the board and unused, then
   unmark the tile.

The result is sorted by score (highest first), then length (longest first),
then alphabetically, and carries each word's path as a list of `Position`s.
`min_length` counts letters, so a `Qu` tile is two. The score of a word does
not depend on the path.

Pruning by the trie is what keeps the search small; there is no other
optimisation. The search counts its **visits**, the tile steps that stayed
inside the trie, and `wjs/solver_work.hpp` tallies them per thread, once per
solve; the performance tests hold budgets on that count
([Testing](testing.md#performance-tests)). On the corpus of 3x3 to 6x6 boards
the worst solve is about 5,200 visits and 0.9 ms; a 6x6 built to hold 1,788
words is about 9,400 visits and 1.6 ms. Nothing larger than 6x6 has been
measured.

### Profiling

`wjs/profiling.hpp` is the one place the code asks to be timed: `zone<"name">()`,
`frame_mark()`, `plot()`, `message()` and `thread_name()`, as functions and not
macros. There are two files of that name under `src/lib/variants/`, one with
Tracy behind it and one empty, and CMake puts exactly one directory on the
include path, so no source asks which it got. A normal build has the empty one.
See [Profiling](profiling.md).

### Path

`validate_path(board, path)` checks a path a finger or the solver traced against
a board: every position on the board, no tile used twice, each tile touching the
one before it (diagonals included, edges not wrapping). On success it is the word
the tiles spell, upper-case, a two-letter tile such as `Qu` adding both letters;
otherwise a `PathError` (`empty`, `off_board`, `repeated_tile`, `not_adjacent`)
for the first fault. `are_neighbours` is the one-step question, for a touch
handler that asks it a tile at a time.

### Dice

`DiceSet` is a set of dice, each with faces of one to three letters (the Boggle
`Qu` face is the tile `QU`), read from a text file, one die a line (`parse`,
`load`, `load_file`). `roll(dice, seed)` shakes them onto a square board: the
dice are put in a random order, a die to a tile, and each shows one face. It is a
pure function of the set and the seed, with the library's own `Draw`
(`std::mt19937` taken modulo, and a shuffle that is not `std::shuffle`), so a seed
is the same board on every compiler. The set must hold a square number of dice, so
the 3x3 is its own set and not a window on the 4x4.

The library holds no set. The project's are `data/dice/<n>x<n>.txt` for 3x3 to
6x6, made by `tools/data/make_dice.ps1` from the dictionary's letter counts: six
different letters to a die dealt in proportion to the counts, a vowel on each, a
Q shown as `QU`, a seed to each size, and the source's hash and the method in the
header ([Data](https://github.com/iainchesworthlabs/wordjewelssolver/blob/develop/data/README.md)).
`data.dice` holds the files to what the tool makes. The faces of a printed set are
not used: in the scanner mode the board is read from the table.

### Phase data

A phonics phase is data: which graphemes it teaches and which words it offers,
each word split into graphemes. The library holds no phase of its own and knows
no letter, sound or word of one; the files are written and reviewed by a person
qualified to do it, and **none exists in the repository** (the tests carry a
dozen words of test data of their own). `phase_data.hpp` reads and checks them.

Two kinds of file, in `data/phonics/`, one line each, `#` lines being the header
or a comment:

```text
# phase: A
# reviewed-by: <name>   reviewed-on: <date>   source: <where it came from>

phase-<x>.graphemes.txt     ai  /sound/  clip:ai
phase-<x>.words.txt         pain  p ai n
                            <word>  tricky
```

- A grapheme is lowercase letters, its sound is written between slashes (any
  bytes, so a phonetic alphabet passes through), its clip id is `clip:<id>`.
  Two graphemes may share a clip.
- A word is three or more lowercase letters (nothing shorter can be counted)
  followed by its graphemes, or by `tricky` and nothing else: a word that does
  not decode, spoken whole and never sounded out. The graphemes, joined, spell
  the word.
- **A split digraph** is written as its two halves around the letters between,
  `cake  c a_ k _e`, and is the grapheme `a_e` in the graphemes file. The halves
  are one grapheme: `WordGrapheme` carries where each half sits in the word
  (`first` and `second`). It is not found while tracing (below).
- **A grapheme that makes more than one sound** is listed once for each sound,
  and a word line says which, counting from 1 in the order of that grapheme's
  lines: `snow  s n ow:2`, `a_:2` for a split digraph. A grapheme with several
  sounds has to be told which; with one, the number is optional. A later phase
  may add a sound to a grapheme, and an earlier phase's words cannot use it.
- Both files are sorted in byte order, and hold no word twice, and no grapheme
  with the same sound twice.
- A header is `key: value` pairs, several to a line. A file with no
  `reviewed-by`, `reviewed-on` (a date, `YYYY-MM-DD`) and `source` is **test
  data**: `PhaseHeader::reviewed()` and `PhaseSet::reviewed()` say so, and a
  release build refuses it
  ([Testing](testing.md#phonics-data-the-checker-and-the-review-gate)). A value
  still in `<angle brackets>` has not been given. Test data need not say where
  it came from; a file that claims a review has to.

`parse_words` and `parse_graphemes` read one file and report every problem, with
its line, not the first. `PhaseSet::build` (or `load_directory`, which reads
`phase-<x>.*.txt` from a directory) holds the files together to the checks of
the design: each word is in the English dictionary, each grapheme is in the
word's phase or an earlier one and a grapheme with several sounds there is told
which, nothing is listed twice across phases. (That the graphemes spell the word
is held at parsing, where the line is.) Phases are **cumulative** and ordered by name: a `PhaseIndex` holds the
words and graphemes of its phase and every earlier one, with a `Dictionary` of
the words, which is what a board is solved against, and the phase's `alphabet()`.
`tools/data/check_phonics.ps1` applies the same checks outside the build.

### Seeding

`seed_board(index, options, recent, seed)` makes the board of Mode B. It is a
pure function of the phase data, the options, the recent targets and a seed:

1. **Choose** two or three target words of the index. Words with a grapheme that
   makes a focus sound come first, and within each of those and the rest, words
   that are not among the recent targets come first. A third target is taken
   only from the focus words while the targets are all focus words.
2. **Plant** each as a path of touching tiles, a tile shared where the letter
   agrees, leaving room for the dead jewels.
3. **Fill** the other tiles: `dead_cells` of them with the dead letter, the rest
   from the filler (the phase's alphabet by default).
4. **Verify** with `wjs::solve` against the phase's dictionary. Three to five
   words is kept. Otherwise another board is made, up to `max_tries` (20)
   verifications in all, and the nearest is returned with `fallback` set.

Two options make fuller boards (measured in
[Testing](testing.md#fuller-boards-a-guided-fill-and-short-targets)):
`max_target_letters` limits the targets to short words, and `guided_fill` gives
each tile left a letter the solver says keeps the count in range, making a tile
dead only if no letter fits, with the planted words kept apart where there is
room. Both are off by default, and `phonics_board_options(index, size)` is the
two together with every letter but one as filler, which is the phonics board as
decided: the phase's letters first, others only where a letter of the phase
would make too many words, and no dead jewel on a board that lands.

Dead jewels are a letter no word of the phase uses, chosen by the caller and
checked (`bad_dead_letter`); the library's `Board` stays A-Z and the application
draws that letter as a jewel. Draws are `std::mt19937` taken modulo and shuffles
are the library's own, never `std::uniform_int_distribution` or `std::shuffle`,
so a seed is the same board on every compiler. `RecentTargets` is the ring
buffer of the last ten boards' targets, which the caller keeps. What was
measured of its share and cost is in
[Testing](testing.md#seeding-what-was-measured).

### Grapheme blocks

`GraphemeBinder` binds a traced path into blocks as the finger moves: the
letters of the tiles are matched greedily, longest first, against the
multi-letter graphemes of the phase, and each run of whole tiles that spells one
is a block. A tile that starts no grapheme is a block of its own, and a grapheme
cannot start or end inside a tile. The brief's example: R, A, I, N is `R [AI] N`.
A split digraph is not found in flight (it cannot be known until its last
letter); the design draws it from the word's authored split, which the data
holds as the two halves and where each sits (`WordGrapheme::first` and
`second`), and the binder leaves such a grapheme out.

### Shake detection

`detect_shakes(samples, options)` is a pure function over `(t, ax, ay, az)`
accelerometer samples. Gravity is followed by a low-pass filter and taken away;
a jolt is the remainder going over a threshold (1.2 g), jolts must alternate
direction and come within 0.4 s of each other, and four make a shake. The result
is each shake's start, end, jolt count and peak, and whether the trace ended while
it may still be going, which is what a live caller asks of a short window of the
latest samples. A knock, turning the device over, tilting it slowly, walking and a
gentle wave are not shakes. The numbers are starting values, tuned on synthetic
traces and not on a device. Reading the sensor is an operating-system service and
belongs behind an interface in `src/platforms`, which is not written.

### Game rules

`game_rules.hpp` is what the touch handler and the tray call to ask a board in
play four questions. It draws nothing, plays nothing and keeps nothing between
boards; the application keeps the answers.

The questions are put to a **word set**, `WordSets{dictionary, active}`, because
the first release has two board modes and the rules do not know which they are
on (decided by the author, 2026-10-16; see [MVP library](design/mvp-library.md)).
On the **standard board**, a roll of dice, `active` is the full dictionary
itself (`WordSets::standard`). On the **phonics board**, the parent's switch,
`active` is the words of the active `PhaseIndex` (`WordSets::phonics`). Both are
held by reference and must outlive the `WordSets`.

**What a traced path is.** `classify_path(board, path, words)` checks the path
with `validate_path` and answers one of three states, never "wrong":

| `WordState` | When | `word` | The application |
| --- | --- | --- | --- |
| `viable` | the word has three letters or more and `active` has it | the word, upper-case | a jewel, stars, the sound-it-out sequence |
| `real_not_todays` | three letters or more, in `dictionary` and not in `active` | empty | the neutral cue: no star, no word shown or read |
| `not_a_word` | anything else: a word of two letters or fewer, a string that is no word, a path `validate_path` refuses | empty | the path fades |

On the standard board `active` and `dictionary` are one, so the middle state
cannot arise. A refused path is `not_a_word` with the `PathError` in `fault`.
The word is withheld from the two states that are not viable on purpose: the
design never shows or reads a word that is not one of today's, and the library
does not hand the application one to show. `viable_words(board, words)` is the
solve of the board against `active` at the minimum length, the list the other
two rules read; a path is viable exactly when its word is in it.

**Tier and stars** come from the letter count and are never stored:
`tier_for_letters(n)` (a `Qu` tile counts two, so the word's `size()` is the
count) and `reward_for(tier)`:

| Letters | `Tier` | Stars | Sticker |
| --- | --- | --- | --- |
| 3 | `spark` | 1 | no |
| 4 | `power` | 2 | yes, the first time the word is found on this device |
| 5 or more | `mega` | 5 | no (its accessory is after the MVP) |

Fewer than three letters have no tier. Whether this is the first time a word
is found on the device is the application's to know; `Reward::sticker` says
only that the tier carries one. The Mega accessory is not modelled.

**The words found on a board** are a `FoundWords`: each once, whatever its
case, in the order found. `insert` returns whether the word was new, which is
when the application awards stars; a word traced again on the same board is
`viable` and `insert` says false.

**The start-tile help.** `start_tile_help(viable, found)` is the start tile of
one viable word not yet found, chosen as a total order so that nothing is
remembered and the same press gives the same answer:

1. the shortest word, counted in letters (QUIT is four);
2. then the word whose first tile is nearest the top-left tile, by
   straight-line distance (row squared plus column squared, an integer), the
   upper tile winning a tie and then the left one;
3. then the word that sorts first.

The tile is the first of the path the solver reported, which is the first start
tile in reading order that can trace the word, so the word can be traced from
the tile that lights. Because the answer is the least of the words left in a
fixed order, finding any other word cannot move it: it stays until its word is
found, and then the same call names the next. It is `nullopt` when no viable
word is left.

**Cleared.** `is_cleared(viable, found)` is true when no viable word is left
unfound. A board with no viable word at all is cleared in that literal sense,
and none is meant to be dealt: the board is decided and checked before the
shake, so a child never lands on one (the author, 2026-10-16). A phonics board
always holds its planted targets, so `seed_board` already keeps to that; the
standard board's deal does not check yet (below).

What is the library's reading of the design, and what the design left open, is
on [Testing](testing.md#game-rules-what-is-pinned).

### Errors

An expected failure (a board that cannot be read, a word list that cannot be
opened) is a `std::expected<T, E>` value with a small enum for `E` and a
`to_string` for it. The library does not throw for them.

### The version header

`src/lib/include/wjs/version.hpp.in` is configured by CMake into a generated
header that holds `wjs::project_version` and `wjs::target_platform` as
`constexpr` values. That is how the version and the name of the platform reach
the code without a macro, and why `wjs-cli --version` can print them.

## The platform layer

An OS service the program needs is declared in a header under
`src/platforms/interfaces/wjs/platform/`, and defined once per platform:

| Interface | What it does | Windows | Linux | macOS |
| --- | --- | --- | --- | --- |
| `paths.hpp`: `executable_path()` | The absolute path of the running program, so data beside it can be found | `GetModuleFileNameW` | `/proc/self/exe` | `_NSGetExecutablePath` |
| `console.hpp`: `use_utf8_console()` | Make the console read and write UTF-8 | `SetConsoleCP`, `SetConsoleOutputCP` | nothing to do (`posix/`) | nothing to do (`posix/`) |

Linux and macOS share `posix/console_posix.cpp`, named by both
`platform.cmake` files.

## The Qt Quick window

`src/app` is a small Qt Quick application and is optional (`WJS_BUILD_APP`,
off by default; it needs a prebuilt Qt 6.12). `SolverModel` is a `QObject` that
owns a `wjs::Dictionary`, loads it from `--dictionary <file>` or from
`dictionaries/en.txt` beside the executable, and turns a board string into a
`QVariantList` of words, scores and paths for QML. `Main.qml` is a grid of
text fields, a Solve button and a list. All the solving is the library's; the
model only reshapes its answer. The library knows nothing of Qt, and the model
has its own Catch2 tests.

What makes the window a program of the platform's desktop (a GUI-subsystem
executable on Windows, an application bundle on macOS) and what puts Qt's
runtime beside it are two hooks in each platform's `platform.cmake`, not tests
of the operating system in CMake.

## Build targets

| Target | Kind | Notes |
| --- | --- | --- |
| `wjs_lib` (`wjs::lib`) | static library | The game logic |
| `wjs_platform` (`wjs::platform`) | static library | The platform's implementations |
| `wjs_cli` | executable `wjs-cli` | `WJS_BUILD_CLI` |
| `wjs_app` | executable `WordJewelsSolver` | `WJS_BUILD_APP` |
| `wjs_unit_tests` | executable | Catch2 |
| `wjs_perf_tests` | executable | Catch2; work budgets and opt-in wall-time checks, linked to `wjs::lib` only |
| `wjs::tracy` | interface library | Tracy's client when `WJS_ENABLE_TRACY` is on, otherwise nothing |
| `wjs_app_tests` | executable | Catch2, with `WJS_BUILD_APP` |
| `wjs::warnings`, `wjs::sanitizers`, `wjs::coverage` | interface libraries | Flags, linked into every first-party target |

## Limits of what exists

- Words and tiles are the letters A to Z. There is no support for accented
  letters or other scripts, and a word list line with one is skipped.
- One word list is loaded for a run; there is no per-language handling.
- The board is at most 16 by 16 and a tile holds at most three letters.
- The only front ends are the CLI and the small window.

## Not built yet

These do not exist, and nothing in the tree stands in for them:

- **Camera input.** Cameras are an OS service, so by the rule in
  [Platform isolation](platform-isolation.md) any camera API will sit behind an
  interface in `src/platforms/interfaces/` with an implementation per platform.
  How is not designed.
- **Reading letters from a picture.** The seam is the board: whatever reads
  the picture has to produce the text `Board::parse` accepts, or a `Board`.
  The recognition stack is not chosen, and its choice carries licensing
  questions (see [Licensing](licensing.md)).
- **Phonics phase data.** The reader, the checks, the seeding, the binder, the
  shake detector and the game rules exist and are tested, and no front end uses
  them yet. No phase
  file exists, and none is written here: the content has to be written and
  reviewed by someone qualified to do it before a child uses it. The audio
  clips, the sound-it-out sequence and the dashboard are not built.
- **A standard board that always holds a word.** The standard board's deal
  (`wjs::roll` on the dice branch) does not check what it rolled, and with the
  full dictionary some rolls hold nothing: measured 2026-10-16 with the
  project's own dice, 3,830 of 100,000 3x3 rolls (3.8%), 30 of 20,000 4x4 rolls
  (0.15%) and none of 5,000 5x5 or 2,000 6x6 rolls. The author's rule is that
  such a board is never dealt, so the deal has to solve what it rolled with
  `viable_words` and roll again until it holds a word, before the shake. Not
  built.
- **Using the child-facing word lists.** `data/lexicon/` holds category and
  early-reader tier lists (animals, colours, shapes, and so on); nothing reads
  them yet. The [roadmap](roadmap.md) (M2) covers the lexicon and hints.
- **Packaging and installers**, and **phone and browser targets**.

The [roadmap](roadmap.md) has the plan and its order.
