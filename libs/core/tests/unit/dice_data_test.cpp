// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/deal.hpp"
#include "wjs/dice.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/solver.hpp"

// The project's own dice, data/dice/<n>x<n>.txt, made by tools/data/make_dice.ps1
// from a fixed Scrabble-style English letter-frequency table. These cases read
// the shipped files and the shipped dictionary by their paths in this checkout,
// as the performance tier does: they are about the data, not about the code.
// That the files are what the tool makes (and not a stale copy) is data.dice,
// in ctest.

namespace {

// data/ in this checkout, as a path CMake passes in.
constexpr std::string_view data_directory = WJS_DATA_DIRECTORY;

[[nodiscard]] bool is_vowel_face(const std::string& face) {
    return face == "A" || face == "E" || face == "I" || face == "O" || face == "U";
}

// The letters the generator keeps to one a die: QU counts as Q.
[[nodiscard]] bool is_rare_face(const std::string& face) {
    if (face.empty()) {
        return false;
    }
    switch (face.front()) {
        case 'J':
        case 'K':
        case 'Q':
        case 'V':
        case 'X':
        case 'Z':
            return true;
        default:
            return false;
    }
}

std::filesystem::path dice_file(std::size_t side) {
    return std::filesystem::path(data_directory) / "dice" /
           (std::to_string(side) + "x" + std::to_string(side) + ".txt");
}

wjs::DiceSet load(std::size_t side) {
    auto set = wjs::DiceSet::load_file(dice_file(side));
    REQUIRE(set.has_value());
    return *std::move(set);
}

const wjs::Dictionary& english() {
    return wjs::test::shipped_english();
}

}  // namespace

TEST_CASE("each shipped set has the dice for its board and six faces to a die", "[dice][data]") {
    for (const std::size_t side : {3U, 4U, 5U, 6U}) {
        const auto set = load(side);

        INFO("side " << side);
        CHECK(set.size() == side * side);
        CHECK(set.side() == side);
        for (std::size_t i = 0; i < set.size(); ++i) {
            const auto& faces = set.die(i);
            CHECK(faces.size() == 6);
            // Six different faces, in letter order, one to three vowels, and no
            // pile of rare letters on one die.
            CHECK(std::set<std::string>(faces.begin(), faces.end()).size() == 6);
            CHECK(std::ranges::is_sorted(faces));
            const auto vowels = std::ranges::count_if(faces, is_vowel_face);
            CHECK(vowels >= 1);
            CHECK(vowels <= 3);
            CHECK(std::ranges::count_if(faces, is_rare_face) <= 1);
        }
    }
}

TEST_CASE("the shipped sets have vowels to play with and no S-heavy board", "[dice][data]") {
    // A board needs vowels, and one that is a fifth S makes easy plurals and
    // little else. The tool deals again until these hold, so a set outside them
    // is stale or hand-edited.
    for (const std::size_t side : {3U, 4U, 5U, 6U}) {
        const auto set = load(side);
        const std::size_t faces = set.size() * 6;

        std::size_t vowels = 0;
        std::size_t s_faces = 0;
        std::set<std::string> distinct;
        for (std::size_t i = 0; i < set.size(); ++i) {
            for (const auto& face : set.die(i)) {
                vowels += is_vowel_face(face) ? 1U : 0U;
                s_faces += face == "S" ? 1U : 0U;
                distinct.insert(face);
            }
        }

        INFO("side " << side << " vowel faces " << vowels << " of " << faces << ", S faces "
                     << s_faces);
        // 30% to 40% vowels, and S at most 8% of the faces. Integer arithmetic
        // so the test says the same thing on every compiler.
        CHECK(vowels * 10 >= faces * 3);
        CHECK(vowels * 10 <= faces * 4);
        CHECK(s_faces * 100 <= faces * 8);
        // A small set cannot hold every letter, but it should be more than a
        // handful of common ones.
        CHECK(distinct.size() >= 20);
    }
}

TEST_CASE("the shipped dice cover the alphabet and keep Q as the tile QU", "[dice][data]") {
    std::set<std::string> first_letters;
    std::size_t qu_faces = 0;

    for (const std::size_t side : {3U, 4U, 5U, 6U}) {
        const auto set = load(side);
        for (std::size_t i = 0; i < set.size(); ++i) {
            for (const auto& face : set.die(i)) {
                REQUIRE_FALSE(face.empty());
                CHECK(face.size() <= 2);
                for (const char letter : face) {
                    CHECK((letter >= 'A' && letter <= 'Z'));
                }
                if (face.front() == 'Q') {
                    CHECK(face == "QU");
                    ++qu_faces;
                } else {
                    CHECK(face.find('Q') == std::string::npos);
                }
                first_letters.insert(std::string(1, face.front()));
            }
        }
    }

    // Across the four sets every letter is somewhere, as the sets are the
    // project's whole dice supply, and there is a QU tile to use it.
    CHECK(first_letters.size() == 26);
    CHECK(qu_faces >= 1);
}

