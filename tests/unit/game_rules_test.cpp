#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/path.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/solver.hpp"

// TEST DATA. The words below were written for these tests: the ones of the
// phonics index are the product brief's own five early-reader words and two
// like them, and none of it is content for a child.

namespace {

using wjs::Position;

wjs::Board make_board(std::string_view text) {
    auto board = wjs::Board::parse(text);
    REQUIRE(board.has_value());
    return *board;
}

wjs::Dictionary dictionary_of(std::initializer_list<std::string_view> words) {
    wjs::Dictionary dictionary;
    for (const std::string_view word : words) {
        REQUIRE(dictionary.insert(word));
    }
    return dictionary;
}

// Every word of the tests' English, which is also the standard board's word set.
wjs::Dictionary english() {
    return dictionary_of({"nip", "pan", "pat", "pin", "sat", "sit", "tap",  // the phonics index
                          "tan", "pit", "sap", "ant",                       // real, not in it
                          "at", "it"});                                     // too short to count
}

// Phase A of the tests: six single-letter graphemes and seven words.
wjs::PhaseSet phase_a(const wjs::Dictionary& dictionary) {
    std::istringstream graphemes_in(
        "# phase: A\n"
        "a  /t1/  clip:t1\n"
        "i  /t2/  clip:t2\n"
        "n  /t3/  clip:t3\n"
        "p  /t4/  clip:t4\n"
        "s  /t5/  clip:t5\n"
        "t  /t6/  clip:t6\n");
    auto graphemes = wjs::parse_graphemes(graphemes_in, "phase-a.graphemes.txt");
    REQUIRE(graphemes.has_value());

    std::istringstream words_in(
        "# phase: A\n"
        "nip  n i p\n"
        "pan  p a n\n"
        "pat  p a t\n"
        "pin  p i n\n"
        "sat  s a t\n"
        "sit  s i t\n"
        "tap  t a p\n");
    auto words = wjs::parse_words(words_in, "phase-a.words.txt");
    REQUIRE(words.has_value());

    auto set = wjs::PhaseSet::build({*graphemes}, {*words}, dictionary);
    REQUIRE(set.has_value());
    return std::move(*set);
}

// The board of most of the cases:
//
//     S A T
//     P I N
//     T A P
//
// Traced on it: SAT along the top row, SIT by the I in the middle, TAN with a
// diagonal step, SAN, which is no word, and AT, which is too short to be one
// of the game's.
constexpr std::string_view board_text = "SAT/PIN/TAP";

const std::vector<Position> path_sat = {{0, 0}, {0, 1}, {0, 2}};
const std::vector<Position> path_sit = {{0, 0}, {1, 1}, {0, 2}};
const std::vector<Position> path_tan = {{0, 2}, {0, 1}, {1, 2}};
const std::vector<Position> path_san = {{0, 0}, {0, 1}, {1, 2}};
const std::vector<Position> path_at = {{0, 1}, {0, 2}};

wjs::Classification classify(const wjs::Board& board, const std::vector<Position>& path,
                             const wjs::WordSets& words) {
    return wjs::classify_path(board, path, words);
}

// Calls visit(path) for every path of one or more touching tiles, no tile
// twice: every path a finger could trace on the board.
template <typename Visit>
void extend_paths(const wjs::Board& board, std::vector<Position>& path, std::vector<bool>& used,
                  Visit& visit) {
    visit(std::span<const Position>(path));
    const Position last = path.back();
    for (std::size_t row = 0; row < board.rows(); ++row) {
        for (std::size_t column = 0; column < board.columns(); ++column) {
            const Position next{row, column};
            const std::size_t index = row * board.columns() + column;
            if (used[index] || !wjs::are_neighbours(last, next)) {
                continue;
            }
            used[index] = true;
            path.push_back(next);
            extend_paths(board, path, used, visit);
            path.pop_back();
            used[index] = false;
        }
    }
}

template <typename Visit>
void for_each_path(const wjs::Board& board, Visit visit) {
    std::vector<bool> used(board.rows() * board.columns(), false);
    std::vector<Position> path;
    for (std::size_t row = 0; row < board.rows(); ++row) {
        for (std::size_t column = 0; column < board.columns(); ++column) {
            used[row * board.columns() + column] = true;
            path = {Position{row, column}};
            extend_paths(board, path, used, visit);
            used[row * board.columns() + column] = false;
        }
    }
}

// A word the solver "found", written by hand: the rules look at the word and
// at the tile its path starts on, and nothing else of it. The path runs along
// a row from the given tile, so it is as long as the word has tiles.
wjs::FoundWord along(std::string word, std::size_t row, std::size_t column) {
    wjs::FoundWord result;
    for (std::size_t i = 0; i < word.size(); ++i) {
        result.path.push_back(Position{row, column + i});
    }
    result.word = std::move(word);
    return result;
}

// As above for a word spelled by tiles of more than one letter: the path has
// as many positions as the tiles it is given.
wjs::FoundWord along_tiles(std::string word, std::size_t tiles, std::size_t row,
                           std::size_t column) {
    wjs::FoundWord result;
    for (std::size_t i = 0; i < tiles; ++i) {
        result.path.push_back(Position{row, column + i});
    }
    result.word = std::move(word);
    return result;
}

std::optional<std::string> helped_word(const std::vector<wjs::FoundWord>& viable,
                                       const wjs::FoundWords& found) {
    const auto help = wjs::start_tile_help(viable, found);
    if (!help) {
        return std::nullopt;
    }
    return help->word;
}

}  // namespace

