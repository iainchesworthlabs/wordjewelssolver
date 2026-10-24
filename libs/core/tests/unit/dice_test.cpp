// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/dice.hpp"

// TEST DATA. The dice below were written for these tests: nine dice whose faces
// are letters so that each die is told apart, not a set that anyone plays.

namespace {

// Nine dice for a 3x3 board. Die i has the faces of one letter and its two
// neighbours in the alphabet, so a tile's letter says which die it came from.
constexpr std::string_view nine_dice =
    "# nine test dice, one a line\n"
    "a b c d e f\n"
    "g h i j k l\n"
    "m n o p q r\n"
    "s t u v w x\n"
    "y z a b c d\n"
    "e f g h i j\n"
    "k l m n o p\n"
    "q r s t u v\n"
    "w x y z a b\n";

wjs::DiceSet nine() {
    auto set = wjs::DiceSet::parse(nine_dice);
    REQUIRE(set.has_value());
    return *std::move(set);
}

using wjs::test::text_of;

// "die:face" for each tile, row-major, space separated: a placement pin that
// reads as itself when it fails.
std::string placements_text(const wjs::RolledBoard& rolled) {
    std::string text;
    for (const auto& placement : rolled.placements) {
        if (!text.empty()) {
            text += ' ';
        }
        text += std::to_string(placement.die) + ':' + std::to_string(placement.face);
    }
    return text;
}
}  // namespace

TEST_CASE("a set of dice is read one die a line", "[dice]") {
    const auto set = nine();

    CHECK(set.size() == 9);
    CHECK(set.side() == 3);
    CHECK(set.die(0) == std::vector<std::string>{"A", "B", "C", "D", "E", "F"});
    CHECK(set.die(8).back() == "B");
}

TEST_CASE("faces are stored upper-case, may be several letters and may be separated by commas",
          "[dice]") {
    const auto set = wjs::DiceSet::parse("a, Qu, e\r\nQU i o\n");

    REQUIRE(set.has_value());
    CHECK(set->size() == 2);
    CHECK(set->die(0) == std::vector<std::string>{"A", "QU", "E"});
    CHECK(set->die(1) == std::vector<std::string>{"QU", "I", "O"});
}

TEST_CASE("comments, blank lines and a byte-order mark are skipped", "[dice]") {
    const auto set = wjs::DiceSet::parse("\xEF\xBB\xBF# header\n\n  \n# another\na b\n");

    REQUIRE(set.has_value());
    CHECK(set->size() == 1);
}

TEST_CASE("a set that cannot be read is refused with a reason", "[dice]") {
    const auto error_of = [](std::string_view text) {
        const auto set = wjs::DiceSet::parse(text);
        REQUIRE_FALSE(set.has_value());
        return set.error();
    };

    CHECK(error_of("") == wjs::DiceError::empty);
    CHECK(error_of("# only a header\n") == wjs::DiceError::empty);
    CHECK(error_of("a b 7\n") == wjs::DiceError::bad_face);
    CHECK(error_of("a bcde f\n") == wjs::DiceError::bad_face);
    CHECK(error_of("a b c d e f g h i j k l m\n") == wjs::DiceError::bad_die);
}

TEST_CASE("a set with a number of dice that is not a square makes no board", "[dice]") {
    const auto set = wjs::DiceSet::parse("a b\nc d\ne f\n");
    REQUIRE(set.has_value());

    const auto board = wjs::roll(*set, 1);

    CHECK(set->side() == 0);
    REQUIRE_FALSE(board.has_value());
    CHECK(board.error() == wjs::DiceError::not_square);
}

TEST_CASE("a roll puts each die on one tile and shows one of its faces", "[dice]") {
    const auto set = nine();

    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        const auto board = wjs::roll(set, seed);

        INFO("seed " << seed);
        REQUIRE(board.has_value());
        REQUIRE(board->rows() == 3);
        REQUIRE(board->columns() == 3);

        // Every die is used once: collect, for each tile, the dice that could
        // have shown its letter, and check that the tiles can be matched to the
        // dice one to one (a die's faces are in the letters it holds).
        std::vector<std::string> letters;
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                letters.push_back(board->tile({row, column}));
            }
        }
        std::vector<std::size_t> dice(9);
        for (std::size_t i = 0; i < 9; ++i) {
            dice[i] = i;
        }
        bool matched = false;
        do {
            bool all = true;
            for (std::size_t tile = 0; tile < 9 && all; ++tile) {
                const auto& faces = set.die(dice[tile]);
                all = std::ranges::find(faces, letters[tile]) != faces.end();
            }
            matched = all;
        } while (!matched && std::ranges::next_permutation(dice).found);
        CHECK(matched);
    }
}

