// SPDX-License-Identifier: GPL-3.0-or-later
// Work budgets for the solver and the word list: how many visits its searches
// make over a fixed corpus of boards, how many words they find, and what the
// dictionary holds. Nothing here is timed. The counts are the same on every
// machine, compiler and build type, which is why they can be held close to what
// they are: a change that loses the trie pruning multiplies them by an order of
// magnitude long before a stopwatch notices.
//
// What a failure means, and how to move a budget: docs/testing.md,
// "Performance tests". The numbers are in budgets.hpp.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include "budgets.hpp"
#include "check.hpp"
#include "corpus.hpp"
#include "measure.hpp"
#include "metrics.hpp"
#include "wjs/solver.hpp"

namespace {

using wjs::perf::check_within_budget;
using wjs::perf::skip_unless_optimised;

}  // namespace

TEST_CASE("the dictionary holds the words and nodes it did", "[perf][dictionary]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::dictionary_metrics());
}

TEST_CASE("a solve visits no more than its budget, over the random boards", "[perf][solver]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::random_board_metrics());
}

TEST_CASE("the boards built to hold many words stay within their budgets", "[perf][solver]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::special_board_metrics());
}

TEST_CASE("every solution over the corpus is the one it was", "[perf][solver]") {
    skip_unless_optimised();
    const std::uint64_t digest = wjs::perf::corpus_digest();
    INFO("digest " << digest << "; if the solver's output was meant to change, set kCorpusDigest");
    CHECK(digest == wjs::perf::budget::kCorpusDigest);
}

TEST_CASE("the solver's tally is the walk's visits, counted independently", "[perf][solver]") {
    skip_unless_optimised();
    // The budgets above are held on the library's own counter. This is what
    // says that counter counts what the definition in solver_work.hpp says: a
    // second walk, written out here with nothing but the dictionary's trie.
    const wjs::Dictionary& dictionary = wjs::perf::english();
    for (const std::size_t size : wjs::perf::kSizes) {
        for (unsigned index = 0; index < 5; ++index) {
            const wjs::Board board = wjs::perf::random_board(size, index);
            INFO("corpus seed " << wjs::perf::corpus_seed(size, index));
            CHECK(wjs::perf::measure_solve(board, dictionary).visits ==
                  wjs::perf::recount_visits(board, dictionary));
        }
    }
    for (const wjs::perf::RichBoard& rich : wjs::perf::kRichBoards) {
        const wjs::Board board = wjs::perf::rich_board(rich);
        INFO("board " << rich.name);
        CHECK(wjs::perf::measure_solve(board, dictionary).visits ==
              wjs::perf::recount_visits(board, dictionary));
    }
}

TEST_CASE("the search walks the built boards exactly as it was measured to", "[perf][solver]") {
    skip_unless_optimised();
    // The exact figures the budgets in budgets.hpp sit a quarter above (their
    // comments say "measured"): a work count is the same on every machine and
    // compiler, so a change in the walk's order or its pruning fails here even
    // when it stays inside the quarter of headroom the budgets allow.
    const wjs::Dictionary& dictionary = wjs::perf::english();
    constexpr std::array<std::uint64_t, 4> rich_visits = {2'832, 5'496, 9'443, 12'350};
    for (std::size_t i = 0; i < wjs::perf::kRichBoards.size(); ++i) {
        const wjs::perf::RichBoard& rich = wjs::perf::kRichBoards[i];
        INFO("board " << rich.name);
        CHECK(wjs::perf::measure_solve(wjs::perf::rich_board(rich), dictionary).visits ==
              rich_visits[i]);
    }
    constexpr std::array<std::uint64_t, 2> empty_visits = {100, 256};
    for (std::size_t i = 0; i < wjs::perf::kEmptyBoards.size(); ++i) {
        const wjs::perf::RichBoard& empty = wjs::perf::kEmptyBoards[i];
        INFO("board " << empty.name);
        CHECK(wjs::perf::measure_solve(wjs::perf::rich_board(empty), dictionary).visits ==
              empty_visits[i]);
    }
}

TEST_CASE("the corpus is the same boards every time", "[perf][solver]") {
    skip_unless_optimised();
    // Two builds of one board agree on every tile, so a seed is one board.
    const wjs::Board first = wjs::perf::random_board(4, 17);
    const wjs::Board second = wjs::perf::random_board(4, 17);
    REQUIRE(first.rows() == 4U);
    REQUIRE(first.columns() == 4U);
    for (std::size_t row = 0; row < first.rows(); ++row) {
        for (std::size_t column = 0; column < first.columns(); ++column) {
            CHECK(first.tile({row, column}) == second.tile({row, column}));
        }
    }
}

TEST_CASE("reading a frame does no more work than its budget, over fixed synthetic frames",
          "[perf][scan]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::scan_metrics());
}
