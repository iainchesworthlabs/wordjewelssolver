// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/solver.hpp"
#include "wjs/solver_work.hpp"

namespace {

using wjs::test::board_of;
using wjs::test::dictionary_of;

wjs::Dictionary make_dictionary(std::initializer_list<std::string_view> words) {
    return dictionary_of(words);
}

wjs::Board make_board(std::string_view text) {
    return board_of(text);
}

std::vector<std::string> words_of(const std::vector<wjs::FoundWord>& found) {
    std::vector<std::string> words;
    for (const auto& entry : found) {
        words.push_back(entry.word);
    }
    return words;
}

}  // namespace

// The board used by the first cases:
//
//     C A T
//     O R E
//     D G S
//
// CATS and REST are in the dictionary but cannot be traced: the S is not next
// to the T, and the T is not next to the S that follows the E.
TEST_CASE("a word is found only if its letters touch", "[solver]") {
    const auto board = make_board("CAT/ORE/DGS");
    const auto dictionary = make_dictionary(
        {"CAT", "CAR", "CARE", "ARE", "TAR", "TEA", "DOG", "COD", "ROD", "CATS", "REST", "AT"});

    const auto found = wjs::solve(board, dictionary);

    // Highest score first, then longest, then alphabetical; AT is below the
    // default minimum of three letters.
    const std::vector<std::string> expected = {"CARE", "ARE", "CAR", "CAT", "COD",
                                               "DOG",  "ROD", "TAR", "TEA"};
    CHECK(words_of(found) == expected);
}

TEST_CASE("the words come highest score first, then the longer, then alphabetical", "[solver]") {
    //     S T A
    //     E R L
    //     D N O
    //
    // STARED (six tiles, score 3) is traced S T A R E D; STARE scores 2; STAR,
    // ARE, RED and TAR score 1, the four-letter one first.
    const auto board = make_board("STA/ERL/DNO");
    const auto dictionary = make_dictionary({"TAR", "RED", "ARE", "STAR", "STARE", "STARED"});

    const auto found = wjs::solve(board, dictionary);

    const std::vector<std::string> expected = {"STARED", "STARE", "STAR", "ARE", "RED", "TAR"};
    CHECK(words_of(found) == expected);
    REQUIRE(found.size() == 6U);
    CHECK(found[0].score == 3);
    CHECK(found[1].score == 2);
    CHECK(found[2].score == 1);
    CHECK(found[0].score > found[1].score);
    CHECK(found[1].score > found[2].score);
}

TEST_CASE("the order holds in the other scheme too: scores never rise down the list", "[solver]") {
    const auto board = make_board("STA/ERL/DNO");
    const auto dictionary = make_dictionary({"TAR", "RED", "ARE", "STAR", "STARE", "STARED"});
    wjs::SolveOptions options;
    options.scoring = wjs::ScoringScheme::letter_rarity;

    const auto found = wjs::solve(board, dictionary, options);

    REQUIRE(found.size() == 6U);
    for (std::size_t i = 1; i < found.size(); ++i) {
        INFO(found[i - 1].word << " before " << found[i].word);
        CHECK(found[i - 1].score >= found[i].score);
        if (found[i - 1].score == found[i].score) {
            CHECK(found[i - 1].word.size() >= found[i].word.size());
        }
    }
}

TEST_CASE("the minimum length can be lowered", "[solver]") {
    const auto board = make_board("CAT/ORE/DGS");
    const auto dictionary = make_dictionary({"CAT", "AT"});

    wjs::SolveOptions options;
    options.min_length = 2;
    const auto found = wjs::solve(board, dictionary, options);

    const std::vector<std::string> expected = {"CAT", "AT"};
    CHECK(words_of(found) == expected);
}

TEST_CASE("each word reports the tiles that spell it", "[solver]") {
    const auto board = make_board("CAT/ORE/DGS");
    const auto dictionary = make_dictionary({"CARE"});

    const auto found = wjs::solve(board, dictionary);

    REQUIRE(found.size() == 1U);
    const std::vector<wjs::Position> expected = {{0, 0}, {0, 1}, {1, 1}, {1, 2}};
    CHECK(found.front().path == expected);
    CHECK(found.front().score == 1);
}

TEST_CASE("a tile is not used twice in one word", "[solver]") {
    const auto board = make_board("AB/CD");
    const auto dictionary = make_dictionary({"ABA", "ABC"});

    const auto found = wjs::solve(board, dictionary);

    const std::vector<std::string> expected = {"ABC"};
    CHECK(words_of(found) == expected);
}

TEST_CASE("a word that can be traced several ways is reported once", "[solver]") {
    const auto board = make_board("AA/AA");
    const auto dictionary = make_dictionary({"AAA"});

    const auto found = wjs::solve(board, dictionary);

    CHECK(found.size() == 1U);
}

