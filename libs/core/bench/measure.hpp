// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How the performance tests measure, in one place so that the budget tests,
// the timing tests and the hidden report all read the same numbers.
//
// Two kinds of figure, kept apart on purpose (docs/testing.md, "Performance
// tests"):
//
//   work    counted, not timed: the visits a search made. The same on every
//           machine, compiler and build type, so a budget on it is exact and
//           can sit close to the truth.
//   time    a stopwatch, only ever as a ratio to a calibration workload run in
//           the same process, and never trusted outside an optimised,
//           uninstrumented build (perf_build.hpp).

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_session.hpp"
#include "wjs/grapheme_binder.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/seeding.hpp"
#include "wjs/solver.hpp"

namespace wjs::perf {

// ---------------------------------------------------------------------------
// The dictionary
// ---------------------------------------------------------------------------

// ENABLE, loaded once for the whole run (it is tens of milliseconds, and every
// case wants the same one). The process ends if the file cannot be read: the
// tier has nothing to say without it.
[[nodiscard]] const wjs::Dictionary& english();

// How many nodes the trie holds, counted through the public interface: the root
// and everything a step from it reaches.
[[nodiscard]] std::uint64_t trie_nodes(const wjs::Dictionary& dictionary);

// ---------------------------------------------------------------------------
// Solver work
// ---------------------------------------------------------------------------

struct SolveWork {
    std::uint64_t visits = 0;
    std::uint64_t words = 0;
};

// One solve of `board`, with the work counter reset first and read after.
[[nodiscard]] SolveWork measure_solve(const wjs::Board& board, const wjs::Dictionary& dictionary);

// The visits of the walk written out independently of the solver: the same
// depth-first search over the trie, with nothing else. The solver's own tally
// must equal it (work_test.cpp), which is what ties the numbers in the budgets
// to a definition and not to an implementation.
[[nodiscard]] std::uint64_t recount_visits(const wjs::Board& board,
                                           const wjs::Dictionary& dictionary);

// ---------------------------------------------------------------------------
// Seeding work (the phonics board)
// ---------------------------------------------------------------------------

// One seeding with the options of wjs::phonics_board_options for the board's
// size, no recent targets, and the work counter reset first and read after.
// The process ends if the options or the seeding are refused: the corpus
// cannot be said to have been seeded.
struct SeedRun {
    wjs::SeededBoard board;
    // The solver's own tally for the call: the visits of every solve it made,
    // and how many solves that was. `board.solves` is the seeding's count of
    // the same thing, which work_test cross-checks.
    std::uint64_t visits = 0;
    std::uint64_t solver_solves = 0;
};

[[nodiscard]] SeedRun measure_seed(const wjs::PhaseIndex& index, std::size_t size,
                                   std::uint32_t seed);

// ---------------------------------------------------------------------------
// Binder work (the phonics board)
// ---------------------------------------------------------------------------

// The comparisons of one bind of `tiles`, from the library's tally, with the
// blocks it returned.
struct BindRun {
    std::vector<wjs::GraphemeBinder::Block> blocks;
    std::uint64_t comparisons = 0;
};

[[nodiscard]] BindRun measure_bind(const wjs::GraphemeBinder& binder,
                                   std::span<const std::string> tiles);

// The comparisons the blocks of a bind say it made, written out from the
// definition and nothing the library counts: the graphemes the binder holds are
// the multi-letter ones that are not split, lower-case, longest first and then
// in letter order; a block that starts no grapheme was held against every one,
// and a block that starts one was held against those before it and itself. The
// library's tally must equal it (game_work_test.cpp).
[[nodiscard]] std::uint64_t recount_comparisons(std::span<const wjs::Grapheme> graphemes,
                                                std::span<const wjs::GraphemeBinder::Block> blocks);

// ---------------------------------------------------------------------------
// Session work (both board modes)
// ---------------------------------------------------------------------------

// One board of the game session, dealt and played through, and what each part
// cost the solver. The solver's tally is reset before the part and read after
// it, so each figure is the work of that part and nothing else.
struct SessionRun {
    // The board mode of the session: the library's, not the metrics' (which names
    // the same two).
    wjs::BoardMode mode = wjs::BoardMode::phonics;

    // The session's own account of the deal, and the solver's: the visits and
    // solves of the shake. `deal.solves` is the session's count of the same
    // solves, which game_work_test cross-checks.
    wjs::DealReport deal;
    std::uint64_t deal_visits = 0;
    std::uint64_t deal_solves = 0;

    // Everything after the shake: still, landed, a find for each word of the
    // board (with the start-tile help asked for before every other one), a word
    // traced again, a touch of one tile, a snapshot after the first find, the
    // record, rest, and the last snapshot. Not one of them solves.
    std::uint64_t play_visits = 0;
    std::uint64_t play_solves = 0;

    // restore() of the snapshot taken after the first find: the one solve that
    // reads the board again.
    std::uint64_t restore_visits = 0;
    std::uint64_t restore_solves = 0;

