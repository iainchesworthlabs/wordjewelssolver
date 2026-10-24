// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/deal.hpp"
#include "wjs/dice.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_rules.hpp"

// TEST DATA. The boards and the words below were written for these tests: a
// board maker that gives a board with no word for most seeds and one with a
// word for some, and a list of two words that is not the game's.

namespace {

constexpr std::string_view data_directory = WJS_DATA_DIRECTORY;

constexpr std::string_view empty_board = "X X X/X X X/X X X";
constexpr std::string_view cat_board = "C A T/X X X/X X X";
constexpr std::string_view both_board = "C A T/D O G/X X X";

using wjs::test::board_of;
using wjs::test::dictionary_of;
using wjs::test::text_of;

wjs::Dictionary small_dictionary() {
    return dictionary_of({"cat", "dog"});
}

// What a seed makes: a function of the seed alone, as a maker has to be. Seeds
// divisible by 10 make a board with CAT, by 15 one with CAT and DOG, the rest
// none, so the deal from a seed that is not takes a number of rolls that the
// tests can find by asking the maker.
wjs::Board maker_board(std::uint32_t seed) {
    if (seed % 15 == 0) {
        return board_of(both_board);
    }
    if (seed % 10 == 0) {
        return board_of(cat_board);
    }
    return board_of(empty_board);
}

wjs::BoardMaker maker() {
    return [](std::uint32_t seed) -> std::expected<wjs::Board, wjs::DealError> {
        return maker_board(seed);
    };
}

std::size_t viable_count(const wjs::WordSets& words, std::uint32_t seed) {
    return wjs::viable_words(maker_board(seed), words).size();
}

}  // namespace

TEST_CASE("the same inputs deal the same board", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);
    wjs::DealOptions options;
    options.max_rolls = 1000;

    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        const auto first = wjs::deal_playable(words, maker(), seed, options);
        const auto second = wjs::deal_playable(words, maker(), seed, options);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());

        CHECK(first->rolls == second->rolls);
        CHECK(first->seed == second->seed);
        CHECK(first->viable.size() == second->viable.size());
    }
}

TEST_CASE("a board that holds a word is dealt in one roll", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    const auto dealt = wjs::deal_playable(words, maker(), 10);
    REQUIRE(dealt.has_value());
    CHECK(dealt->rolls == 1);
    CHECK(dealt->seed == 10);
    REQUIRE(dealt->viable.size() == 1);
    CHECK(dealt->viable[0].word == "CAT");
}

TEST_CASE("a board with no word is made again from the next seed", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    // A seed whose own board is empty and whose next seed's is not.
    std::uint32_t seed = 1;
    while (viable_count(words, seed) != 0 || viable_count(words, wjs::next_deal_seed(seed)) == 0) {
        REQUIRE(++seed < 100000);
    }

    std::vector<std::uint32_t> made;
    const wjs::BoardMaker recording =
        [&made](std::uint32_t s) -> std::expected<wjs::Board, wjs::DealError> {
        made.push_back(s);
        return maker_board(s);
    };

    const auto dealt = wjs::deal_playable(words, recording, seed);
    REQUIRE(dealt.has_value());
    CHECK(dealt->rolls == 2);
    CHECK(dealt->seed == wjs::next_deal_seed(seed));
    CHECK(!dealt->viable.empty());
    REQUIRE(made.size() == 2);
    CHECK(made[0] == seed);
    CHECK(made[1] == wjs::next_deal_seed(seed));
}

TEST_CASE("the viable list is the board's own solve", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    const auto dealt = wjs::deal_playable(words, maker(), 30);
    REQUIRE(dealt.has_value());
    const auto solved = wjs::viable_words(dealt->board, words);

    REQUIRE(dealt->viable.size() == 2);
    REQUIRE(solved.size() == 2);
    for (std::size_t i = 0; i < solved.size(); ++i) {
        CHECK(dealt->viable[i].word == solved[i].word);
    }
}

TEST_CASE("min_viable asks for more words and the deal makes boards until there are", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    wjs::DealOptions options;
    options.min_viable = 2;
    options.max_rolls = 1000;

    // Seed 10 holds one word, so it is not enough, and the deal moves on.
    const auto dealt = wjs::deal_playable(words, maker(), 10, options);
    REQUIRE(dealt.has_value());
    CHECK(dealt->viable.size() >= 2);
    CHECK(dealt->rolls > 1);
    CHECK(dealt->seed != 10);
}

TEST_CASE("the deal gives up after max_rolls and says so", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);
    const wjs::BoardMaker always_empty =
        [](std::uint32_t) -> std::expected<wjs::Board, wjs::DealError> {
        return board_of(empty_board);
    };

    for (const std::size_t max_rolls : {1U, 2U, 7U}) {
        std::size_t made = 0;
        const wjs::BoardMaker counting = [&](std::uint32_t seed) {
            ++made;
            return always_empty(seed);
        };
        wjs::DealOptions options;
        options.max_rolls = max_rolls;
        const auto dealt = wjs::deal_playable(words, counting, 5, options);
        REQUIRE(!dealt.has_value());
        CHECK(dealt.error() == wjs::DealError::no_board);
        CHECK(made == max_rolls);
    }

    // The cap is on boards: one that comes on the second is not dealt by one.
    std::uint32_t seed = 1;
    while (viable_count(words, seed) != 0 || viable_count(words, wjs::next_deal_seed(seed)) == 0) {
        REQUIRE(++seed < 100000);
    }
    wjs::DealOptions capped;
    capped.max_rolls = 1;
    const auto refused = wjs::deal_playable(words, maker(), seed, capped);
    REQUIRE(!refused.has_value());
    CHECK(refused.error() == wjs::DealError::no_board);

    capped.max_rolls = 2;
    CHECK(wjs::deal_playable(words, maker(), seed, capped).has_value());
}