// --- 1. what a traced path is ------------------------------------------------

TEST_CASE("on the standard board every word of the dictionary counts", "[rules]") {
    const auto dictionary = english();
    const auto words = wjs::WordSets::standard(dictionary);
    const auto board = make_board(board_text);

    SECTION("a word the phonics index also has") {
        const auto result = classify(board, path_sat, words);
        CHECK(result.state == wjs::WordState::viable);
        CHECK(result.word == "SAT");
        CHECK_FALSE(result.fault.has_value());
    }

    SECTION("a word only the dictionary has is viable too") {
        const auto result = classify(board, path_tan, words);
        CHECK(result.state == wjs::WordState::viable);
        CHECK(result.word == "TAN");
    }

    SECTION("a string of letters that is no word") {
        const auto result = classify(board, path_san, words);
        CHECK(result.state == wjs::WordState::not_a_word);
        CHECK(result.word.empty());
        CHECK_FALSE(result.fault.has_value());
    }
}

TEST_CASE("on the phonics board only the phase's words count", "[rules]") {
    const auto dictionary = english();
    const auto set = phase_a(dictionary);
    const auto* index = set.find("A");
    REQUIRE(index != nullptr);
    const auto words = wjs::WordSets::phonics(dictionary, *index);
    const auto board = make_board(board_text);

    SECTION("a word of the index is viable") {
        const auto result = classify(board, path_sit, words);
        CHECK(result.state == wjs::WordState::viable);
        CHECK(result.word == "SIT");
    }

    SECTION("a dictionary word that is not in the index is real, not today's") {
        const auto result = classify(board, path_tan, words);
        CHECK(result.state == wjs::WordState::real_not_todays);
    }

    SECTION("a string of letters that is no word") {
        CHECK(classify(board, path_san, words).state == wjs::WordState::not_a_word);
    }
}

TEST_CASE("a word that is not today's is never handed back", "[rules]") {
    // The design neither shows nor reads a real word that is not one of
    // today's, so the answer carries no word to show.
    const auto dictionary = english();
    const auto set = phase_a(dictionary);
    const auto words = wjs::WordSets::phonics(dictionary, *set.find("A"));
    const auto board = make_board(board_text);

    CHECK(classify(board, path_tan, words).word.empty());
    CHECK(classify(board, path_san, words).word.empty());
}

