// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/grapheme_binder.hpp"
#include "wjs/phase_data.hpp"

// TEST DATA. The graphemes below were written for these tests, with
// placeholder sounds, so that the binder has something to match. They are not a
// phase of the application and not content for a child; the real graphemes of
// a phase are written and reviewed by a person qualified to do it.

namespace {

std::vector<wjs::Grapheme> graphemes_of(std::initializer_list<std::string_view> letters) {
    std::vector<wjs::Grapheme> graphemes;
    for (const auto text : letters) {
        graphemes.push_back({std::string(text), "/t/", "t", 0});
    }
    return graphemes;
}

// The blocks as text, "R [AI] N": a block of tiles in brackets, a single tile bare.
std::string shown(const wjs::GraphemeBinder& binder, const std::vector<std::string>& tiles) {
    std::string text;
    for (const auto& block : binder.bind(tiles)) {
        if (!text.empty()) {
            text += ' ';
        }
        std::string letters;
        for (std::size_t i = 0; i < block.tiles; ++i) {
            letters += tiles[block.first + i];
        }
        text += block.linked() ? "[" + letters + "]" : letters;
    }
    return text;
}

}  // namespace

TEST_CASE("a vowel team that is traced in order becomes one block", "[binder]") {
    const auto graphemes = graphemes_of({"ai", "ay"});
    const wjs::GraphemeBinder binder(graphemes);

    // The brief's example: R, then A, then I, then N.
    CHECK(shown(binder, {"R", "A", "I", "N"}) == "R [AI] N");
    CHECK(shown(binder, {"P", "L", "A", "Y"}) == "P L [AY]");
}

