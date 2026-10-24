// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Every threshold of the performance tier, in one file.
//
// How to read a number here, and how to move it, is in docs/testing.md
// ("Performance tests"). The short version: each work budget sits a quarter
// above what the corpus measures today, so that a change that really does cost
// more is a failure with a name on it, and a change that costs a little more
// is a conscious edit of a number in this file, not a surprise.
// `wjs_perf_tests "[.perf-report]"` prints what each metric measures now beside
// its budget, and how much room is left.
//
// A work budget is exact on every machine and compiler, so it may be tight. A
// time budget is a ratio to a calibration workload and is deliberately loose:
// it is there for the order-of-magnitude slowdown a visit count cannot see (a
// copy where there was a reference, an allocation per visit), and it must
// never fail because another process was busy.
//
// Measured with MSVC 19.51 (/O2, RelWithDebInfo) on one Windows 11 desktop;
// docs/design/performance.md records the machine and the figures.

#include <array>
#include <cstddef>
#include <cstdint>

namespace wjs::perf::budget {

// ---------------------------------------------------------------------------
// The dictionary: ENABLE, data/dictionaries/en.txt
// ---------------------------------------------------------------------------

// Exact, not a quarter above: they change when the data does, and then a
// person decides whether it was meant to (data/README.md).
inline constexpr std::uint64_t kDictionaryWords = 172'823;
inline constexpr std::uint64_t kDictionaryNodes = 387'889;

// ---------------------------------------------------------------------------
// The solver, counted in visits (wjs/solver_work.hpp)
// ---------------------------------------------------------------------------

// The most visits any one of the kBoardsPerSize random boards of each size
// (corpus.hpp) makes; kSizes order, 3x3 to 6x6. Beside each is the figure the
// budget is a quarter above, and the corpus seed of the board that made it.
inline constexpr std::array<std::uint64_t, 4> kMaxVisitsPerSize = {
    640,    // measured 514 (seed 1235367)
    1'940,  // measured 1,548 (seed 1298720)
    3'580,  // measured 2,859 (seed 617687)
    6'470,  // measured 5,174 (seed 1417507)
};

// The boards built to hold many words (corpus.hpp, kRichBoards order): the
// visits of the search, a quarter above what each measured (2,832 / 5,496 /
// 9,443 / 12,350), and the words it finds, which is exact because it pins the
// corpus and the dictionary together.
inline constexpr std::array<std::uint64_t, 4> kRichVisits = {3'550, 6'900, 11'800, 15'500};
inline constexpr std::array<std::uint64_t, 4> kRichWords = {1'126, 1'464, 1'788, 327};

// The boards with no word on them (kEmptyBoards order): exact. The search
// still walks every tile and its neighbours until the trie refuses.
inline constexpr std::array<std::uint64_t, 2> kEmptyVisits = {100, 256};

// A digest of every solution over the corpus (random boards, rich boards,
// empty boards): each word, its score and its path, in the order solve()
// returns them. One number for "the results did not change". It moves when the
// solver's output does, deliberately and with a reason in the commit; it does
// not move for a change that only makes the same answer faster.
inline constexpr std::uint64_t kCorpusDigest = 228066831253711239ULL;

// ---------------------------------------------------------------------------
// The two board modes
//
// Everything above is the STANDARD board's work (dice, the full word list), as
// docs/design/performance.md says. What follows is that mode's making of a
// board, and then the PHONICS board's seeding and binding. The standard board
// is the default and the phonics board a parent's switch (decided 2026-10-16),
// so each section below says which it is for and every metric carries its
// mode (metrics.hpp, BoardMode). The figures below are a quarter above the
// measurement, rounded up to three significant figures.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// STANDARD BOARD (the default): a roll of the project's dice, then a solve
// ---------------------------------------------------------------------------

// The work of making a standard board: roll data/dice/<n>x<n>.txt with the seed
// of each of the kBoardsPerSize corpus boards of a size (corpus.hpp,
// dice_board) and solve the result against the full word list. The most visits
// any one of them makes, kSizes order, a quarter above what each measured, and
// the corpus seed of the board that made it.
inline constexpr std::array<std::uint64_t, 4> kDiceMaxVisitsPerSize = {
    589,    // measured 471 (seed 886931)
    2'050,  // measured 1,636 (seed 823580)
    4'690,  // measured 3,748 (seed 300927)
    8'080,  // measured 6,457 (seed 277171)
};

// A digest of every solution over those boards, in the order solve() returns
// them. Exact, as kCorpusDigest is, and it moves with the dice files (which
// move with the dictionary: data/README.md) or the roll, by a person's decision.
inline constexpr std::uint64_t kDiceDigest = 8038812626269854397ULL;

// ---------------------------------------------------------------------------
// PHONICS BOARD (a parent's switch): seeding and binding
// ---------------------------------------------------------------------------

// The words of the test phase (corpus.hpp, phonics_phase): exact, because it
// pins the phase and the dictionary together.
inline constexpr std::uint64_t kPhonicsWords = 249;

// What seeding spent over kSeedsPerSize seeds of each size on that phase, with
// the options of wjs::phonics_board_options (guided fill, targets of three
// letters at most, every letter but the dead one as filler), the range 3 to 5
// and 20 tries: each a quarter above what it measured, by size, kSizes order.
//
//   tries_total   boards verified, summed (one solve each)
//   solves_total  every solve: the verifications and the guided fill's trials
//   visits_total  the solver's visits over all of them, the machine-independent
//                 cost of the whole corpus
//   visits_max    the visits of the one seeding that made the most
//   fallbacks     boards that were the nearest and not in range
struct SeedBudget {
    std::uint64_t tries_total;
    std::uint64_t solves_total;
    std::uint64_t visits_total;
    std::uint64_t visits_max;
    std::uint64_t fallbacks;
};

//
// 3x3 is the board this phase seeds worst: 77 of its 100 seeds are fallbacks
// (the 23% that land is what docs/testing.md measured for targets of three
// letters), so nearly every seeding spends all twenty verifications. A quarter
// above its 1,739 is 2,174, which is past the 2,000 that 100 seeds of twenty
// tries make; the budget is that cap, and cannot fail. What holds the 3x3 is
// the solves and the visits, which the guided fill's trials make unbounded.
inline constexpr std::array<SeedBudget, 4> kSeedBudgets = {{
    {2'000, 6'880, 610'000, 13'100, 97},   // 3x3: 1,739 / 5,503 / 487,209 / 10,460 / 77
    {830, 27'100, 1'300'000, 31'700, 7},   // 4x4: 664 / 21,653 / 1,036,442 / 25,292 / 5
    {243, 50'900, 3'160'000, 121'000, 0},  // 5x5: 194 / 40,663 / 2,525,621 / 96,479 / 0
    {234, 85'100, 6'980'000, 168'000, 0},  // 6x6: 187 / 68,049 / 5,578,096 / 133,802 / 0
}};

// The seeding's own cap on verifications (SeedOptions::max_tries). Not a
// quarter above anything: it is the algorithm's promise, and no seed may spend
// more.
inline constexpr std::uint64_t kSeedMaxTries = 20;

// A digest of every board seeded over the corpus: tiles, targets, dead jewels,
// the words found, tries and solves. Exact; it moves with the seeding
// algorithm or the phase, deliberately, and with the pinned boards of
// seeding_test.cpp.
inline constexpr std::uint64_t kSeedDigest = 6255957985524133718ULL;

// The binder's comparisons for a path of nine tiles (corpus.hpp, kBindPaths
// order: none, all, mixed) against the 31 multi-letter graphemes of its test
// set, a quarter above what each measured. A tile that starts no grapheme costs
// 31; one that does costs its place in the binder's order.
inline constexpr std::array<std::uint64_t, 3> kBindComparisons = {
    349,  // none: measured 279 (9 tiles x 31 graphemes)
    63,   // all: measured 50 (4 blocks)
    234,  // mixed: measured 187 (7 blocks)
};

// ---------------------------------------------------------------------------
// PHONICS BOARD (a parent's switch): the game session
// ---------------------------------------------------------------------------

// What the session spent dealing and playing kSeedsPerSize boards of each size
// (kSessionSizes order) on the test phase: sessions seeded with corpus_seed,
// the default options (a board of three to five words, up to
// kSessionMaxSeedings seedings of up to 20 verifications each), every board
// played through to the resting screen. Each figure a quarter above what it
// measured, rounded up to three significant figures.
//
//   seedings_total  calls to seed_board, summed (a board that lands first time is one)
//   tries_total     boards verified, summed
//   solves_total    every solve of the deal, the solver's own count
//   visits_total    the solver's visits to deal them all
//   visits_max      the visits of the one deal that cost the most
//   out_of_range    boards that held fewer than three or more than five words
//                   after every seeding
//   words_total     the words of the phase on all of them: exact, it pins the
//                   corpus, the phase and the seeding together
//   restore_visits  the visits of restoring each board once
struct SessionBudget {
    std::uint64_t seedings_total;
    std::uint64_t tries_total;
    std::uint64_t solves_total;
    std::uint64_t visits_total;
    std::uint64_t visits_max;
    std::uint64_t out_of_range;
    std::uint64_t words_total;
    std::uint64_t restore_visits;
};

//
// 3x3 is the board this phase deals worst, as it seeds worst: 35 of its 100 boards
// are out of range after all five seedings (322 seedings for 100 boards), where
// a single seeding left 77 of 100 as fallbacks (kSeedBudgets). The 4x4 lands
// every one, in 106 seedings.
inline constexpr std::array<SessionBudget, 2> kSessionBudgets = {{
    // 3x3: 322 / 5,792 / 17,534 / 1,544,699 / 26,251 / 35 / 522 / 5,122
    {403, 7'240, 22'000, 1'940'000, 32'900, 44, 522, 6'410},
    // 4x4: 106 / 717 / 22,809 / 1,101,833 / 25,292 / 0 / 476 / 6,605
    {133, 897, 28'600, 1'380'000, 31'700, 0, 476, 8'260},
}};

// The session's own cap on seedings for one board (SessionOptions::max_seedings):
// the promise of the design, and no board may take more.
inline constexpr std::uint64_t kSessionMaxSeedings = 5;

// A digest of every board a session dealt and played: the deal, the kind of
// every trace, and the snapshots after the first find and at rest. Exact; it
// moves with the session, the seeding or the phase, deliberately.
inline constexpr std::uint64_t kSessionDigest = 4119037603476531768ULL;

// ---------------------------------------------------------------------------
// STANDARD BOARD (the default): the game session
// ---------------------------------------------------------------------------

// What the session spent dealing and playing kSeedsPerSize boards of each size
// (kSizes order, 3x3 to 6x6) against the project's dice and the full word list:
// sessions seeded with corpus_seed, the default options (a roll that holds a word,
// up to SessionOptions::max_rolls rolls), every board played through to the
// resting screen, which is every word of the dictionary on it. Each figure a
// quarter above what it measured, rounded up to three significant figures, except
// the exact ones.
//
//   rolls_total     boards rolled, summed (a board that holds a word at the first
//                   roll is one); a roll is a solve
//   rolls_max       the most rolls any one board took, with the corpus seed of the
//                   one that took them
//   visits_total    the solver's visits to deal them all
//   visits_max      the visits of the one deal that cost the most
//   words_total     the words of the dictionary on all of them: exact, it pins the
//                   dice, the roll, the dictionary and the deal together
//   restore_visits  the visits of restoring each board once
struct StandardSessionBudget {
    std::uint64_t rolls_total;
    std::uint64_t rolls_max;
    std::uint64_t visits_total;
    std::uint64_t visits_max;
    std::uint64_t words_total;
    std::uint64_t restore_visits;
};

//
// 3x3 is the only size that ever rolls twice: 3.8% of its rolls hold no word, and 8
// of the 100 boards took a second roll or a third (108 rolls; the worst took 3). A
// 4x4 rolls again for 0.15% of its rolls and none of these 100 did, and a 5x5 and a
// 6x6 not at all, so their rolls are the 100 boards themselves. The visits are the
// solver's over the rolls and so are the dice rows' (kDiceMaxVisitsPerSize): the
// worst deal of a 5x5 and of a 6x6 is the very board that is worst there, and the
// restore of a board that landed first time is the same solve again.
inline constexpr std::array<StandardSessionBudget, 4> kStandardSessionBudgets = {{
    // 3x3: measured visits unchanged in the budget; 3,273 words over the rolls
    {135, 4, 23'500, 558, 3'273, 23'300},
    // 4x4: 9,948 words
    {125, 2, 71'400, 1'390, 9'948, 71'400},
    // 5x5: 19,165 words
    {125, 2, 202'000, 4'690, 19'165, 202'000},
    // 6x6: 31,396 words
    {125, 2, 373'000, 8'080, 31'396, 373'000},
}};

// A digest of every board a standard session dealt and played: the deal, the kind
// of every trace, and the snapshots after the first find and at rest. Exact; it
// moves with the session, the roll, the dice or the dictionary, deliberately.
inline constexpr std::uint64_t kStandardSessionDigest = 6788242915648402942ULL;

// ---------------------------------------------------------------------------
// Time, as a multiple of the calibration workload (perf/measure.hpp)
// ---------------------------------------------------------------------------

// Each budget is a ratio of the task's fastest run to the calibration
// workload's fastest run, measured in the same process and run alternately
// with it, so that the machine's speed and a stretch of load cancel out. The
// comment above each is what that ratio measured; the budget is several times
// that. What is left over for the noise that does not cancel (frequency steps,
// a neighbour's cache, a compiler that is not the one the number was taken
// with) is the room between the two, and a copy or an allocation per visit is
// a factor of several, not a few percent. These are not 10% gates and must not
// be tightened into being one.
struct TimeBudget {
    double ratio;
};

// Loading ENABLE, against the heavy calibration (a working set of tens of
// megabytes, which loading a trie of 388,000 nodes has too). Measured 0.65 to
// 0.78: 66 to 68 ms against 84 to 125 ms, the calibration drifting with what
// else the machine was doing, which is what the ratio is for.
inline constexpr TimeBudget kLoadTime = {5.0};
// Solving the 4x4 and the 6x6 built to hold many words, one solve against the
// light calibration (a search is a few thousand visits, which a cache holds).
// Measured 0.042 to 0.043 (0.57 to 0.59 ms) and 0.115 to 0.120 (1.6 ms),
// against 13 to 17 ms.
inline constexpr TimeBudget kSolveRich4Time = {0.3};
inline constexpr TimeBudget kSolveRich6Time = {0.8};

// Dealing one session board (the shake, which seeds up to five times) against the
// light calibration, for the first kDealsPerRun corpus seeds of the size. Measured
// 0.087 and 0.086 for one deal (1.23 and 1.25 ms against 14 ms); the budget is
// several times that, as the solves' are.
inline constexpr TimeBudget kDeal3Time = {0.6};
inline constexpr TimeBudget kDeal4Time = {0.6};

// And dealing one STANDARD session board (the shake: a roll and a solve, and a
// second for the few boards that hold no word) of each size, 3x3 to 6x6, against
// the same light calibration and in the same batches. Measured 0.003, 0.010, 0.023
// and 0.046 for one deal (0.050, 0.150, 0.350 and 0.672 ms against 14 ms); the
// budgets are six to ten times that.
inline constexpr std::array<TimeBudget, 4> kStandardDealTimes = {{
    {0.03},
    {0.08},
    {0.15},
    {0.30},
}};

// How many timed runs each takes, after one that is thrown away; and how many
// solves make one timed run of a solve (a solve is under a millisecond).
inline constexpr int kLoadRuns = 5;
inline constexpr int kSolveRuns = 15;
inline constexpr int kSolvesPerRun = 50;
// And for the deal of a session board: deals in one timed run, and timed runs.
inline constexpr int kDealsPerRun = 5;
inline constexpr int kDealRuns = 9;

// ---------------------------------------------------------------------------
// What the screens wait on besides the solver (data_metrics.cpp)
// ---------------------------------------------------------------------------

// The store that twenty played boards leave, the dashboard built from it, the test
// phase as a directory, and a path submitted to a session. The library keeps no
// counter for these as it does for the solver, so the work is what can be counted
// from outside and does not depend on the compiler or the standard library: bytes,
// records, rows, words. Each figure a quarter above what it measured, rounded up
// to three significant figures, except the exact ones; the time of each is a ratio
// to the calibration workload, below.
//
// Every row states its board mode. The store of the STANDARD board lists no words
// (a board holds tens to hundreds, and the store keeps their count); the PHONICS
// board's lists its own, up to 24, which is most of what a kept board costs.
struct StoreBudget {
    std::uint64_t boards;         // kept boards (exact: wjs::max_stored_boards)
    std::uint64_t finds;          // finds kept over them (exact)
    std::uint64_t vault_words;    // words ever found (exact)
    std::uint64_t bytes;          // the file after the twenty boards: one save, whole
    std::uint64_t written_bytes;  // what the twenty changes wrote, a file after each
};

//
// The standard board's twenty boards hold hundreds of words each, so the store is
// at both of its limits (300 finds, 1,000 vault words) and a save writes 31 KB; the
// phonics board's, 95 finds and 48 words, is 4.8 KB. The change that rewrites the
// file whole after every find is what the written bytes are: 517 KB over the
// twenty changes of the standard board's, on the GUI thread.
inline constexpr std::array<StoreBudget, 2> kStoreBudgets = {{
    // standard: 20 / 300 / 1,000 / 31,013 / 517,233
    {20, 300, 1'000, 38'800, 647'000},
    // phonics: 20 / 95 / 48 / 4,756 / 53,550
    {20, 95, 48, 5'950, 67'000},
}};

// The heaviest store the format allows (data_corpus.hpp, heaviest_store()): the
// file a save writes at worst, and the room under the format's own 128 KiB cap.
// Measured 88,521 bytes, 68% of the cap.
inline constexpr std::uint64_t kStoreHeaviestBytes = 111'000;

// The dashboard built from the phonics board's twenty boards (data_corpus.hpp,
// kept_store) against the test phase: the rows are the phase's graphemes, and what
// they add up to is exact.
struct DashboardBudget {
    std::uint64_t sounds;       // exact
    std::uint64_t boards_met;   // exact
    std::uint64_t words_found;  // exact
    std::uint64_t vault;        // exact
    std::uint64_t inputs;       // boards, finds and vault words it reads
};
// Measured 6 / 81 / 133 / 48 / 163.
inline constexpr DashboardBudget kDashboardBudget = {6, 81, 133, 48, 204};

// The test phase loaded from a directory of its two files (PhaseSet::load_directory).
struct PhaseLoadBudget {
    std::uint64_t phases;     // exact
    std::uint64_t words;      // exact: kPhonicsWords
    std::uint64_t graphemes;  // exact
    std::uint64_t bytes;      // the two files together
};
// Measured 1 / 249 / 6 / 4,345.
inline constexpr PhaseLoadBudget kPhaseLoadBudget = {1, kPhonicsWords, 6, 5'440};

// Time, as a multiple of the calibration workload (below, "Time"): the save a
// find waits for, the parse at start-up, the dashboard a parent opens, the phase
// directory at start-up, a path handed in by a finger and the session that checks
// it. None of these is on a thread but the GUI's. About ten times the highest of
// three runs on the desktop above, against the light calibration (14 to 19 ms; the
// save and the phase directory, which read and write files, against the heavy one,
// 118 to 130 ms). The save and the phase directory touch files, which is the one
// thing here that another process or a virus scanner changes and a ratio to CPU work
// does not cancel (the phase directory measured 0.40 once, with a build running on
// the same disk), so they are held about fifty times, to catch a read or a write per
// word and not a busy disk:
//
//   serialize the heaviest store   0.026 to 0.040  (0.58 ms)   budget 0.4
//   parse it                       0.069 to 0.082  (1.3 ms)    budget 0.8
//   save it to a file              0.018 to 0.022  (2.8 ms)    budget 1.0
//   build the dashboard            0.012 to 0.014  (0.23 ms)   budget 0.15
//   load the phase directory       0.008 to 0.010  (1.0 ms)    budget 0.5
//   200 submits of each kind       0.004 to 0.008  (0.11 ms)   budget 0.08
//   200 validations of 12 tiles    0.001 to 0.002  (0.025 ms)  budget 0.02
inline constexpr TimeBudget kStoreSerializeTime = {0.4};
inline constexpr TimeBudget kStoreParseTime = {0.8};
inline constexpr TimeBudget kStoreSaveTime = {1.0};
inline constexpr TimeBudget kDashboardTime = {0.15};
inline constexpr TimeBudget kPhaseLoadTime = {0.5};
inline constexpr std::array<TimeBudget, 2> kSubmitTimes = {{{0.08}, {0.08}}};
inline constexpr TimeBudget kPathValidateTime = {0.02};

// How many timed runs each takes, and how many submits or traced paths make one.
inline constexpr int kDataRuns = 9;
inline constexpr int kSubmitsPerRun = 200;
inline constexpr int kPathsPerRun = 200;

// ---------------------------------------------------------------------------
// The board scene, counted (apps/app/tests/unit/shake_script_test.cpp)
// ---------------------------------------------------------------------------

// The scene's frame work, where it is countable without a window: the
// simulation and the atlas. A play is one 4x4 weighted play at 60 Hz with the
// stand-in's shake, from ShakeScript.create() to the last die home.

// The whole simulation steps of that play (wjs::dice::World::steps(): a
// Tuning::substep each, 1/1200 s). A count, so the same on every machine and
// compiler; the budget is a quarter above what it measured. It is the cost
// docs/design/shake-prototype.md measured as "62 us a step", counted.
inline constexpr std::uint64_t kSceneSteps4x4 = 3'200;  // measured 2,560

// The cells of a 4x4 board's atlas: six a die, exact. The letters are the glyph
// cells (`Text`, one each) and the jewels the cells that show the jewel's
// texture. `prepare` builds one atlas for a board, so this is the work of its
// frame (docs/design/shake-prototype.md, "The real faces").
inline constexpr std::uint64_t kSceneAtlasCells = 96;
inline constexpr std::uint64_t kSceneAtlasLetters = 48;
inline constexpr std::uint64_t kSceneAtlasJewels = 48;

// The window's frame, during a smoke run's shake cycle (docs/profiling.md,
// "The smoke mode"). The design's budget is one frame's 16.7 ms at 60 Hz and no
// frame over 33 ms during the shake, drop and confetti (docs/design/
// performance.md, "Budgets").
//
// The frame is held against the DISPLAY's period and not a fixed 16.7 ms. A window
// that holds every refresh of a 100 Hz screen has a 10 ms median; one that holds
// every second refresh (50 frames a second, a 20 ms median) has missed half its
// frames. A fixed 17 ms is right for a 60 Hz screen, fails the 100 Hz one for a
// reason that is the display's and not the program's, and cannot see the same miss
// at 144 Hz, where 17 ms is two and a half periods. The smoke report carries the
// refresh rate of the screen the window was on (`refresh_hz`), and the budget
// follows it:
//
//   median   one period and 0.3 ms of room for the display's own period (16.68 ms
//            measured on a 60 Hz one) and a timer's granularity: 16.97 ms at 60 Hz,
//            which is the 17.0 the budget was
//   95th     one period and 2 ms: one frame in twenty may be late, by a little. A
//            100 Hz display measured 10.0 / 10.5 (median / 95th) at idle and
//            10.1 / 14.2 with every core busy; the load fails it, as it should
//   maximum  33 ms, the design's, whatever the display: no frame the player can
//            see as a stall
//
// A report with no refresh rate (0, from a screen that does not say) is held to
// 60 Hz. Measured by apps/app/tests/unit/frame_timing_test.cpp, which runs the window: a
// debug, a Tracy or a coverage build cannot take a clock reading and skips it, and
// it is opt-in (WJS_PERF_FRAME_TIMING=1), so a shared runner never runs it.
inline constexpr double kFrameFallbackHz = 60.0;
inline constexpr double kFrameP50RoomMs = 0.3;
inline constexpr double kFrameP95RoomMs = 2.0;
inline constexpr double kFrameMaxMs = 33.0;

// The display's period in ms: from its refresh rate when that is one a screen has,
// 60 Hz's when it is not (unknown, zero or nonsense).
[[nodiscard]] constexpr double frame_period_ms(double refresh_hz) {
    const bool known = refresh_hz >= 24.0 && refresh_hz <= 1000.0;
    return 1000.0 / (known ? refresh_hz : kFrameFallbackHz);
}
[[nodiscard]] constexpr double frame_p50_budget_ms(double refresh_hz) {
    return frame_period_ms(refresh_hz) + kFrameP50RoomMs;
}
[[nodiscard]] constexpr double frame_p95_budget_ms(double refresh_hz) {
    return frame_period_ms(refresh_hz) + kFrameP95RoomMs;
}

// ---------------------------------------------------------------------------
// The scanner, counted (wjs/scan/work.hpp)
// ---------------------------------------------------------------------------

// Reading four fixed synthetic frames (a 4x4, a 5x5, a 6x6, a 4x4 under glare,
// metrics.cpp). The reads, and every one answering with a board, and the pixels of
// the frames and of the working pictures, are exact: they pin the corpus. The rest
// are what a read does, a quarter above what each measured (rounded up to three
// significant figures), and are not exact because a decision at the edge of a
// threshold (which look found the board, which height fitted best) can differ by a
// compiler's last digit; a change that reads each cell at four times the angles, or
// allocates a buffer a row, is a factor and not a few percent.
inline constexpr std::uint64_t kScanReads = 4;
inline constexpr std::uint64_t kScanFramePixels = 4ULL * 960 * 720;
inline constexpr std::uint64_t kScanWorkingPixels = 4ULL * 480 * 360;
// Beside each, what it measured (MSVC 19.51, /O2): the warped pixels are the board at
// 480 x 480 and each of five looks for it at 240 x 240; the blobs are every bright
// connected set the five looks found, and the dice candidates the die-shaped ones.
inline constexpr std::uint64_t kScanWarpPixels = 2'600'000;  // measured 2,073,600
inline constexpr std::uint64_t kScanBlobs = 3'240;           // measured 2,589
inline constexpr std::uint64_t kScanDiceCandidates = 619;    // measured 495
inline constexpr std::uint64_t kScanCellsRead =
    282;  // measured 225 (a winning trial's sample cells are read once and reused)
inline constexpr std::uint64_t kScanComparisons =
    364'500;  // measured 291,600 (225 cells x 48 templates x 27)
// A warm read allocates no pixel buffers at all: the whole pipeline runs on the
// reader's thread-local scratch pool (libs/scan/src/scratch.hpp); a cold first
// read on a fresh thread allocates about ten, once. The pool's hand-outs over
// the corpus are counted too, so a return to per-read allocation is caught both
// ways.
inline constexpr std::uint64_t kScanBuffers = 0;         // measured 0
inline constexpr std::uint64_t kScanBuffersMax = 0;      // measured 0
inline constexpr std::uint64_t kScanBufferReuses = 105;  // measured 84
// Exact pins of what the optimised reader does, so a change that adds a read
// or a comparison is a failure with a name on it, not a quiet drift.
inline constexpr std::uint64_t kScanBuffersAllocated = 0;
inline constexpr std::uint64_t kScanCellsReadExact = 225;
inline constexpr std::uint64_t kScanComparisonsExact = 291'600;

// One read of a 4x4 and of a 6x6 frame, against the heavy calibration (a read walks
// several pictures, none of which stays in cache). Measured 0.36 to 0.44 (45 to 50 ms)
// and 0.46 to 0.55 (61 to 69 ms) against 102 to 150 ms, three runs, on the desktop
// above; each budget is seven to eight times what it measures.
//
// A read is on the GUI thread (ScanModel runs board_reader::read where the capture
// arrives), so what it costs is the stall the player sees: the 5x5 (0.40 to 0.51)
// and the 4x4 under glare (0.37 to 0.51), the other two frames of the corpus, are
// held as well, at about eight times the highest.
inline constexpr TimeBudget kScanRead4Time = {3.0};
inline constexpr TimeBudget kScanRead5Time = {4.0};
inline constexpr TimeBudget kScanRead6Time = {4.0};
inline constexpr TimeBudget kScanReadGlareTime = {4.0};
inline constexpr int kScanRuns = 7;

}  // namespace wjs::perf::budget
