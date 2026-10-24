// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/dice.hpp"
#include "wjs/dice_sets.hpp"

// TEST DATA. Sets of dice written for these tests: every die shows the same six
// letters, so a set is told apart only by how many dice it has. The case about
// the files reads the project's own dice.

namespace {

constexpr std::string_view data_directory = WJS_DATA_DIRECTORY;

wjs::DiceSet dice_of(std::size_t count) {
    std::string text;
    for (std::size_t die = 0; die < count; ++die) {
        text += "a b c d e f\n";
    }
    auto set = wjs::DiceSet::parse(text);
    REQUIRE(set.has_value());
    return *std::move(set);
}

// The set of a board `side` by `side`.
wjs::DiceSet square(std::size_t side) {
    return dice_of(side * side);
}

}  // namespace

TEST_CASE("a set for each side from 3 to 6, in any order, makes the dice of the standard board",
          "[dice_sets]") {
    std::vector<wjs::DiceSet> sets;
    sets.push_back(square(5));
    sets.push_back(square(3));
    sets.push_back(square(6));
    sets.push_back(square(4));

    const auto made = wjs::DiceSets::make(std::move(sets));

    REQUIRE(made.has_value());
    for (std::size_t side = 3; side <= 6; ++side) {
        const wjs::DiceSet* set = made->find(side);
        INFO("side " << side);
        REQUIRE(set != nullptr);
        CHECK(set->side() == side);
        CHECK(set->size() == side * side);
    }
}

TEST_CASE("the sizes the standard board is not played at have no set", "[dice_sets]") {
    std::vector<wjs::DiceSet> sets;
    for (std::size_t side = 3; side <= 6; ++side) {
        sets.push_back(square(side));
    }
    const auto made = wjs::DiceSets::make(std::move(sets));
    REQUIRE(made.has_value());

    for (const std::size_t other : {0U, 1U, 2U, 7U, 8U, 16U}) {
        CHECK(made->find(other) == nullptr);
    }
}

TEST_CASE("sets that are not one for each side are refused", "[dice_sets]") {
    const auto refused = [](std::vector<std::size_t> sides) {
        std::vector<wjs::DiceSet> sets;
        for (const std::size_t side : sides) {
            sets.push_back(square(side));
        }
        return wjs::DiceSets::make(std::move(sets));
    };

    // A side missing, twice, or one the board is not played at.
    const std::vector<std::vector<std::size_t>> cases = {
        {}, {3, 4, 5}, {3, 4, 5, 6, 6}, {3, 3, 5, 6}, {3, 4, 5, 7}, {2, 4, 5, 6}};
    for (const auto& sides : cases) {
        const auto made = refused(sides);
        REQUIRE_FALSE(made.has_value());
        CHECK(made.error() == wjs::DiceError::wrong_count);
    }
}

TEST_CASE("a set whose dice are not a square number makes no board and is refused", "[dice_sets]") {
    std::vector<wjs::DiceSet> sets;
    sets.push_back(square(3));
    sets.push_back(square(4));
    sets.push_back(square(5));
    sets.push_back(dice_of(35));  // not a square

    const auto made = wjs::DiceSets::make(std::move(sets));

    REQUIRE_FALSE(made.has_value());
    CHECK(made.error() == wjs::DiceError::not_square);
}

TEST_CASE("the project's dice directory is a set for each side", "[dice_sets][data]") {
    const auto loaded =
        wjs::DiceSets::load_directory(std::filesystem::path(data_directory) / "dice");

    REQUIRE(loaded.has_value());
    for (std::size_t side = 3; side <= 6; ++side) {
        INFO("side " << side);
        REQUIRE(loaded->find(side) != nullptr);
        CHECK(loaded->find(side)->side() == side);
    }
}

TEST_CASE("a directory without the files is unreadable", "[dice_sets]") {
    const auto loaded = wjs::DiceSets::load_directory(std::filesystem::path(data_directory) /
                                                      "no-such-directory-of-dice");

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == wjs::DiceError::unreadable);
}

namespace {

// A directory of the four files, `swap` of whose names are given each other's dice.
void write_dice_directory(const wjs::test::TempDirectory& dir, bool swap_4_and_5) {
    for (std::size_t side = 3; side <= 6; ++side) {
        std::size_t content = side;
        if (swap_4_and_5 && side == 4) {
            content = 5;
        } else if (swap_4_and_5 && side == 5) {
            content = 4;
        }
        std::string text;
        for (std::size_t die = 0; die < content * content; ++die) {
            text += "a b c d e f\n";
        }
        dir.write(std::to_string(side) + "x" + std::to_string(side) + ".txt", text);
    }
}

}  // namespace

TEST_CASE("a directory of the right files is a set for each side", "[dice_sets]") {
    const wjs::test::TempDirectory dir;
    write_dice_directory(dir, false);

    const auto loaded = wjs::DiceSets::load_directory(dir.path());

    REQUIRE(loaded.has_value());
    CHECK(loaded->find(4)->side() == 4);
}

TEST_CASE("files holding each other's dice are refused, not taken for a set", "[dice_sets]") {
    const wjs::test::TempDirectory dir;
    write_dice_directory(dir, true);  // 4x4.txt holds a board of five and 5x5.txt of four

    const auto loaded = wjs::DiceSets::load_directory(dir.path());

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == wjs::DiceError::wrong_count);
}

TEST_CASE("a file whose dice are not a square is refused by name of the side", "[dice_sets]") {
    const wjs::test::TempDirectory dir;
    write_dice_directory(dir, false);
    dir.write("5x5.txt", "a b c\na b c\na b c\n");

    const auto loaded = wjs::DiceSets::load_directory(dir.path());

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == wjs::DiceError::not_square);
}

TEST_CASE("every dice error says something", "[dice_sets]") {
    for (const wjs::DiceError error :
         {wjs::DiceError::empty, wjs::DiceError::bad_die, wjs::DiceError::bad_face,
          wjs::DiceError::not_square, wjs::DiceError::wrong_count, wjs::DiceError::unreadable}) {
        CHECK_FALSE(wjs::to_string(error).empty());
    }
    // A die of no faces is a blank line, which is skipped; the error is for too many.
    CHECK(wjs::to_string(wjs::DiceError::bad_die).find("no faces") == std::string_view::npos);
    const auto too_many = wjs::DiceSet::parse("a b c d e f g h i j k l m\n");
    REQUIRE_FALSE(too_many.has_value());
    CHECK(too_many.error() == wjs::DiceError::bad_die);
}