    std::size_t words = 0;  // the words that count on the board: the phase's, or the dictionary's
    // The kinds each trace came to, in order, and the two snapshots.
    std::vector<wjs::TraceKind> kinds;
    wjs::SessionSnapshot after_first_find;
    wjs::SessionSnapshot at_rest;
};

// A PHONICS session of the given size, seeded with `seed`, dealt and played as
// above. The process ends if the session cannot be made or refuses the deal: the
// corpus cannot be said to have been played.
[[nodiscard]] SessionRun measure_session(const wjs::PhaseIndex& index, std::size_t size,
                                         std::uint32_t seed);

// The same for a STANDARD session: the project's dice (corpus.hpp, dice_sets) of
// the size, the full word list, the same play.
[[nodiscard]] SessionRun measure_standard_session(std::size_t size, std::uint32_t seed);

// One session's shake, for the timing rows: a session of this size seeded with
// `seed`, dealt once. Returns the words that count on the board.
[[nodiscard]] std::size_t measure_session_deal(const wjs::PhaseIndex& index, std::size_t size,
                                               std::uint32_t seed);
[[nodiscard]] std::size_t measure_standard_session_deal(std::size_t size, std::uint32_t seed);

// An FNV-1a digest of what solve() returned: each word, its score and its path,
// in order, and of what seed_board() made. Add one result after another for a
// corpus' digest.
class Digest {
public:
    void add(const std::vector<wjs::FoundWord>& found);
    void add(const wjs::SeededBoard& seeded);
    void add(const SessionRun& run);
    [[nodiscard]] std::uint64_t value() const noexcept { return state_; }

private:
    void byte(std::uint8_t value) noexcept;
    void number(std::uint64_t value) noexcept;
    void text(const std::string& value);
    void add(const wjs::SessionSnapshot& snapshot);

    std::uint64_t state_ = 14695981039346656037ULL;
};

// ---------------------------------------------------------------------------
// Time, relative to a calibration workload
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

[[nodiscard]] inline double milliseconds_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// What the build says about itself (perf_build.hpp), as calls rather than
// constants, so that a case that always skips in some build does not make the
// statements after the skip unreachable code to the compiler.
[[nodiscard]] bool optimised_build();
[[nodiscard]] bool timing_meaningful();

// True when the person running the tests has asked for the wall-time cases:
// WJS_PERF_TIMING is set to something other than "" or "0". They are opt-in
// because a stopwatch on a shared machine is the one check that can be wrong
// for no reason of its own (docs/testing.md, "Performance tests").
[[nodiscard]] bool timing_requested();

// How much a calibration workload touches. A task that stays in cache (a
// search of a few thousand visits) is calibrated against one that does; a task
// that does not (loading a trie of 388,000 nodes, each with a vector of its own)
// against one that does not either. A busy neighbour costs the second kind
// several times what it costs the first, and a ratio that follows the machine's
// load is no use.
enum class Weight { light, heavy };

// A fixed amount of integer, hash-table and copy work that touches none of the
// code under test, so a regression in that code cannot also slow it down and
// cancel itself out of the ratio. What it measures is how fast this machine is,
// right now, at the kind of work the solver and the loader do (hash insertions,
// small structs appended and read back, small allocations). Its result is
// returned so that the optimiser cannot drop it.
[[nodiscard]] std::uint64_t calibration_workload(Weight weight);

// What a task cost against the calibration workload.
//
// `ratio` is the task's fastest run over the calibration's fastest run. The
// fastest, not the median: interference only ever adds time, so the fastest run
// is the one the code itself cost and a burst of load that lands on a third of
// the runs cannot move it. A regression slows every run, the fastest included,
// so it still shows. The medians are kept for the report, which is where a skew
// between the two is worth seeing.
struct RelativeTime {
    double task_ms = 0.0;
    double calibration_ms = 0.0;
    double ratio = 0.0;
    double task_median_ms = 0.0;
    double calibration_median_ms = 0.0;
};

namespace detail {
// Written, never read: the sink the results of timed work are stored in.
inline volatile std::uint64_t g_sink = 0;

[[nodiscard]] double median(std::vector<double> values);
}  // namespace detail

// Runs `task` and the calibration workload alternately, `runs` times each
// after one warm-up of each. `task` takes no arguments and returns a number
// that depends on its work (a count, a checksum): it is what keeps an optimiser
// that sees the whole program from deciding the work was not needed.
template <typename Task>
[[nodiscard]] RelativeTime time_against_calibration(Task&& task, int runs, Weight weight) {
    std::uint64_t sink = calibration_workload(weight);
    sink += static_cast<std::uint64_t>(task());
    std::vector<double> task_ms;
    std::vector<double> calibration_ms;
    for (int run = 0; run < runs; ++run) {
        auto start = Clock::now();
        sink += calibration_workload(weight);
        calibration_ms.push_back(milliseconds_since(start));
        start = Clock::now();
        sink += static_cast<std::uint64_t>(task());
        task_ms.push_back(milliseconds_since(start));
    }
    detail::g_sink = sink;

    RelativeTime out;
    out.task_ms = *std::ranges::min_element(task_ms);
    out.calibration_ms = *std::ranges::min_element(calibration_ms);
    out.ratio = out.task_ms / out.calibration_ms;
    out.task_median_ms = detail::median(task_ms);
    out.calibration_median_ms = detail::median(calibration_ms);
    return out;
}

}  // namespace wjs::perf
