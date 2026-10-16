#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "wjs/board.hpp"
#include "wjs/deal.hpp"
#include "wjs/dice.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_rules.hpp"

// TEST DATA. The dice and the words below were written for these tests: nine
// dice whose faces are spread over the alphabet so that most rolls hold none of
// a short list of words, and a list of words that is not the game's. The cases
// tagged [data] read the project's own dice and dictionary by their path in the
// checkout, as dice_data_test.cpp does.

namespace {

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

constexpr std::string_view data_directory = WJS_DATA_DIRECTORY;

wjs::DiceSet dice_from(std::string_view text) {
    auto set = wjs::DiceSet::parse(text);
    REQUIRE(set.has_value());
    return *std::move(set);
}

wjs::Dictionary dictionary_of(std::initializer_list<std::string_view> words) {
    wjs::Dictionary dictionary;
    for (const std::string_view word : words) {
        REQUIRE(dictionary.insert(word));
    }
    return dictionary;
}

wjs::Dictionary small_dictionary() {
    return dictionary_of({"cab", "dab", "bad", "bed", "fed", "hid", "kid", "lid", "mop", "pot",
                          "tow", "wet", "zap", "yak", "net", "pen", "ten", "oxen"});
}

std::string text_of(const wjs::Board& board) {
    std::string text;
    for (std::size_t row = 0; row < board.rows(); ++row) {
        if (row > 0) {
            text += '/';
        }
        for (std::size_t column = 0; column < board.columns(); ++column) {
            if (column > 0) {
                text += ' ';
            }
            text += board.tile({row, column});
        }
    }
    return text;
}

std::size_t viable_count(const wjs::DiceSet& dice, const wjs::WordSets& words, std::uint32_t seed) {
    auto board = wjs::roll(dice, seed);
    REQUIRE(board.has_value());
    return wjs::viable_words(*board, words).size();
}

// A seed whose roll holds no word of the set and whose next roll holds one, so
// that a deal from it takes two rolls.
std::uint32_t seed_that_needs_a_second_roll(const wjs::DiceSet& dice, const wjs::WordSets& words) {
    for (std::uint32_t seed = 1; seed < 20000; ++seed) {
        if (viable_count(dice, words, seed) == 0 &&
            viable_count(dice, words, wjs::next_deal_seed(seed)) > 0) {
            return seed;
        }
    }
    FAIL("no seed of the first 20000 rolls an empty board and then a board with a word");
}

// The test words are few, so a board that holds one is rare: room to roll.
wjs::DealOptions generous() {
    wjs::DealOptions options;
    options.max_rolls = 1000;
    return options;
}

wjs::DiceSet shipped_dice(std::size_t side) {
    const auto name = std::to_string(side) + "x" + std::to_string(side) + ".txt";
    auto set = wjs::DiceSet::load_file(std::filesystem::path(data_directory) / "dice" / name);
    REQUIRE(set.has_value());
    return *std::move(set);
}

const wjs::Dictionary& english() {
    static const wjs::Dictionary dictionary = [] {
        auto loaded = wjs::Dictionary::load_file(std::filesystem::path(data_directory) /
                                                 "dictionaries" / "en.txt");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return dictionary;
}

}  // namespace

TEST_CASE("the same inputs deal the same board", "[deal]") {
    const auto dice = dice_from(nine_dice);
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        const auto first = wjs::deal_standard_board(dice, words, seed, generous());
        const auto second = wjs::deal_standard_board(dice, words, seed, generous());
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());

        CHECK(text_of(first->board) == text_of(second->board));
        CHECK(first->rolls == second->rolls);
        CHECK(first->seed == second->seed);
        CHECK(first->viable.size() == second->viable.size());
    }
}

TEST_CASE("a board that holds a word is dealt as rolled, in one roll", "[deal]") {
    const auto dice = dice_from(nine_dice);
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    std::optional<std::uint32_t> seed;
    for (std::uint32_t candidate = 1; candidate < 20000 && !seed; ++candidate) {
        if (viable_count(dice, words, candidate) > 0) {
            seed = candidate;
        }
    }
    REQUIRE(seed.has_value());

    const auto dealt = wjs::deal_standard_board(dice, words, *seed, generous());
    REQUIRE(dealt.has_value());
    CHECK(dealt->rolls == 1);
    CHECK(dealt->seed == *seed);
    CHECK(text_of(dealt->board) == text_of(*wjs::roll(dice, *seed)));
}

TEST_CASE("a roll with no word is rolled again", "[deal]") {
    const auto dice = dice_from(nine_dice);
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);
    const std::uint32_t seed = seed_that_needs_a_second_roll(dice, words);

    // The first roll really is empty, and is not what is dealt.
    REQUIRE(viable_count(dice, words, seed) == 0);

    const auto dealt = wjs::deal_standard_board(dice, words, seed, generous());
    REQUIRE(dealt.has_value());
    CHECK(dealt->rolls == 2);
    CHECK(dealt->seed == wjs::next_deal_seed(seed));
    CHECK(!dealt->viable.empty());
    CHECK(text_of(dealt->board) == text_of(*wjs::roll(dice, dealt->seed)));
    CHECK(text_of(dealt->board) != text_of(*wjs::roll(dice, seed)));
}