TEST_CASE("a word of fewer than three letters is not a word, however real", "[rules]") {
    const auto dictionary = english();
    const auto board = make_board(board_text);
    REQUIRE(dictionary.contains("AT"));

    SECTION("when the dictionary is the word set") {
        const auto words = wjs::WordSets::standard(dictionary);
        const auto result = classify(board, path_at, words);
        CHECK(result.state == wjs::WordState::not_a_word);
        CHECK(result.word.empty());
    }

    SECTION("even when the active set holds it") {
        const auto active = dictionary_of({"at", "sat"});
        const wjs::WordSets words{dictionary, active};
        CHECK(classify(board, path_at, words).state == wjs::WordState::not_a_word);
    }

    SECTION("a single tile") {
        const auto words = wjs::WordSets::standard(dictionary);
        const std::vector<Position> one = {{0, 0}};
        CHECK(classify(board, one, words).state == wjs::WordState::not_a_word);
    }
}

TEST_CASE("a path that is not a path is not a word, and says why", "[rules]") {
    const auto dictionary = english();
    const auto words = wjs::WordSets::standard(dictionary);
    const auto board = make_board(board_text);

    const auto fault_of = [&](const std::vector<Position>& path) {
        const auto result = classify(board, path, words);
        CHECK(result.state == wjs::WordState::not_a_word);
        CHECK(result.word.empty());
        return result.fault;
    };

    CHECK(fault_of({}) == wjs::PathError::empty);
    CHECK(fault_of({{0, 0}, {0, 3}}) == wjs::PathError::off_board);
    CHECK(fault_of({{0, 0}, {0, 1}, {0, 0}}) == wjs::PathError::repeated_tile);
    CHECK(fault_of({{0, 0}, {0, 2}}) == wjs::PathError::not_adjacent);
}

TEST_CASE("a Qu tile counts as two letters in the length of a word", "[rules]") {
    const auto dictionary = dictionary_of({"quit", "qua"});
    const auto words = wjs::WordSets::standard(dictionary);
    const auto board = make_board("Qu I/T A");

    SECTION("three tiles, four letters") {
        const std::vector<Position> path = {{0, 0}, {0, 1}, {1, 0}};
        const auto result = classify(board, path, words);
        CHECK(result.state == wjs::WordState::viable);
        CHECK(result.word == "QUIT");
        CHECK(result.word.size() == 4);
    }

    SECTION("two tiles are enough for a three-letter word") {
        const std::vector<Position> path = {{0, 0}, {1, 1}};
        const auto result = classify(board, path, words);
        CHECK(result.state == wjs::WordState::viable);
        CHECK(result.word == "QUA");
    }

    SECTION("a lone Qu is two letters, below the minimum") {
        const std::vector<Position> path = {{0, 0}};
        CHECK(classify(board, path, words).state == wjs::WordState::not_a_word);
    }
}

TEST_CASE("viable words are the active words of three letters or more on the board", "[rules]") {
    const auto dictionary = english();
    const auto board = make_board(board_text);

    SECTION("the standard board's") {
        const auto words = wjs::WordSets::standard(dictionary);
        const auto viable = wjs::viable_words(board, words);

        std::unordered_set<std::string> spelled;
        for (const auto& found : viable) {
            spelled.insert(found.word);
        }
        // Every word of the test English that can be traced, and neither
        // two-letter word, though both can be.
        CHECK(spelled == std::unordered_set<std::string>{"NIP", "PAN", "PAT", "PIN", "SAT", "SIT",
                                                         "TAP", "TAN", "PIT", "SAP", "ANT"});
    }

    SECTION("the phonics board's") {
        const auto set = phase_a(dictionary);
        const auto words = wjs::WordSets::phonics(dictionary, *set.find("A"));
        const auto viable = wjs::viable_words(board, words);

        std::unordered_set<std::string> spelled;
        for (const auto& found : viable) {
            spelled.insert(found.word);
        }
        CHECK(spelled ==
              std::unordered_set<std::string>{"NIP", "PAN", "PAT", "PIN", "SAT", "SIT", "TAP"});
    }

    SECTION("a short word in the active set is passed over") {
        const auto active = dictionary_of({"at", "sat"});
        const wjs::WordSets words{dictionary, active};
        const auto viable = wjs::viable_words(board, words);
        REQUIRE(viable.size() == 1);
        CHECK(viable.front().word == "SAT");
    }
}

