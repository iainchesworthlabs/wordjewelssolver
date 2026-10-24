// SPDX-License-Identifier: GPL-3.0-or-later
// Work budgets for the code that makes a board and shows its blocks: the roll of
// the standard board's dice, the seeding of the phonics board and the
// grapheme binder. Nothing here is timed: the counts are the same on every
// machine, compiler and build type, as the solver's are (work_test.cpp).
//
// Every case is for one board mode, and says which in its tags and its title.
// The standard board is the default; the phonics board is a parent's switch
// (docs/design/performance.md, "What was measured"). A figure for one is not a
// figure for the other.
//
// What a failure means, and how to move a budget: docs/testing.md,
// "Performance tests". The numbers are in budgets.hpp.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "budgets.hpp"
#include "check.hpp"
#include "corpus.hpp"
#include "measure.hpp"
#include "metrics.hpp"
#include "wjs/binder_work.hpp"
#include "wjs/dice.hpp"
#include "wjs/solver_work.hpp"

namespace {

using wjs::perf::check_within_budget;
using wjs::perf::skip_unless_optimised;

}  // namespace

// --- standard board: dice ---------------------------------------------------

TEST_CASE("standard board: a roll and a solve visit no more than their budget",
          "[perf][dice][standard]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::dice_board_metrics());
}

TEST_CASE("standard board: every solution over the rolled boards is the one it was",
          "[perf][dice][standard]") {
    skip_unless_optimised();
    const std::uint64_t digest = wjs::perf::dice_digest();
    INFO("digest " << digest << "; if the dice or the roll were meant to change, set kDiceDigest");
    CHECK(digest == wjs::perf::budget::kDiceDigest);
}

TEST_CASE("standard board: the dice make the boards of their own size", "[perf][dice][standard]") {
    skip_unless_optimised();
    // A set of the wrong size would roll a board of another size, and every
    // figure above would be about that one.
    for (const std::size_t size : wjs::perf::kSizes) {
        const wjs::Board board = wjs::perf::dice_board(size, 0);
        INFO("size " << size);
        CHECK(wjs::perf::dice_set(size).side() == size);
        CHECK(board.rows() == size);
        CHECK(board.columns() == size);
    }
}

// --- phonics board: seeding -------------------------------------------------

TEST_CASE("phonics board: seeding spends no more than its budget", "[perf][seeding][phonics]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::seeding_metrics());
}

TEST_CASE("phonics board: every board seeded over the corpus is the one it was",
          "[perf][seeding][phonics]") {
    skip_unless_optimised();
    const std::uint64_t digest = wjs::perf::seeding_digest();
    INFO("digest " << digest
                   << "; if the seeding or the phase were meant to change, set kSeedDigest");
    CHECK(digest == wjs::perf::budget::kSeedDigest);
}

TEST_CASE("phonics board: the solves a seeding reports are the solves the solver counted",
          "[perf][seeding][phonics]") {
    skip_unless_optimised();
    // The budgets hold the seeding's own count of its solves and the solver's
    // visits. This is what says the first is the number of solves the second
    // is the visits of: every solve the seeding spent went through solve().
    const wjs::PhaseIndex& phase = wjs::perf::phonics_phase();
    for (const std::size_t size : wjs::perf::kSizes) {
        for (unsigned index = 0; index < 5; ++index) {
            const unsigned seed = wjs::perf::corpus_seed(size, index);
            const wjs::perf::SeedRun run = wjs::perf::measure_seed(phase, size, seed);
            INFO("size " << size << ", corpus seed " << seed);
            CHECK(run.board.solves == run.solver_solves);
        }
    }
}

// --- phonics board: binding -------------------------------------------------

TEST_CASE("phonics board: binding a path of nine makes no more comparisons than its budget",
          "[perf][binder][phonics]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::binder_metrics());
}

TEST_CASE(
    "phonics board: the binder's tally is the comparisons of its blocks, counted independently",
    "[perf][binder][phonics]") {
    skip_unless_optimised();
    // The budget above is held on the library's own counter. This is what says
    // that counter counts what bind_work() says it does: the comparisons the
    // blocks that came back imply, from the graphemes and the order the binder
    // documents, with nothing the library counts.
    for (const wjs::perf::BindPath& path : wjs::perf::kBindPaths) {
        const std::vector<std::string> tiles = wjs::perf::bind_tiles(path);
        const wjs::perf::BindRun run = wjs::perf::measure_bind(wjs::perf::phonics_binder(), tiles);
        INFO("path " << path.name << ": " << path.tiles);
        CHECK(run.comparisons ==
              wjs::perf::recount_comparisons(wjs::perf::bind_graphemes(), run.blocks));
    }
}

TEST_CASE("phonics board: the three paths bind as they were chosen to", "[perf][binder][phonics]") {
    skip_unless_optimised();
    // "none" has no block of more than a tile, "all" has none of one tile, and
    // "mixed" has both: the paths mean what their names say, so the budgets are
    // for the work they were meant to be for.
    const auto blocks_of = [](const wjs::perf::BindPath& path) {
        return wjs::perf::measure_bind(wjs::perf::phonics_binder(), wjs::perf::bind_tiles(path))
            .blocks;
    };
    const auto none = blocks_of(wjs::perf::kBindPaths[0]);
    const auto all = blocks_of(wjs::perf::kBindPaths[1]);
    const auto mixed = blocks_of(wjs::perf::kBindPaths[2]);

    CHECK(none.size() == 9U);
    for (const auto& block : none) {
        CHECK_FALSE(block.linked());
    }
    CHECK(all.size() == 4U);
    for (const auto& block : all) {
        CHECK(block.linked());
    }
    bool linked = false;
    bool single = false;
    for (const auto& block : mixed) {
        (block.linked() ? linked : single) = true;
    }
    CHECK(linked);
    CHECK(single);
}