TEST_CASE("each option and maker that cannot work is refused", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    wjs::DealOptions no_words;
    no_words.min_viable = 0;
    const auto a = wjs::deal_playable(words, maker(), 10, no_words);
    REQUIRE(!a.has_value());
    CHECK(a.error() == wjs::DealError::bad_options);

    wjs::DealOptions no_rolls;
    no_rolls.max_rolls = 0;
    const auto b = wjs::deal_playable(words, maker(), 10, no_rolls);
    REQUIRE(!b.has_value());
    CHECK(b.error() == wjs::DealError::bad_options);

    const auto c = wjs::deal_playable(words, wjs::BoardMaker{}, 10);
    REQUIRE(!c.has_value());
    CHECK(c.error() == wjs::DealError::bad_options);

    // A maker that cannot make a board ends the deal, which is not "no board".
    const wjs::BoardMaker failing = [](std::uint32_t) -> std::expected<wjs::Board, wjs::DealError> {
        return std::unexpected(wjs::DealError::bad_board);
    };
    const auto d = wjs::deal_playable(words, failing, 10);
    REQUIRE(!d.has_value());
    CHECK(d.error() == wjs::DealError::bad_board);
}

TEST_CASE("the words that count are the active set's", "[deal]") {
    // A real word that is not today's does not make a board dealt.
    const auto language = small_dictionary();
    const auto todays = dictionary_of({"dog"});
    const wjs::WordSets words{language, todays};

    wjs::DealOptions options;
    options.max_rolls = 1000;
    const auto dealt = wjs::deal_playable(words, maker(), 10, options);
    REQUIRE(dealt.has_value());
    // 10 holds CAT only, which is not in the active set: so it was made again.
    CHECK(dealt->rolls > 1);
    for (const auto& found : dealt->viable) {
        CHECK(found.word == "DOG");
    }
}

TEST_CASE("next_deal_seed is the first output of std::mt19937", "[deal]") {
    // Fixed by the standard: 3499211612 for the default seed, 5489.
    CHECK(wjs::next_deal_seed(5489) == 3499211612U);
    CHECK(wjs::next_deal_seed(1) != wjs::next_deal_seed(2));
}

TEST_CASE("each error has a name", "[deal]") {
    CHECK(wjs::to_string(wjs::DealError::bad_options) == "bad_options");
    CHECK(wjs::to_string(wjs::DealError::bad_board) == "bad_board");
    CHECK(wjs::to_string(wjs::DealError::no_board) == "no_board");
}

// ---------------------------------------------------------------------------
// The standard board: the same rule over wjs::roll and the project's own dice

namespace {

wjs::DiceSet shipped_dice(std::size_t side) {
    const auto name = std::to_string(side) + "x" + std::to_string(side) + ".txt";
    auto set = wjs::DiceSet::load_file(std::filesystem::path(data_directory) / "dice" / name);
    REQUIRE(set.has_value());
    return *std::move(set);
}

const wjs::Dictionary& english() {
    return wjs::test::shipped_english();
}

}  // namespace

TEST_CASE("no board dealt from the shipped 3x3 dice is empty", "[deal][data]") {
    const auto dice = shipped_dice(3);
    const auto words = wjs::WordSets::standard(english());

    std::size_t rerolled = 0;
    for (std::uint32_t seed = 1; seed <= 2000; ++seed) {
        const auto dealt = wjs::deal_standard_board(dice, words, seed);
        REQUIRE(dealt.has_value());
        CHECK(!dealt->viable.empty());
        if (dealt->rolls > 1) {
            ++rerolled;
        }
    }
    // About one roll in 26 is empty, so some of 2000 seeds are dealt again.
    CHECK(rerolled > 0);
}

TEST_CASE("the standard deal is pinned to two seeds", "[deal][data]") {
    const auto dice = shipped_dice(3);
    const auto words = wjs::WordSets::standard(english());

    const auto plain = wjs::deal_standard_board(dice, words, 12345);
    REQUIRE(plain.has_value());
    CHECK(plain->rolls == 1);
    CHECK(text_of(plain->board) == "O L R/D O U/W L B");

    // Seed 190 rolls P N T/C N K/D N D, which holds no word of three letters.
    CHECK(text_of(*wjs::roll(dice, 190)) == "P N T/C N K/D N D");
    const auto again = wjs::deal_standard_board(dice, words, 190);
    REQUIRE(again.has_value());
    CHECK(again->rolls == 2);
    CHECK(text_of(again->board) == "T E R/F S Y/G T D");
}

TEST_CASE("the standard deal refuses dice that make no board", "[deal]") {
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);
    // Ten dice are not a square.
    std::string text;
    for (int die = 0; die < 10; ++die) {
        text += "a b c d e f\n";
    }
    auto ten = wjs::DiceSet::parse(text);
    REQUIRE(ten.has_value());

    const auto dealt = wjs::deal_standard_board(*ten, words, 1);
    REQUIRE(!dealt.has_value());
    CHECK(dealt.error() == wjs::DealError::bad_board);
}