TEST_CASE("every path classifies the way the solver says", "[rules]") {
    // Exhaustive: every path a finger could trace on the board, against both
    // word sets. A path is viable exactly when the solver finds its word.
    const auto dictionary = english();
    const auto set = phase_a(dictionary);
    const auto board = make_board(board_text);
    const auto standard = wjs::WordSets::standard(dictionary);
    const auto phonics = wjs::WordSets::phonics(dictionary, *set.find("A"));

    for (const wjs::WordSets* words : {&standard, &phonics}) {
        std::unordered_set<std::string> solver_words;
        for (const auto& found : wjs::viable_words(board, *words)) {
            solver_words.insert(found.word);
        }

        std::size_t paths = 0;
        std::size_t viable = 0;
        std::size_t real_not_todays = 0;
        std::size_t disagreements = 0;

        for_each_path(board, [&](std::span<const Position> path) {
            ++paths;
            const auto result = wjs::classify_path(board, path, *words);
            const auto spelled = wjs::validate_path(board, path);
            if (!spelled) {
                ++disagreements;
                return;
            }
            const bool solver_has_it = solver_words.contains(*spelled);
            switch (result.state) {
                case wjs::WordState::viable:
                    ++viable;
                    if (!solver_has_it || result.word != *spelled) {
                        ++disagreements;
                    }
                    break;
                case wjs::WordState::real_not_todays:
                    ++real_not_todays;
                    if (solver_has_it || !result.word.empty()) {
                        ++disagreements;
                    }
                    break;
                case wjs::WordState::not_a_word:
                    if (solver_has_it || !result.word.empty()) {
                        ++disagreements;
                    }
                    break;
            }
        });

        CHECK(paths == 10305);  // every simple path of the king graph on 3 by 3
        CHECK(viable > 0);
        CHECK(disagreements == 0);
        if (words == &standard) {
            // The standard board has no middle state.
            CHECK(real_not_todays == 0);
        } else {
            // The phonics board has one, and the case would prove nothing if
            // this board never reached it.
            CHECK(real_not_todays > 0);
        }
    }
}

TEST_CASE("the path the solver reports for a word is a viable path", "[rules]") {
    const auto dictionary = english();
    const auto words = wjs::WordSets::standard(dictionary);
    const auto board = make_board(board_text);

    const auto viable = wjs::viable_words(board, words);
    REQUIRE_FALSE(viable.empty());
    for (const auto& found : viable) {
        const auto result = wjs::classify_path(board, found.path, words);
        CHECK(result.state == wjs::WordState::viable);
        CHECK(result.word == found.word);
    }
}

// --- 2. tiers and stars -------------------------------------------------------

TEST_CASE("a word's tier follows from its letter count", "[rules]") {
    CHECK_FALSE(wjs::tier_for_letters(0).has_value());
    CHECK_FALSE(wjs::tier_for_letters(1).has_value());
    CHECK_FALSE(wjs::tier_for_letters(2).has_value());
    CHECK(wjs::tier_for_letters(3) == wjs::Tier::spark);
    CHECK(wjs::tier_for_letters(4) == wjs::Tier::power);
    CHECK(wjs::tier_for_letters(5) == wjs::Tier::mega);
    CHECK(wjs::tier_for_letters(6) == wjs::Tier::mega);
    CHECK(wjs::tier_for_letters(9) == wjs::Tier::mega);
    CHECK(wjs::tier_for_letters(48) == wjs::Tier::mega);
}