TEST_CASE("the same set and seed make the same board and other seeds make others", "[dice]") {
    const auto set = nine();

    std::set<std::string> boards;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const auto first = wjs::roll(set, seed);
        const auto second = wjs::roll(set, seed);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(text_of(*first) == text_of(*second));
        boards.insert(text_of(*first));
    }

    CHECK(boards.size() >= 35);
}

TEST_CASE("a seed gives the board it gave when the roll was written", "[dice]") {
    // The roll is std::mt19937 taken modulo and a shuffle of the library's own,
    // so these are the same on MSVC, GCC and Clang. If they change, the roll
    // did, and a recorded board changes with it.
    const auto set = nine();

    CHECK(text_of(*wjs::roll(set, 1)) == "F K G/J U R/X S Y");
    CHECK(text_of(*wjs::roll(set, 2)) == "O J E/L X X/B V B");
}

TEST_CASE("a roll says which die is on each tile and which of its faces is up", "[dice]") {
    const auto set = nine();

    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        const auto rolled = wjs::roll_with_placements(set, seed);

        INFO("seed " << seed);
        REQUIRE(rolled.has_value());
        REQUIRE(rolled->placements.size() == 9);

        // Row-major, and the face that is up is the tile's letters.
        std::set<std::size_t> used;
        for (std::size_t tile = 0; tile < 9; ++tile) {
            const auto& placement = rolled->placements[tile];
            REQUIRE(placement.die < set.size());
            REQUIRE(placement.face < set.die(placement.die).size());
            CHECK(set.die(placement.die)[placement.face] ==
                  rolled->board.tile({tile / 3, tile % 3}));
            used.insert(placement.die);
        }
        // Each die of the set is on one tile.
        CHECK(used.size() == 9);
    }
}

TEST_CASE("roll gives the board that roll_with_placements does, for the same seed", "[dice]") {
    // roll is what the library gave before the placements were returned; a
    // seed that gave a board then gives it still. The two pinned boards of
    // "a seed gives the board it gave when the roll was written" are the same
    // pins, read through the new function.
    const auto set = nine();

    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        const auto board = wjs::roll(set, seed);
        const auto rolled = wjs::roll_with_placements(set, seed);

        INFO("seed " << seed);
        REQUIRE(board.has_value());
        REQUIRE(rolled.has_value());
        CHECK(text_of(*board) == text_of(rolled->board));
    }
    CHECK(text_of(wjs::roll_with_placements(set, 1)->board) == "F K G/J U R/X S Y");
    CHECK(text_of(wjs::roll_with_placements(set, 2)->board) == "O J E/L X X/B V B");
}

TEST_CASE("a seed gives the placements it gave when they were first returned", "[dice]") {
    // The nine dice's faces repeat across dice (die 4 holds Y Z A B C D, die 0
    // holds A B C D E F), so a letter alone does not say its die: this pins the
    // draws, which a board does not. Written die:face for each tile, row-major.
    const auto set = nine();

    CHECK(placements_text(*wjs::roll_with_placements(set, 1)) ==
          "0:5 6:0 5:2 1:3 7:4 2:5 8:1 3:0 4:0");
    CHECK(placements_text(*wjs::roll_with_placements(set, 2)) ==
          "2:2 1:3 5:0 6:1 3:5 8:1 4:3 7:5 0:1");
}

TEST_CASE("a set that makes no board makes no placements", "[dice]") {
    const auto set = wjs::DiceSet::parse("a b\nc d\ne f\n");
    REQUIRE(set.has_value());

    const auto rolled = wjs::roll_with_placements(*set, 1);

    REQUIRE_FALSE(rolled.has_value());
    CHECK(rolled.error() == wjs::DiceError::not_square);
}

TEST_CASE("each face of each die is shown, over many rolls", "[dice]") {
    const auto set = nine();

    std::set<std::string> seen;
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        const auto board = wjs::roll(set, seed);
        REQUIRE(board.has_value());
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                seen.insert(board->tile({row, column}));
            }
        }
    }

    // The nine dice hold every letter of the alphabet between them.
    CHECK(seen.size() == 26);
}

TEST_CASE("a set is read from a file", "[dice]") {
    std::istringstream in{std::string(nine_dice)};

    const auto set = wjs::DiceSet::load(in);

    REQUIRE(set.has_value());
    CHECK(set->size() == 9);

    const auto missing = wjs::DiceSet::load_file("this-file-does-not-exist.txt");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == wjs::DiceError::unreadable);
}

TEST_CASE("each dice error has a message", "[dice]") {
    for (const auto error :
         {wjs::DiceError::empty, wjs::DiceError::bad_die, wjs::DiceError::bad_face,
          wjs::DiceError::not_square, wjs::DiceError::wrong_count, wjs::DiceError::unreadable}) {
        CHECK_FALSE(wjs::to_string(error).empty());
    }
}
