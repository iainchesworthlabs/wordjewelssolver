// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/path.hpp"
#include "wjs/solver.hpp"

namespace {

wjs::Board make_board(std::string_view text) {
    return wjs::test::board_of(text);
}

}  // namespace

// The board used throughout, as in the solver's cases:
//
//     C A T
//     O R E
//     D G S
TEST_CASE("a path spells the letters of its tiles in order", "[path]") {
    const auto board = make_board("CAT/ORE/DGS");

    SECTION("along a row") {
        const std::vector<wjs::Position> path = {{0, 0}, {0, 1}, {0, 2}};
        CHECK(wjs::validate_path(board, path) == "CAT");
    }

    SECTION("with a diagonal step") {
        // C, then R below and to the right, then E beside it.
        const std::vector<wjs::Position> path = {{0, 0}, {1, 1}, {1, 2}};
        CHECK(wjs::validate_path(board, path) == "CRE");
    }

    SECTION("a single tile is a path") {
        const std::vector<wjs::Position> path = {{2, 1}};
        CHECK(wjs::validate_path(board, path) == "G");
    }

    SECTION("going back up the board is as good as going down") {
        const std::vector<wjs::Position> path = {{2, 2}, {1, 2}, {0, 2}, {0, 1}};
        CHECK(wjs::validate_path(board, path) == "SETA");
    }
}

TEST_CASE("a tile with two letters adds both to the word", "[path]") {
    const auto board = make_board("Qu I/T E");
    const std::vector<wjs::Position> path = {{0, 0}, {0, 1}, {1, 0}};

    CHECK(wjs::validate_path(board, path) == "QUIT");
}

TEST_CASE("a path of no tiles is refused", "[path]") {
    const auto board = make_board("CAT/ORE/DGS");
    const std::vector<wjs::Position> path;

    const auto result = wjs::validate_path(board, path);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == wjs::PathError::empty);
}

TEST_CASE("tiles that do not touch are refused", "[path]") {
    const auto board = make_board("CAT/ORE/DGS");

    const auto check = [&](std::initializer_list<wjs::Position> tiles) {
        const std::vector<wjs::Position> path(tiles);
        const auto result = wjs::validate_path(board, path);
        REQUIRE_FALSE(result.has_value());
        return result.error();
    };

    SECTION("a gap of one tile") {
        CHECK(check({{0, 0}, {0, 2}}) == wjs::PathError::not_adjacent);
    }

    SECTION("opposite corners") {
        CHECK(check({{0, 0}, {2, 2}}) == wjs::PathError::not_adjacent);
    }

    SECTION("the end of one row and the start of the next do not wrap") {
        CHECK(check({{0, 2}, {1, 0}}) == wjs::PathError::not_adjacent);
    }

    SECTION("a fault later in the path is found") {
        CHECK(check({{0, 0}, {0, 1}, {0, 2}, {2, 0}}) == wjs::PathError::not_adjacent);
    }
}

TEST_CASE("a tile used twice is refused", "[path]") {
    const auto board = make_board("CAT/ORE/DGS");

    const auto check = [&](std::initializer_list<wjs::Position> tiles) {
        const std::vector<wjs::Position> path(tiles);
        const auto result = wjs::validate_path(board, path);
        REQUIRE_FALSE(result.has_value());
        return result.error();
    };

    SECTION("the same tile twice in a row") {
        CHECK(check({{0, 0}, {0, 0}}) == wjs::PathError::repeated_tile);
    }

    SECTION("stepping back onto the tile before") {
        CHECK(check({{0, 0}, {0, 1}, {0, 0}}) == wjs::PathError::repeated_tile);
    }

    SECTION("coming round to the start of a loop") {
        CHECK(check({{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}) == wjs::PathError::repeated_tile);
    }
}

TEST_CASE("a tile off the board is refused", "[path]") {
    const auto board = make_board("CAT/ORE/DGS");

    const auto check = [&](std::initializer_list<wjs::Position> tiles) {
        const std::vector<wjs::Position> path(tiles);
        const auto result = wjs::validate_path(board, path);
        REQUIRE_FALSE(result.has_value());
        return result.error();
    };

    SECTION("below the last row") {
        CHECK(check({{2, 0}, {3, 0}}) == wjs::PathError::off_board);
    }

    SECTION("beyond the last column") {
        CHECK(check({{0, 2}, {0, 3}}) == wjs::PathError::off_board);
    }

    SECTION("as the first tile") {
        CHECK(check({{5, 5}}) == wjs::PathError::off_board);
    }
}

TEST_CASE("each fault has a message", "[path]") {
    for (const auto error : {wjs::PathError::empty, wjs::PathError::off_board,
                             wjs::PathError::repeated_tile, wjs::PathError::not_adjacent}) {
        CHECK_FALSE(wjs::to_string(error).empty());
    }
}

TEST_CASE("neighbours are the eight tiles around one and not the tile itself", "[path]") {
    const wjs::Position centre{1, 1};

    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            const wjs::Position other{row, column};
            CHECK(wjs::are_neighbours(centre, other) == (other != centre));
        }
    }

    CHECK_FALSE(wjs::are_neighbours({0, 0}, {0, 2}));
    CHECK_FALSE(wjs::are_neighbours({0, 0}, {2, 2}));
}

TEST_CASE("every path the solver reports is a valid path spelling its word", "[path][solver]") {
    const auto board = make_board("CAT/ORE/DGS");
    wjs::Dictionary dictionary;
    for (const std::string_view word : {"CAT", "CAR", "CARE", "ARE", "TAR", "TEA", "DOG", "COD"}) {
        REQUIRE(dictionary.insert(word));
    }

    const auto found = wjs::solve(board, dictionary);

    REQUIRE_FALSE(found.empty());
    for (const auto& entry : found) {
        CHECK(wjs::validate_path(board, entry.path) == entry.word);
    }
}