TEST_CASE("the placements of a roll are the faces of the shipped dice that are up",
          "[dice][data]") {
    // The scene draws a die with its six faces and lands it on the face the
    // roll says is up, so for every tile of every board the die and face that
    // are returned must be the tile's letters, and each die is on one tile.
    for (const std::size_t side : {3U, 4U, 5U, 6U}) {
        const auto set = load(side);

        for (std::uint32_t seed = 1; seed <= 300; ++seed) {
            const auto rolled = wjs::roll_with_placements(set, seed);

            INFO("side " << side << " seed " << seed);
            REQUIRE(rolled.has_value());
            REQUIRE(rolled->board.rows() == side);
            REQUIRE(rolled->board.columns() == side);
            REQUIRE(rolled->placements.size() == side * side);

            std::set<std::size_t> used;
            bool all_match = true;
            for (std::size_t tile = 0; tile < side * side; ++tile) {
                const auto& placement = rolled->placements[tile];
                REQUIRE(placement.die < set.size());
                REQUIRE(placement.face < set.die(placement.die).size());
                all_match = all_match && set.die(placement.die)[placement.face] ==
                                             rolled->board.tile({tile / side, tile % side});
                used.insert(placement.die);
            }
            CHECK(all_match);
            CHECK(used.size() == side * side);
        }
    }
}

TEST_CASE("a dealt standard board has the dice that its seed rolls", "[dice][data]") {
    // The deal rolls again until a board holds a word and says the seed of the
    // roll it kept (DealtBoard::seed). The scene is given the dice of that roll,
    // so the roll of that seed must be the dealt board, die for die.
    const auto words = wjs::WordSets::standard(english());
    for (const std::size_t side : {3U, 4U}) {
        const auto set = load(side);

        for (std::uint32_t seed = 1; seed <= 40; ++seed) {
            const auto dealt = wjs::deal_standard_board(set, words, seed);
            REQUIRE(dealt.has_value());
            const auto rolled = wjs::roll_with_placements(set, dealt->seed);
            REQUIRE(rolled.has_value());

            INFO("side " << side << " seed " << seed << " dealt from " << dealt->seed);
            bool same = rolled->board.rows() == dealt->board.rows() &&
                        rolled->board.columns() == dealt->board.columns();
            for (std::size_t tile = 0; tile < side * side && same; ++tile) {
                const wjs::Position at{tile / side, tile % side};
                const auto& placement = rolled->placements[tile];
                same = rolled->board.tile(at) == dealt->board.tile(at) &&
                       set.die(placement.die)[placement.face] == dealt->board.tile(at);
            }
            CHECK(same);
        }
    }
}

TEST_CASE("boards rolled from the shipped dice hold about the words a random board does",
          "[dice][data]") {
    // The design measured boards of letters drawn by the dictionary's letter
    // counts at 31, 120, 254 and 435 words on average, a 3x3 to a 6x6. The
    // Scrabble-style table gives the small boards more vowels and common letters,
    // and every set fewer S faces, so the 5x5 and 6x6 hold fewer plural-heavy
    // dictionary words: about 32, 102, 194 and 322 over 300 boards. These bounds
    // are loose enough to hold if the dice are made again and tight enough to
    // catch a set that has gone wrong (all one letter, no vowels).
    struct Expect {
        std::size_t side;
        double low;
        double high;
        double no_word_share_limit;
    };
    const std::vector<Expect> expected = {{3, 20.0, 45.0, 0.08},
                                          {4, 70.0, 150.0, 0.02},
                                          {5, 180.0, 330.0, 0.01},
                                          {6, 280.0, 600.0, 0.01}};

    for (const auto& expect : expected) {
        const auto set = load(expect.side);

        double total = 0;
        std::size_t without_words = 0;
        constexpr std::uint32_t boards = 300;
        for (std::uint32_t seed = 1; seed <= boards; ++seed) {
            const auto board = wjs::roll(set, seed);
            REQUIRE(board.has_value());
            REQUIRE(board->rows() == expect.side);
            const auto found = wjs::solve(*board, english());
            total += static_cast<double>(found.size());
            if (found.empty()) {
                ++without_words;
            }
        }

        INFO("side " << expect.side << " mean " << total / boards);
        CHECK(total / boards >= expect.low);
        CHECK(total / boards <= expect.high);
        CHECK(static_cast<double>(without_words) / boards <= expect.no_word_share_limit);
    }
}

TEST_CASE("a seed rolls the same board from the shipped dice on every compiler", "[dice][data]") {
    // Pinned, as the seeding's boards are: a roll is std::mt19937 taken modulo and a
    // shuffle of the library's own. The dice files are fixed by the tool's seed,
    // so these change only if the dice or the roll do.
    const auto text = [](const wjs::Board& board) {
        std::string result;
        for (std::size_t row = 0; row < board.rows(); ++row) {
            if (row > 0) {
                result += '/';
            }
            for (std::size_t column = 0; column < board.columns(); ++column) {
                if (column > 0) {
                    result += ' ';
                }
                result += board.tile({row, column});
            }
        }
        return result;
    };

    CHECK(text(*wjs::roll(load(3), 1)) == "T A E/H T Y/D A A");
    CHECK(text(*wjs::roll(load(4), 1)) == "C A T E/S C O D/T L A L/I Y T H");

    // The same boards from the function that returns the placements, which
    // must not have moved a board: a board of today is a board of before.
    CHECK(text(wjs::roll_with_placements(load(3), 1)->board) == "T A E/H T Y/D A A");
    CHECK(text(wjs::roll_with_placements(load(4), 1)->board) == "C A T E/S C O D/T L A L/I Y T H");
}
