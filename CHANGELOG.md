# Changelog

*For people following what has shipped. The project overview is the
[README](README.md).*

All notable changes to this project are documented in this file, in the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format. This project
adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

WordJewelsSolver began in 2013 as a Windows-only Win32 and DirectShow program.
This is its rebuild as a portable C++23 CMake project. Nothing has been
released: the first release will be 0.1. The repository is public from before
that release, so that its rules (branch protection, required checks) can apply.

### Added

- **A prototype of the 3D board scene**, built only with
  `-DWJS_BUILD_SHAKE_PROTOTYPE=ON` (and `WJS_BUILD_APP=ON`): the shake, the lid
  and the drop in Qt Quick 3D behind a one-component interface, scripted from a
  seed with no physics engine, with a reduced-motion variant, a mouse drag
  standing in for the accelerometer, frame timing, and a light probe baked by
  `tools/lightprobe`. The program links Qt Quick 3D and so is GPL-3.0 only. See
  [docs/design/shake-prototype.md](docs/design/shake-prototype.md).
- **A solver library with no GUI and no operating system code** (`wjs::lib`).
  It parses a board (one letter per tile, or tiles of one to three letters
  separated by spaces or commas so that the Boggle "Qu" face works, up to 16 by
  16), holds a word list as a trie, scores words, and finds every word on the
  board by a depth-first search that reports the tiles each word is spelt by.
  Two scoring schemes: the printed Boggle scores, and the letter-rarity score
  of the original program. See [docs/architecture.md](docs/architecture.md).
- **`wjs-cli`, a command-line solver.** `wjs-cli [-d <file>] [-m <n>]
  [-s boggle|rarity] <row>...` prints each word, its score and its path, and
  the totals. Without `-d` it uses the English dictionary beside the
  executable. See [docs/index.md](docs/index.md#using-the-command-line-solver).
- **A small Qt Quick window**, built only with `-DWJS_BUILD_APP=ON` and Qt 6.12:
  a grid to type a board into, a Solve button and a scored word list. The model
  behind it has unit tests; the window itself has been started offscreen and
  not otherwise tested. See [docs/building.md](docs/building.md#the-qt-quick-window).
- **Per-operating-system code in one place** (`src/platforms`): the path of the
  running executable for Windows, Linux and macOS, and a UTF-8 console for
  Windows. `cmake/Platform.cmake` is the only place the target system is
  decided; each platform's `platform.cmake` says what it compiles. See
  [docs/platform-isolation.md](docs/platform-isolation.md).
- **A platform-isolation check**, `tools/checks/check_platform_isolation.ps1`
  (also the `platform.isolation` test): no preprocessor conditionals or
  `#define`s in first-party C++, no OS headers or OS-querying calls outside the
  platform section, no OS tests in CMake outside the selector, and every
  platform file wired into a `platform.cmake`. A second test,
  `platform.fragments`, holds every platform's `platform.cmake` to the contract
  on whichever system runs it.
- **Unit tests** (Catch2) for the board, the dictionary, scoring, the solver and
  the platform layer. See [docs/testing.md](docs/testing.md).
- **CMake presets** for Windows (MSVC), Linux (GCC, with a coverage preset) and
  macOS (Clang); the dependency provider, vcpkg, is a git submodule. Every
  first-party target builds with warnings as errors, and Debug builds run under
  AddressSanitizer (and UBSan on GCC and Clang). See
  [docs/building.md](docs/building.md).
- **The licence and the project documents**: the GPLv3 text (`LICENSE`; the
  project is GPL-3.0-or-later), `README.md`, `CONTRIBUTING.md`,
  `CODE_OF_CONDUCT.md`, `SECURITY.md` and the documentation under `docs/`, built
  with mkdocs-material. See [docs/licensing.md](docs/licensing.md).
- **An English word list** (`data/dictionaries/en.txt`): ENABLE, unmodified, with
  its source and SHA-256 in [data/README.md](data/README.md). A build copies it
  beside the program, so `wjs-cli` and the window work with no flag.
- **Child-facing word lists** (`data/lexicon/`): ten categories (animals,
  colours, shapes, food, body, family, nature, home, clothes, transport) and a
  237-word early-reader tier, each a subset of the dictionary and held to that by
  `tools/data/check_wordlists.ps1` (also the `data.wordlists` test). Nothing uses
  them yet, and they have not been reviewed by a teacher or a parent.
- **Continuous integration and repository rules**: workflows for formatting,
  platform isolation, Windows/Linux/macOS builds and a coverage report; a
  commit and pull-request text policy; repository rulesets as code; and local
  hooks. None has run yet. See [docs/ci.md](docs/ci.md).
- **Profiling with Tracy, off by default** (`wjs/profiling.hpp`). The
  repository allows no `#define` or `#if`, and Tracy's interface is macros, so
  the library has a macro-free wrapper in two variants (`zone<"name">()`,
  `frame_mark()`, `plot()`, `message()`, `thread_name()`), one of which has
  Tracy behind it and one nothing, selected by CMake's include path. The
  `*-profiling` presets turn it on (`WJS_ENABLE_TRACY`, a vcpkg `profiling`
  feature with the client and the capture and export tools); nothing that ships
  has it, which `profiling.shipped_presets` holds. The solver and the
  dictionary load carry the first zones, and `wjs-cli` has `--repeat <n>` so
  that a profiler has time to attach. See [docs/profiling.md](docs/profiling.md).
- **A performance-test tier** (`wjs_perf_tests`, `tests/perf`): exact work
  budgets on what the solver does (visits, words, trie nodes, and a digest of
  every result) over a fixed corpus of 800 random boards and six built ones,
  which run in every optimised build and skip in Debug, and wall-time checks as
  a ratio to a calibration workload, which run only on request. The solver has a
  work counter (`wjs/solver_work.hpp`) for it. A Linux Release leg in CI runs
  it. See [docs/testing.md](docs/testing.md#performance-tests).

### Changed

- **Rebuilt as a portable C++23 project.** The Visual Studio solution is
  replaced by CMake (4.0 or newer), Ninja and the presets above, and the
  program is split into a library, front ends and a platform layer, with tests.

### Removed

- **The Win32 and DirectShow program**: the Visual Studio solution and its
  sources (the engine, the DirectShow capture and renderer code, and the Win32
  window), and the empty per-language word list placeholders. The code remains
  in the git history before the rebuild. The rebuilt program has no camera
  input yet.

> **Pre-release.** The first continuous-integration run built and tested the
> library, the command-line solver and the platform layer on Windows (MSVC
> 19.51), Linux (GCC 15.2) and macOS (Apple clang 21), all green with warnings
> as errors and sanitizers on; the Qt window is built only on Windows so far.
> There is no camera input, no reading of letters from a picture, and no
> installer or package. It has not been tried with children. Opening a file by a
> path longer than `MAX_PATH` fails on Windows, since the executables are not
> long-path aware. `wjs-cli` itself is checked by hand, not by a test.