// --- phonics board: the game session ----------------------------------------

TEST_CASE("phonics board: a session deals and plays within its budget",
          "[perf][session][phonics]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::session_metrics());
}

TEST_CASE("phonics board: every board a session deals and plays is the one it was",
          "[perf][session][phonics]") {
    skip_unless_optimised();
    const std::uint64_t digest = wjs::perf::session_digest();
    INFO("digest " << digest
                   << "; if the session, the seeding or the phase were meant to change, "
                      "set kSessionDigest");
    CHECK(digest == wjs::perf::budget::kSessionDigest);
}

TEST_CASE("phonics board: the solves a session reports are the solves the solver counted",
          "[perf][session][phonics]") {
    skip_unless_optimised();
    // The budgets hold the deal's own count of its solves next to the solver's
    // visits. This is what says the first is the number of solves the second is
    // the visits of: every solve a deal spent went through solve(), and the
    // deal made no other.
    const wjs::PhaseIndex& phase = wjs::perf::phonics_phase();
    for (const std::size_t size : wjs::perf::kSessionSizes) {
        for (unsigned index = 0; index < 10; ++index) {
            const unsigned seed = wjs::perf::corpus_seed(size, index);
            const wjs::perf::SessionRun run = wjs::perf::measure_session(phase, size, seed);
            INFO("size " << size << ", corpus seed " << seed);
            CHECK(run.deal.solves == run.deal_solves);
            CHECK(run.deal.seedings >= 1U);
            CHECK(run.deal.seedings <= wjs::perf::budget::kSessionMaxSeedings);
            // A board that is not in range is reported as one.
            CHECK(run.deal.in_range == (run.words >= 3U && run.words <= 5U));
            CHECK(run.deal.viable == run.words);
        }
    }
}

// --- standard board: the game session ----------------------------------------

TEST_CASE("standard board: a session deals and plays within its budget",
          "[perf][session][standard]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::standard_session_metrics());
}

TEST_CASE("standard board: every board a session deals and plays is the one it was",
          "[perf][session][standard]") {
    skip_unless_optimised();
    const std::uint64_t digest = wjs::perf::standard_session_digest();
    INFO("digest " << digest
                   << "; if the session, the roll, the dice or the dictionary were meant to "
                      "change, set kStandardSessionDigest");
    CHECK(digest == wjs::perf::budget::kStandardSessionDigest);
}

TEST_CASE("standard board: the solves a session reports are the solves the solver counted",
          "[perf][session][standard]") {
    skip_unless_optimised();
    // The budgets hold the deal's own count of its solves next to the solver's
    // visits. This is what says the first is the number of solves the second is
    // the visits of: every roll the deal made was solved once through solve(), and
    // the deal made no other.
    for (const std::size_t size : wjs::perf::kSizes) {
        for (unsigned index = 0; index < 10; ++index) {
            const unsigned seed = wjs::perf::corpus_seed(size, index);
            const wjs::perf::SessionRun run = wjs::perf::measure_standard_session(size, seed);
            INFO("size " << size << ", corpus seed " << seed);
            CHECK(run.mode == wjs::BoardMode::standard);
            CHECK(run.deal.solves == run.deal_solves);
            CHECK(run.deal.rolls == run.deal.solves);
            CHECK(run.deal.rolls >= 1U);
            CHECK(run.deal.rolls <= wjs::SessionOptions{}.max_rolls);
            // The standard deal seeds nothing, has no range, and keeps the board
            // as soon as it holds a word.
            CHECK(run.deal.seedings == 0U);
            CHECK(run.deal.tries == 0U);
            CHECK(run.deal.in_range);
            CHECK(run.deal.viable == run.words);
            CHECK(run.words >= 1U);
        }
    }
}

TEST_CASE("standard board: a session deals the board the library rolls from the seed that made it",
          "[perf][session][standard]") {
    skip_unless_optimised();
    // The board of a session is wjs::roll of the project's dice with the seed of
    // the roll that held a word, and that is the corpus seed itself unless the first
    // roll held none: so the standard rows above are about the dice rows' boards.
    for (const std::size_t size : wjs::perf::kSizes) {
        for (unsigned index = 0; index < 10; ++index) {
            const unsigned seed = wjs::perf::corpus_seed(size, index);
            const wjs::perf::SessionRun run = wjs::perf::measure_standard_session(size, seed);
            INFO("size " << size << ", corpus seed " << seed);
            const auto rolled = wjs::roll(wjs::perf::dice_set(size), run.deal.seed);
            REQUIRE(rolled.has_value());
            REQUIRE(run.after_first_find.board.has_value());
            std::string tiles;
            for (std::size_t row = 0; row < rolled->rows(); ++row) {
                tiles += row == 0 ? "" : "/";
                for (std::size_t column = 0; column < rolled->columns(); ++column) {
                    tiles += column == 0 ? "" : " ";
                    tiles += rolled->tile({row, column});
                }
            }
            CHECK(run.after_first_find.board->tiles == tiles);
            if (run.deal.rolls == 1U) {
                CHECK(run.deal.seed == seed);
            }
        }
    }
}