TEST_CASE("the same letters in the other order are not a block", "[binder]") {
    const auto graphemes = graphemes_of({"ai"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(shown(binder, {"R", "I", "A", "N"}) == "R I A N");
}

TEST_CASE("a grapheme that is not in the phase is not linked", "[binder]") {
    const auto graphemes = graphemes_of({"ai"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(shown(binder, {"S", "H", "I", "P"}) == "S H I P");
}

TEST_CASE("a block grows as the path does", "[binder]") {
    // The design's table: A and I link when I joins, and R stays single.
    const auto graphemes = graphemes_of({"ai"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(shown(binder, {"R"}) == "R");
    CHECK(shown(binder, {"R", "A"}) == "R A");
    CHECK(shown(binder, {"R", "A", "I"}) == "R [AI]");
    CHECK(shown(binder, {"R", "A", "I", "N"}) == "R [AI] N");
}

TEST_CASE("the longest grapheme that fits is taken first", "[binder]") {
    SECTION("a three-letter grapheme over the two-letter one inside it") {
        const auto graphemes = graphemes_of({"ch", "tch"});
        const wjs::GraphemeBinder binder(graphemes);

        CHECK(shown(binder, {"T", "C", "H"}) == "[TCH]");
        CHECK(shown(binder, {"C", "H"}) == "[CH]");
    }

    SECTION("the shorter one when the longer is not there") {
        const auto graphemes = graphemes_of({"ch"});
        const wjs::GraphemeBinder binder(graphemes);

        CHECK(shown(binder, {"T", "C", "H"}) == "T [CH]");
    }

    SECTION("the longer is taken even when the shorter is a grapheme too") {
        const auto graphemes = graphemes_of({"ai", "air"});
        const wjs::GraphemeBinder binder(graphemes);

        CHECK(shown(binder, {"A", "I", "R"}) == "[AIR]");
    }
}

TEST_CASE("the match is greedy from the left, not the best split", "[binder]") {
    // "ai" then R, not A then "ir": the first block is the longest that starts
    // at the first tile.
    const auto graphemes = graphemes_of({"ai", "ir"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(shown(binder, {"A", "I", "R"}) == "[AI] R");
}

TEST_CASE("two blocks may follow one another", "[binder]") {
    const auto graphemes = graphemes_of({"sh", "ai"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(shown(binder, {"S", "H", "A", "I", "N"}) == "[SH] [AI] N");
}

TEST_CASE("a block is whole tiles", "[binder]") {
    SECTION("a grapheme may not start inside a two-letter tile") {
        // Tiles Qu, I, T are the letters q u i t. "ui" would start at the u.
        const auto graphemes = graphemes_of({"ui"});
        const wjs::GraphemeBinder binder(graphemes);

        CHECK(shown(binder, {"QU", "I", "T"}) == "QU I T");
    }

    SECTION("a grapheme may span a two-letter tile and the next") {
        const auto graphemes = graphemes_of({"qui"});
        const wjs::GraphemeBinder binder(graphemes);

        CHECK(shown(binder, {"QU", "I", "T"}) == "[QUI] T");
    }

    SECTION("a two-letter tile that is a grapheme by itself is one tile, not a link") {
        const auto graphemes = graphemes_of({"qu"});
        const wjs::GraphemeBinder binder(graphemes);

        const auto blocks = binder.bind(std::vector<std::string>{"QU", "I", "T"});

        REQUIRE(blocks.size() == 3);
        CHECK_FALSE(blocks[0].linked());
        CHECK(blocks[0].grapheme.empty());
    }
}

TEST_CASE("single letters are not blocks", "[binder]") {
    const auto graphemes = graphemes_of({"a", "i", "ai"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(shown(binder, {"A", "N"}) == "A N");
    CHECK(shown(binder, {"A", "I"}) == "[AI]");
}

TEST_CASE("a split digraph is not found in flight", "[binder][split]") {
    // It cannot be known until its last tile is traced, and the design draws it
    // from the word's authored split, so the binder leaves it out.
    const auto graphemes = graphemes_of({"ai", "a_e"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(shown(binder, {"C", "A", "K", "E"}) == "C A K E");
    CHECK(shown(binder, {"R", "A", "I", "N"}) == "R [AI] N");
}

TEST_CASE("a block says which grapheme it is", "[binder]") {
    const auto graphemes = graphemes_of({"ai"});
    const wjs::GraphemeBinder binder(graphemes);

    const auto blocks = binder.bind(std::vector<std::string>{"R", "A", "I", "N"});

    REQUIRE(blocks.size() == 3);
    CHECK(blocks[1].first == 1);
    CHECK(blocks[1].tiles == 2);
    CHECK(blocks[1].grapheme == "ai");
    CHECK(blocks[0].grapheme.empty());
    CHECK(blocks[2].first == 3);
}

TEST_CASE("an empty path has no blocks and a phase with no digraphs binds nothing", "[binder]") {
    const std::vector<wjs::Grapheme> nothing;
    const wjs::GraphemeBinder none(nothing);
    const auto graphemes = graphemes_of({"ai"});
    const wjs::GraphemeBinder binder(graphemes);

    CHECK(binder.bind(std::vector<std::string>{}).empty());
    CHECK(shown(none, {"R", "A", "I", "N"}) == "R A I N");
}

TEST_CASE("blocks cover the path in order, whatever it holds", "[binder]") {
    const auto graphemes = graphemes_of({"ai", "ay", "sh", "ch", "tch", "air"});
    const wjs::GraphemeBinder binder(graphemes);
    const std::vector<std::string> alphabet = {"A", "I", "R", "S", "H", "C", "T", "Y", "QU"};

    // Every path of up to four tiles over the nine tiles above.
    const std::size_t base = alphabet.size();
    for (std::size_t length = 1; length <= 4; ++length) {
        std::size_t total = 1;
        for (std::size_t i = 0; i < length; ++i) {
            total *= base;
        }
        for (std::size_t code = 0; code < total; ++code) {
            std::vector<std::string> tiles;
            std::size_t rest = code;
            for (std::size_t i = 0; i < length; ++i) {
                tiles.push_back(alphabet[rest % base]);
                rest /= base;
            }

            const auto blocks = binder.bind(tiles);

            std::size_t next = 0;
            for (const auto& block : blocks) {
                REQUIRE(block.first == next);
                REQUIRE(block.tiles >= 1);
                next += block.tiles;
            }
            REQUIRE(next == tiles.size());
        }
    }
}

TEST_CASE("a path on a board is bound by its tiles' letters", "[binder]") {
    const auto board = wjs::Board::parse("R A/N I");
    REQUIRE(board.has_value());
    const auto graphemes = graphemes_of({"ai"});
    const wjs::GraphemeBinder binder(graphemes);

    // R(0,0), A(0,1), I(1,1), N(1,0).
    const std::vector<wjs::Position> path = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    const auto blocks = binder.bind(*board, path);

    REQUIRE(blocks.size() == 3);
    CHECK(blocks[1].linked());
    CHECK(blocks[1].grapheme == "ai");
}

TEST_CASE("a binder is built from the graphemes of a phase", "[binder][phase]") {
    std::istringstream in(
        "# phase: A\n"
        "a  /t1/  clip:t1\n"
        "ai  /t2/  clip:t2\n"
        "i  /t3/  clip:t3\n"
        "n  /t4/  clip:t4\n"
        "r  /t5/  clip:t5\n");
    const auto parsed = wjs::parse_graphemes(in, "g.txt");
    REQUIRE(parsed.has_value());
    const wjs::Dictionary english;
    const auto set = wjs::PhaseSet::build({*parsed}, {}, english);
    REQUIRE(set.has_value());

    const wjs::GraphemeBinder binder(*set->find("A"));

    CHECK(shown(binder, {"R", "A", "I", "N"}) == "R [AI] N");
}