TEST_CASE("each tier earns its stars, and Power a sticker", "[rules]") {
    const auto spark = wjs::reward_for(wjs::Tier::spark);
    CHECK(spark.stars == 1);
    CHECK_FALSE(spark.sticker);

    const auto power = wjs::reward_for(wjs::Tier::power);
    CHECK(power.stars == 2);
    CHECK(power.sticker);

    const auto mega = wjs::reward_for(wjs::Tier::mega);
    CHECK(mega.stars == 5);
    CHECK_FALSE(mega.sticker);
}

TEST_CASE("a Qu word is as long as its letters: QUIT is a Power word", "[rules]") {
    const auto dictionary = dictionary_of({"quit", "quite", "tie"});
    const auto words = wjs::WordSets::standard(dictionary);
    const auto board = make_board("Qu I T/X X E");

    const std::vector<Position> quit = {{0, 0}, {0, 1}, {0, 2}};
    const auto four = classify(board, quit, words);
    REQUIRE(four.state == wjs::WordState::viable);
    CHECK(wjs::tier_for_letters(four.word.size()) == wjs::Tier::power);

    const std::vector<Position> quite = {{0, 0}, {0, 1}, {0, 2}, {1, 2}};
    const auto five = classify(board, quite, words);
    REQUIRE(five.state == wjs::WordState::viable);
    CHECK(wjs::tier_for_letters(five.word.size()) == wjs::Tier::mega);

    // Three tiles that spell three letters are Spark.
    const std::vector<Position> tie = {{0, 2}, {0, 1}, {1, 2}};
    const auto three = classify(board, tie, words);
    REQUIRE(three.state == wjs::WordState::viable);
    CHECK(wjs::tier_for_letters(three.word.size()) == wjs::Tier::spark);
}

TEST_CASE("tiers and states have names to print", "[rules]") {
    CHECK(wjs::to_string(wjs::Tier::spark) == "Spark");
    CHECK(wjs::to_string(wjs::Tier::power) == "Power");
    CHECK(wjs::to_string(wjs::Tier::mega) == "Mega");
    CHECK(wjs::to_string(wjs::WordState::viable) == "viable");
    CHECK_FALSE(wjs::to_string(wjs::WordState::real_not_todays).empty());
    CHECK(wjs::to_string(wjs::WordState::not_a_word) == "not a word");
}

// --- the words found on a board -----------------------------------------------

TEST_CASE("a word is found once, whatever its case", "[rules]") {
    wjs::FoundWords found;
    CHECK(found.empty());

    CHECK(found.insert("SAT"));
    CHECK_FALSE(found.insert("SAT"));
    CHECK_FALSE(found.insert("sat"));
    CHECK(found.insert("Sit"));
    CHECK_FALSE(found.insert(""));

    CHECK(found.size() == 2);
    CHECK(found.contains("sat"));
    CHECK(found.contains("SIT"));
    CHECK_FALSE(found.contains("tap"));
}

TEST_CASE("the words found are kept in the order they were found", "[rules]") {
    wjs::FoundWords found;
    found.insert("tap");
    found.insert("sat");
    found.insert("tap");
    found.insert("pin");

    CHECK(found.in_order() == std::vector<std::string>{"TAP", "SAT", "PIN"});
}

// --- 3. the start-tile help ---------------------------------------------------

TEST_CASE("help names the shortest word first, even from the far corner", "[rules]") {
    // The five-letter word starts in the very corner and the three-letter word
    // is the furthest tile away; the shorter word still wins.
    const std::vector<wjs::FoundWord> viable = {
        along("TAPES", 0, 0),
        along("TAPE", 0, 0),
        along("TAP", 3, 3),
    };

    const auto help = wjs::start_tile_help(viable, wjs::FoundWords{});
    REQUIRE(help.has_value());
    CHECK(help->word == "TAP");
    CHECK(help->tile == Position{3, 3});
}