TEST_CASE("the Qu tile spends two letters of the word on one tile", "[solver]") {
    const auto board = make_board("Qu I T");
    const auto dictionary = make_dictionary({"QUIT", "QIT"});

    const auto found = wjs::solve(board, dictionary);

    REQUIRE(found.size() == 1U);
    CHECK(found.front().word == "QUIT");
    CHECK(found.front().path.size() == 3U);
    CHECK(found.front().score == 1);
}

TEST_CASE("the scoring scheme is applied to what is found", "[solver]") {
    const auto board = make_board("CAT/ORE/DGS");
    const auto dictionary = make_dictionary({"CAT"});

    wjs::SolveOptions options;
    options.scoring = wjs::ScoringScheme::letter_rarity;
    const auto found = wjs::solve(board, dictionary, options);

    REQUIRE(found.size() == 1U);
    // (100/2.71 + 100/8.12 + 100/9.10) / 3 = 20.07, truncated.
    CHECK(found.front().score == 20);
}

TEST_CASE("an empty dictionary finds nothing", "[solver]") {
    const auto board = make_board("CAT/ORE/DGS");

    CHECK(wjs::solve(board, wjs::Dictionary{}).empty());
}

// The work counter (solver_work.hpp) that libs/core/bench's budgets are held on. A
// visit is a tile step that stays inside the dictionary's trie.
TEST_CASE("the solver tallies the visits of each solve on this thread", "[solver]") {
    const auto board = make_board("AB");
    const auto dictionary = make_dictionary({"AB"});
    wjs::SolveOptions two_letters;
    two_letters.min_length = 2;

    wjs::reset_solve_work();
    CHECK(wjs::solve_work().solves == 0U);

    // Start on A: A is in the trie (1 visit), then B from A (2). Start on B:
    // no word begins with B, so the step is refused and is not a visit.
    const auto found = wjs::solve(board, dictionary, two_letters);
    REQUIRE(found.size() == 1U);
    CHECK(wjs::solve_work().solves == 1U);
    CHECK(wjs::solve_work().visits == 2U);
    CHECK(wjs::solve_work().largest == 2U);

    // A second solve adds to the totals; the largest is a maximum, not a sum.
    (void)wjs::solve(make_board("A"), dictionary, two_letters);
    CHECK(wjs::solve_work().solves == 2U);
    CHECK(wjs::solve_work().visits == 3U);
    CHECK(wjs::solve_work().largest == 2U);

    wjs::reset_solve_work();
    CHECK(wjs::solve_work().solves == 0U);
    CHECK(wjs::solve_work().visits == 0U);
    CHECK(wjs::solve_work().largest == 0U);
}

TEST_CASE("the solver's tally belongs to the thread that solved", "[solver]") {
    const auto board = make_board("AB");
    const auto dictionary = make_dictionary({"AB"});

    wjs::reset_solve_work();
    std::uint64_t solved_there = 0;
    std::thread worker([&] {
        (void)wjs::solve(board, dictionary);
        solved_there = wjs::solve_work().solves;
    });
    worker.join();

    CHECK(solved_there == 1U);
    CHECK(wjs::solve_work().solves == 0U);
}

TEST_CASE("a tile of three letters spends all three on one visit", "[solver]") {
    // "QUI" is one tile, so QUIT is four letters over two tiles: one step
    // through the trie per tile, one visit per tile, and a path of two.
    const auto board = make_board("QUI T");
    const auto dictionary = make_dictionary({"QUIT", "QIT"});

    wjs::reset_solve_work();
    const auto found = wjs::solve(board, dictionary);

    REQUIRE(found.size() == 1U);
    CHECK(found.front().word == "QUIT");
    CHECK(found.front().path == std::vector<wjs::Position>{{0, 0}, {0, 1}});
    CHECK(found.front().score == 1);
    CHECK(wjs::solve_work().visits == 2U);
}

TEST_CASE("a word may use every tile of the board", "[solver]") {
    // The word traces the whole 4x4 snake: the walk has one choice at every
    // tile, so a visit per tile, and the path reported is the whole board. The
    // first path found is pinned tile for tile, which is what the performance
    // tier's digests pin over its whole corpus.
    const auto board = make_board("ABCD/HGFE/IJKL/PONM");
    const auto dictionary = make_dictionary({"ABCDEFGHIJKLMNOP"});

    wjs::reset_solve_work();
    const auto found = wjs::solve(board, dictionary);

    REQUIRE(found.size() == 1U);
    CHECK(found.front().word == "ABCDEFGHIJKLMNOP");
    const std::vector<wjs::Position> expected = {
        {0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 3}, {1, 2}, {1, 1}, {1, 0},
        {2, 0}, {2, 1}, {2, 2}, {2, 3}, {3, 3}, {3, 2}, {3, 1}, {3, 0},
    };
    CHECK(found.front().path == expected);
    CHECK(found.front().score == 11);
    CHECK(wjs::solve_work().visits == 16U);
}