TEST_CASE("the viable list is the board's own solve", "[deal]") {
    const auto dice = dice_from(nine_dice);
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        const auto dealt = wjs::deal_standard_board(dice, words, seed, generous());
        REQUIRE(dealt.has_value());
        const auto solved = wjs::viable_words(dealt->board, words);

        REQUIRE(dealt->viable.size() == solved.size());
        for (std::size_t i = 0; i < solved.size(); ++i) {
            CHECK(dealt->viable[i].word == solved[i].word);
        }
    }
}

TEST_CASE("min_viable asks for more words and the deal rolls until there are", "[deal]") {
    const auto dice = dice_from(nine_dice);
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    wjs::DealOptions options;
    options.min_viable = 2;
    options.max_rolls = 500;
    std::size_t again = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        const auto dealt = wjs::deal_standard_board(dice, words, seed, options);
        REQUIRE(dealt.has_value());
        CHECK(dealt->viable.size() >= 2);
        if (dealt->rolls > 1) {
            ++again;
        }
    }
    CHECK(again > 0);
}

TEST_CASE("the deal gives up after max_rolls and says so", "[deal]") {
    const auto dice = dice_from(nine_dice);
    const auto nothing = dictionary_of({"zzzzzz"});
    const auto none = wjs::WordSets::standard(nothing);

    for (const std::size_t max_rolls : {1U, 2U, 7U}) {
        wjs::DealOptions options;
        options.max_rolls = max_rolls;
        const auto dealt = wjs::deal_standard_board(dice, none, 5, options);
        REQUIRE(!dealt.has_value());
        CHECK(dealt.error() == wjs::DealError::no_board);
    }

    // The cap is on rolls: a board that comes on the second is not dealt by one.
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);
    const std::uint32_t seed = seed_that_needs_a_second_roll(dice, words);

    wjs::DealOptions capped;
    capped.max_rolls = 1;
    const auto refused = wjs::deal_standard_board(dice, words, seed, capped);
    REQUIRE(!refused.has_value());
    CHECK(refused.error() == wjs::DealError::no_board);

    capped.max_rolls = 2;
    CHECK(wjs::deal_standard_board(dice, words, seed, capped).has_value());
}

TEST_CASE("each option and input that cannot work is refused", "[deal]") {
    const auto dice = dice_from(nine_dice);
    const auto dictionary = small_dictionary();
    const auto words = wjs::WordSets::standard(dictionary);

    wjs::DealOptions no_words;
    no_words.min_viable = 0;
    const auto a = wjs::deal_standard_board(dice, words, 1, no_words);
    REQUIRE(!a.has_value());
    CHECK(a.error() == wjs::DealError::bad_options);

    wjs::DealOptions no_rolls;
    no_rolls.max_rolls = 0;
    const auto b = wjs::deal_standard_board(dice, words, 1, no_rolls);
    REQUIRE(!b.has_value());
    CHECK(b.error() == wjs::DealError::bad_options);

    // Ten dice are not a square.
    const auto ten = dice_from(std::string(nine_dice) + "a b c d e f\n");
    const auto c = wjs::deal_standard_board(ten, words, 1);
    REQUIRE(!c.has_value());
    CHECK(c.error() == wjs::DealError::bad_dice);
}

TEST_CASE("each error has a name", "[deal]") {
    CHECK(wjs::to_string(wjs::DealError::bad_options) == "bad_options");
    CHECK(wjs::to_string(wjs::DealError::bad_dice) == "bad_dice");
    CHECK(wjs::to_string(wjs::DealError::no_board) == "no_board");
}

TEST_CASE("no board dealt from the shipped 3x3 dice is empty", "[deal][data]") {
    const auto dice = shipped_dice(3);
    const auto words = wjs::WordSets::standard(english());

    std::size_t rerolled = 0;
    for (std::uint32_t seed = 1; seed <= 2000; ++seed) {
        const auto dealt = wjs::deal_standard_board(dice, words, seed, generous());
        REQUIRE(dealt.has_value());
        CHECK(!dealt->viable.empty());
        if (dealt->rolls > 1) {
            ++rerolled;
        }
    }
    // About one roll in 26 is empty, so some of 2000 seeds are dealt again.
    CHECK(rerolled > 0);
}

TEST_CASE("the deal is pinned to two seeds", "[deal][data]") {
    const auto dice = shipped_dice(3);
    const auto words = wjs::WordSets::standard(english());

    // next_deal_seed is std::mt19937's first output, which the standard fixes:
    // 3499211612 for the default seed 5489.
    CHECK(wjs::next_deal_seed(5489) == 3499211612U);

    const auto plain = wjs::deal_standard_board(dice, words, 12345);
    REQUIRE(plain.has_value());
    CHECK(plain->rolls == 1);
    CHECK(text_of(plain->board) == "N J L/E H S/S U E");

    // Seed 43 rolls D L M/P C S/G S S, which holds no word of three letters.
    CHECK(text_of(*wjs::roll(dice, 43)) == "D L M/P C S/G S S");
    const auto again = wjs::deal_standard_board(dice, words, 43);
    REQUIRE(again.has_value());
    CHECK(again->rolls == 2);
    CHECK(text_of(again->board) == "S U G/D L S/M G I");
}