TEST_CASE("of equal length, help names the word whose first tile is nearest the top left",
          "[rules]") {
    SECTION("distance, not reading order") {
        // (0,3) is first in reading order but three tiles from the corner;
        // (2,0) is two.
        const std::vector<wjs::FoundWord> viable = {
            along("TAP", 0, 3),
            along("CAT", 2, 0),
        };
        const auto help = wjs::start_tile_help(viable, wjs::FoundWords{});
        REQUIRE(help.has_value());
        CHECK(help->word == "CAT");
        CHECK(help->tile == Position{2, 0});
    }

    SECTION("a diagonal is further than it looks") {
        // (1,1) is 2 away and (0,2) is 4 away, though both sum to 2.
        const std::vector<wjs::FoundWord> viable = {
            along("TAP", 0, 2),
            along("CAT", 1, 1),
        };
        CHECK(helped_word(viable, wjs::FoundWords{}) == "CAT");
    }

    SECTION("equally near, the upper tile") {
        const std::vector<wjs::FoundWord> viable = {
            along("CAT", 2, 0),
            along("TAP", 0, 2),
        };
        const auto help = wjs::start_tile_help(viable, wjs::FoundWords{});
        REQUIRE(help.has_value());
        CHECK(help->word == "TAP");
        CHECK(help->tile == Position{0, 2});
    }

    SECTION("the same tile, the word that sorts first") {
        const std::vector<wjs::FoundWord> viable = {
            along("CAT", 1, 1),
            along("CAR", 1, 1),
            along("CAN", 1, 1),
        };
        CHECK(helped_word(viable, wjs::FoundWords{}) == "CAN");
    }
}

TEST_CASE("help counts a word's length in letters, not tiles", "[rules]") {
    // QUIT is three tiles and four letters; TIDE is four tiles and four
    // letters. Counted in tiles QUIT would win; counted in letters they tie and
    // the nearer tile decides.
    const std::vector<wjs::FoundWord> viable = {
        along_tiles("QUIT", 3, 2, 2),
        along("TIDE", 0, 0),
    };

    const auto help = wjs::start_tile_help(viable, wjs::FoundWords{});
    REQUIRE(help.has_value());
    CHECK(help->word == "TIDE");
}

TEST_CASE("help gives the same answer however the words are ordered", "[rules]") {
    std::vector<wjs::FoundWord> viable = {
        along("CAT", 2, 0),  along("TAP", 0, 3),   along("CAR", 2, 0),
        along("TAPE", 0, 0), along("TAPES", 1, 0),
    };
    std::ranges::sort(viable, {}, &wjs::FoundWord::word);

    std::size_t orders = 0;
    do {
        ++orders;
        const auto help = wjs::start_tile_help(viable, wjs::FoundWords{});
        REQUIRE(help.has_value());
        CHECK(help->word == "CAR");
        CHECK(help->tile == Position{2, 0});
    } while (std::ranges::next_permutation(viable, {}, &wjs::FoundWord::word).found);
    CHECK(orders == 120);
}

TEST_CASE("help stays on a word until that word is found", "[rules]") {
    const std::vector<wjs::FoundWord> viable = {
        along("CAT", 2, 0),  along("TAN", 4, 0),   along("TAP", 3, 3),
        along("TAPE", 0, 0), along("TAPES", 0, 0),
    };
    wjs::FoundWords found;

    SECTION("asking again changes nothing") {
        const auto first = wjs::start_tile_help(viable, found);
        const auto again = wjs::start_tile_help(viable, found);
        REQUIRE(first.has_value());
        REQUIRE(again.has_value());
        CHECK(first->word == again->word);
        CHECK(first->tile == again->tile);
    }

    SECTION("finding any other word does not move it") {
        REQUIRE(helped_word(viable, found) == "CAT");
        for (const char* other : {"TAPES", "TAP", "TAPE", "TAN"}) {
            found.insert(other);
            CHECK(helped_word(viable, found) == "CAT");
        }
    }

    SECTION("finding it names the next, until none is left") {
        std::vector<std::string> helped;
        while (const auto help = wjs::start_tile_help(viable, found)) {
            helped.push_back(help->word);
            REQUIRE(found.insert(help->word));
        }
        CHECK(helped == std::vector<std::string>{"CAT", "TAN", "TAP", "TAPE", "TAPES"});
        CHECK_FALSE(wjs::start_tile_help(viable, found).has_value());
    }
}

TEST_CASE("help has nothing to say when nothing is left, or nothing is viable", "[rules]") {
    CHECK_FALSE(wjs::start_tile_help({}, wjs::FoundWords{}).has_value());

    const std::vector<wjs::FoundWord> viable = {along("CAT", 0, 0)};
    wjs::FoundWords found;
    found.insert("cat");
    CHECK_FALSE(wjs::start_tile_help(viable, found).has_value());
}

TEST_CASE("help passes over a word it cannot point at", "[rules]") {
    wjs::FoundWord no_path;
    no_path.word = "ANT";

    const std::vector<wjs::FoundWord> viable = {no_path, along("TAPE", 0, 0)};
    CHECK(helped_word(viable, wjs::FoundWords{}) == "TAPE");
}

TEST_CASE("help points at a tile the word can be traced from", "[rules]") {
    // On a real board, with the solver's own paths: the tile is the first of a
    // path that spells the word, and following help finds every word.
    const auto dictionary = english();
    const auto words = wjs::WordSets::standard(dictionary);
    const auto board = make_board(board_text);
    const auto viable = wjs::viable_words(board, words);
    REQUIRE_FALSE(viable.empty());

    wjs::FoundWords found;
    std::size_t helps = 0;
    while (const auto help = wjs::start_tile_help(viable, found)) {
        ++helps;
        REQUIRE_FALSE(found.contains(help->word));

        const auto entry = std::ranges::find(viable, help->word, &wjs::FoundWord::word);
        REQUIRE(entry != viable.end());
        CHECK(help->tile == entry->path.front());

        // The word really can be started there.
        const auto traced = wjs::classify_path(board, entry->path, words);
        CHECK(traced.state == wjs::WordState::viable);
        CHECK(traced.word == help->word);

        CHECK_FALSE(wjs::is_cleared(viable, found));
        REQUIRE(found.insert(help->word));
    }

    CHECK(helps == viable.size());
    CHECK(wjs::is_cleared(viable, found));
}

// --- 4. whether the board is cleared -----------------------------------------

TEST_CASE("a board is cleared when no viable word is left unfound", "[rules]") {
    const std::vector<wjs::FoundWord> viable = {
        along("SAT", 0, 0),
        along("SIT", 1, 0),
        along("TAP", 2, 0),
    };
    wjs::FoundWords found;

    CHECK_FALSE(wjs::is_cleared(viable, found));

    found.insert("SAT");
    found.insert("sit");
    CHECK_FALSE(wjs::is_cleared(viable, found));

    found.insert("TAP");
    CHECK(wjs::is_cleared(viable, found));
}

TEST_CASE("words found that are not viable do not clear a board", "[rules]") {
    const std::vector<wjs::FoundWord> viable = {along("SAT", 0, 0), along("SIT", 1, 0)};
    wjs::FoundWords found;
    found.insert("SAT");
    found.insert("TAN");
    found.insert("PIN");

    CHECK_FALSE(wjs::is_cleared(viable, found));
    found.insert("SIT");
    CHECK(wjs::is_cleared(viable, found));
}

TEST_CASE("a board with no viable word has none left to find", "[rules]") {
    // The literal reading. The deal is meant never to make such a board (the
    // author, 2026-10-16), so this is only what the question answers for one.
    CHECK(wjs::is_cleared({}, wjs::FoundWords{}));
}
