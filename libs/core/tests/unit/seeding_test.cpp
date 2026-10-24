// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/dashboard.hpp"
#include "wjs/path.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/seeding.hpp"
#include "wjs/solver.hpp"

// TEST DATA. A phase of eleven words written for these tests, with
// placeholder sounds. It is not a phase of the application and not content for
// a child; the real phase files have to be written and reviewed by a person
// qualified to do it. The words are ones the product brief itself names.

namespace {

constexpr std::string_view test_graphemes =
    "# phase: A\n"
    "a  /t1/  clip:t1\n"
    "i  /t2/  clip:t2\n"
    "n  /t3/  clip:t3\n"
    "p  /t4/  clip:t4\n"
    "s  /t5/  clip:t5\n"
    "t  /t6/  clip:t6\n";

constexpr std::string_view test_graphemes_b = "# phase: B\nai  /t7/  clip:t7\n";

constexpr std::string_view test_words =
    "# phase: A\n"
    "nip  n i p\n"
    "pan  p a n\n"
    "pat  p a t\n"
    "pin  p i n\n"
    "sat  s a t\n"
    "sit  s i t\n"
    "tap  t a p\n";

constexpr std::string_view test_words_b =
    "# phase: B\n"
    "pain  p ai n\n"
    "paint  p ai n t\n"
    "saint  s ai n t\n"
    "stain  s t ai n\n";

wjs::Dictionary english() {
    return wjs::test::dictionary_of(
        {"nip", "pan", "pat", "pin", "sat", "sit", "tap", "pain", "paint", "saint", "stain"});
}

// The phase set of the test data: A, then B which adds the "ai" grapheme.
wjs::PhaseSet make_set() {
    const auto read_graphemes = [](std::string_view text, std::string_view name) {
        std::istringstream in{std::string(text)};
        auto parsed = wjs::parse_graphemes(in, name);
        REQUIRE(parsed.has_value());
        return *parsed;
    };
    const auto read_words = [](std::string_view text, std::string_view name) {
        std::istringstream in{std::string(text)};
        auto parsed = wjs::parse_words(in, name);
        REQUIRE(parsed.has_value());
        return *parsed;
    };
    const std::vector<wjs::GraphemesFile> graphemes = {read_graphemes(test_graphemes, "a.g"),
                                                       read_graphemes(test_graphemes_b, "b.g")};
    const std::vector<wjs::WordsFile> words = {read_words(test_words, "a.w"),
                                               read_words(test_words_b, "b.w")};
    auto built = wjs::PhaseSet::build(graphemes, words, english());
    REQUIRE(built.has_value());
    return *std::move(built);
}

// The board as text, for comparing two boards: the tiles run together.
std::string text_of(const wjs::Board& board) {
    return wjs::test::text_of(board, "");
}

wjs::SeededBoard seed(const wjs::PhaseIndex& index, const wjs::SeedOptions& options,
                      std::uint32_t seed_value, const wjs::RecentTargets& recent = {}) {
    auto seeded = wjs::seed_board(index, options, recent, seed_value);
    REQUIRE(seeded.has_value());
    return *std::move(seeded);
}

wjs::SeedOptions options_for(std::size_t size, std::size_t dead) {
    wjs::SeedOptions options;
    options.size = size;
    options.dead_cells = dead;
    options.dead_letter = 'X';
    return options;
}

}  // namespace

TEST_CASE("the same inputs make the same board", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    const auto options = options_for(4, 6);

    for (std::uint32_t seed_value = 1; seed_value <= 20; ++seed_value) {
        const auto first = seed(index, options, seed_value);
        const auto second = seed(index, options, seed_value);

        CHECK(text_of(first.board) == text_of(second.board));
        CHECK(first.targets == second.targets);
        CHECK(first.tries == second.tries);
        CHECK(first.fallback == second.fallback);
    }
}

TEST_CASE("different seeds make different boards", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    const auto options = options_for(4, 6);

    std::set<std::string> boards;
    for (std::uint32_t seed_value = 1; seed_value <= 50; ++seed_value) {
        boards.insert(text_of(seed(index, options, seed_value).board));
    }

    CHECK(boards.size() >= 40);
}

TEST_CASE("a seed gives the board it gave when the algorithm was written", "[seeding]") {
    // A board is a function of the seed on every compiler: the generator is
    // std::mt19937 taken modulo and the shuffles are the library's own, so
    // these strings are the same on MSVC, GCC and Clang. If they change, the
    // algorithm did, and replays and recorded boards change with it.
    const auto set = make_set();
    const auto& index = *set.find("B");

    const auto small = seed(index, options_for(3, 3), 1);
    const auto large = seed(index, options_for(4, 6), 2);

    CHECK(text_of(small.board) == "XIX/ANT/PPX");
    CHECK(text_of(large.board) == "XTXS/IPTX/XNXX/SSIT");
}

TEST_CASE("every board holds its targets and a count of words in range", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");

    for (const std::size_t size : {3U, 4U}) {
        const auto options = options_for(size, size == 3 ? 3 : 6);
        for (std::uint32_t seed_value = 1; seed_value <= 100; ++seed_value) {
            const auto board = seed(index, options, seed_value);

            INFO("size " << size << " seed " << seed_value << " board " << text_of(board.board));
            REQUIRE(board.board.rows() == size);
            REQUIRE(board.board.columns() == size);
            CHECK(board.tries >= 1);
            CHECK(board.tries <= options.max_tries);

            // The targets are words of the phase, two or three of them, and
            // each is among the words found.
            CHECK(board.targets.size() >= 1);
            CHECK(board.targets.size() <= 3);
            for (const auto& target : board.targets) {
                REQUIRE(index.find_word(target) != nullptr);
                const bool found = std::ranges::any_of(board.viable, [&](const wjs::FoundWord& f) {
                    std::string lower = f.word;
                    std::ranges::transform(lower, lower.begin(),
                                           [](char c) { return static_cast<char>(c - 'A' + 'a'); });
                    return lower == target;
                });
                CHECK(found);
            }

            // Every viable word is a word of the phase, and each can be traced.
            for (const auto& found : board.viable) {
                CHECK(index.find_word(found.word) != nullptr);
                CHECK(wjs::validate_path(board.board, found.path) == found.word);
            }

            // The count is what the solver says, and it is in the range unless
            // this is the reported fallback.
            const auto solved = wjs::solve(board.board, index.dictionary());
            CHECK(solved.size() == board.viable.size());
            if (!board.fallback) {
                CHECK(board.viable.size() >= options.min_viable);
                CHECK(board.viable.size() <= options.max_viable);
            }
        }
    }
}

TEST_CASE("dead jewels carry a letter no word uses and take no part in a word", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    const auto options = options_for(4, 5);

    for (std::uint32_t seed_value = 1; seed_value <= 50; ++seed_value) {
        const auto board = seed(index, options, seed_value);

        INFO("seed " << seed_value << " board " << text_of(board.board));
        CHECK(board.dead.size() <= 5);

        std::size_t dead_tiles = 0;
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const bool dead = board.board.tile({row, column}) == "X";
                if (dead) {
                    ++dead_tiles;
                }
                const bool listed = std::ranges::any_of(board.dead, [&](const wjs::Position& p) {
                    return p == wjs::Position{row, column};
                });
                CHECK(dead == listed);
                if (!dead) {
                    CHECK(index.alphabet().find(board.board.tile({row, column})[0]) !=
                          std::string::npos);
                }
            }
        }
        CHECK(dead_tiles == board.dead.size());

        for (const auto& found : board.viable) {
            for (const auto& at : found.path) {
                CHECK(board.board.tile(at) != "X");
            }
        }
    }
}

TEST_CASE("a board with no dead jewels has none", "[seeding]") {
    const auto set = make_set();
    const auto board = seed(*set.find("B"), options_for(3, 0), 7);

    CHECK(board.dead.empty());
    CHECK(text_of(board.board).find('X') == std::string::npos);
}

TEST_CASE("fewer dead jewels are made when the words leave no room for them", "[seeding]") {
    const auto set = make_set();

    // Nine tiles and a three-letter word at least: no more than six can be dead.
    const auto board = seed(*set.find("B"), options_for(3, 9), 3);

    CHECK(board.dead.size() <= 6);
    CHECK(board.targets.size() >= 1);
}

TEST_CASE("a board that cannot land in range is the nearest, and says so", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(3, 3);
    options.min_viable = 30;  // more words than the phase has
    options.max_viable = 40;

    const auto board = seed(index, options, 5);

    CHECK(board.fallback);
    CHECK(board.tries == options.max_tries);
    CHECK(board.viable.size() < options.min_viable);
    CHECK(board.targets.size() >= 1);
}

TEST_CASE("with a phase this size nearly every board lands in range without falling back",
          "[seeding]") {
    // Measured, on this data: 100% of 1,000 seeds at both of these settings.
    // The share depends on the size of the phase and on the dead jewels, and
    // this pins only that the mechanism, not a real phase, works as designed.
    const auto set = make_set();
    const auto& index = *set.find("B");

    for (const auto& [size, dead] : {std::pair<std::size_t, std::size_t>{3, 3}, {4, 6}}) {
        const auto options = options_for(size, dead);
        std::size_t fallbacks = 0;
        for (std::uint32_t seed_value = 1; seed_value <= 200; ++seed_value) {
            if (seed(index, options, seed_value).fallback) {
                ++fallbacks;
            }
        }

        INFO("size " << size << " dead " << dead);
        CHECK(fallbacks <= 10);
    }
}

TEST_CASE("changing filler tiles one at a time keeps every guarantee", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 4);
    options.mutations_per_plant = 4;

    for (std::uint32_t seed_value = 1; seed_value <= 100; ++seed_value) {
        const auto board = seed(index, options, seed_value);

        INFO("seed " << seed_value << " board " << text_of(board.board));
        CHECK(board.tries >= 1);
        CHECK(board.tries <= options.max_tries);
        CHECK(board.targets.size() >= 1);
        if (!board.fallback) {
            CHECK(board.viable.size() >= options.min_viable);
            CHECK(board.viable.size() <= options.max_viable);
        }
        CHECK(wjs::solve(board.board, index.dictionary()).size() == board.viable.size());
    }
}

TEST_CASE("the tries are capped at the limit", "[seeding]") {
    const auto set = make_set();
    auto options = options_for(4, 0);
    options.min_viable = 30;
    options.max_viable = 40;

    for (const std::size_t limit : {1U, 2U, 5U, 20U}) {
        options.max_tries = limit;
        CHECK(seed(*set.find("B"), options, 11).tries == limit);
    }
}

TEST_CASE("targets prefer words with a focus sound", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 6);
    options.focus_sounds = {"ai"};  // the grapheme the parent ticked: four words have it

    for (std::uint32_t seed_value = 1; seed_value <= 50; ++seed_value) {
        const auto board = seed(index, options, seed_value);

        INFO("seed " << seed_value);
        CHECK_FALSE(board.focus_unmet);
        for (const auto& target : board.targets) {
            const auto* word = index.find_word(target);
            REQUIRE(word != nullptr);
            CHECK(std::ranges::any_of(word->graphemes, [](const wjs::WordGrapheme& grapheme) {
                return grapheme.letters == "ai";
            }));
        }
    }
}

TEST_CASE("when too few words have a focus sound the rest come from the phase", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("A");  // no "ai" in phase A
    auto options = options_for(4, 6);
    options.focus_sounds = {"ai"};

    const auto board = seed(index, options, 4);

    CHECK(board.focus_unmet);
    CHECK(board.targets.size() >= 1);
}

TEST_CASE("a focus sound that enough words have is met on every board", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("A");
    auto options = options_for(4, 6);
    options.focus_sounds = {"a"};  // which is in pan, pat, sat and tap

    for (std::uint32_t seed_value = 1; seed_value <= 30; ++seed_value) {
        const auto board = seed(index, options, seed_value);

        INFO("seed " << seed_value);
        CHECK_FALSE(board.focus_unmet);
    }
}

TEST_CASE("a focus sound is ticked by its letters, in any case, and not by its slashed sound",
          "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");

    // The parent ticks letters (Settings holds nothing else), so "AI" is "ai".
    auto options = options_for(4, 6);
    options.focus_sounds = {"AI"};
    for (std::uint32_t seed_value = 1; seed_value <= 20; ++seed_value) {
        INFO("seed " << seed_value);
        CHECK_FALSE(seed(index, options, seed_value).focus_unmet);
    }

    // The slashed form is the sound as written in the data, not a thing the
    // parent can tick: it names no grapheme, so it steers nothing.
    options.focus_sounds = {"/t7/"};
    CHECK(seed(index, options, 1).focus_unmet);
}

TEST_CASE("every focus sound the dashboard offers is one that seeding honours", "[seeding]") {
    const auto set = make_set();
    for (const char* phase : {"A", "B"}) {
        const auto& index = *set.find(phase);
        for (const std::string& letters : wjs::focus_sound_choices(index)) {
            auto options = options_for(4, 4);
            options.focus_sounds = {letters};
            INFO("phase " << phase << ", tick " << letters);
            // Four words are enough to land a board of four tiles of any
            // grapheme that three or more words use.
            std::size_t words_with = 0;
            for (const auto& word : index.words()) {
                words_with += std::ranges::any_of(word.graphemes,
                                                  [&](const wjs::WordGrapheme& grapheme) {
                                                      return grapheme.letters == letters;
                                                  })
                                  ? 1U
                                  : 0U;
            }
            if (words_with >= 3) {
                CHECK_FALSE(seed(index, options, 5).focus_unmet);
            }
        }
    }
}

TEST_CASE("recent targets are avoided while there are others", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("A");
    const auto options = options_for(4, 4);

    // Four of the seven words were targets lately: the targets, two or three,
    // come from the other three.
    wjs::RecentTargets recent;
    recent.record({"nip", "pan", "pat", "pin"});
    const std::set<std::string> fresh = {"sat", "sit", "tap"};

    for (std::uint32_t seed_value = 1; seed_value <= 50; ++seed_value) {
        const auto board = seed(index, options, seed_value, recent);

        INFO("seed " << seed_value);
        for (const auto& target : board.targets) {
            CHECK(fresh.contains(target));
        }
    }
}

TEST_CASE("recent targets are used again when there is nothing else", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("A");
    wjs::RecentTargets recent;
    recent.record({"nip", "pan", "pat", "pin", "sat", "sit", "tap"});

    const auto board = seed(index, options_for(4, 4), 9, recent);

    CHECK(board.targets.size() >= 2);
}

TEST_CASE("over many seeds every word of the phase is a target", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    const auto options = options_for(4, 6);

    std::set<std::string> targets;
    std::set<std::string> target_sets;
    for (std::uint32_t seed_value = 1; seed_value <= 300; ++seed_value) {
        const auto board = seed(index, options, seed_value);
        std::string joined;
        for (const auto& target : board.targets) {
            targets.insert(target);
            joined += target + " ";
        }
        target_sets.insert(joined);
    }

    CHECK(targets.size() == index.words().size());
    CHECK(target_sets.size() >= 30);
}

TEST_CASE("the filler can be a chosen set of letters", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 0);
    options.filler = "AAAA";  // only A as filler

    const auto board = seed(index, options, 6);

    // Every tile that is not a letter of a target is an A.
    std::string planted;
    for (const auto& target : board.targets) {
        planted += target;
    }
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const char letter = board.board.tile({row, column})[0];
            const bool in_target = std::ranges::any_of(
                planted, [&](char c) { return static_cast<char>(c - 'a' + 'A') == letter; });
            CHECK((letter == 'A' || in_target));
        }
    }
}

TEST_CASE("options that cannot make a board are refused with a reason", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    const wjs::RecentTargets recent;

    const auto error_of = [&](const wjs::SeedOptions& options) {
        const auto result = wjs::seed_board(index, options, recent, 1);
        REQUIRE_FALSE(result.has_value());
        return result.error();
    };

    SECTION("no tiles") {
        auto options = options_for(0, 0);
        CHECK(error_of(options) == wjs::SeedError::bad_size);
    }

    SECTION("a board larger than the library's") {
        auto options = options_for(17, 0);
        CHECK(error_of(options) == wjs::SeedError::bad_size);
    }

    SECTION("a board too small for any word") {
        auto options = options_for(1, 0);
        CHECK(error_of(options) == wjs::SeedError::empty_index);
    }

    SECTION("a range with its ends the wrong way round") {
        auto options = options_for(3, 0);
        options.min_viable = 5;
        options.max_viable = 3;
        CHECK(error_of(options) == wjs::SeedError::bad_range);
    }

    SECTION("no tries") {
        auto options = options_for(3, 0);
        options.max_tries = 0;
        CHECK(error_of(options) == wjs::SeedError::bad_range);
    }

    SECTION("a dead letter the phase uses") {
        auto options = options_for(3, 2);
        options.dead_letter = 'S';
        CHECK(error_of(options) == wjs::SeedError::bad_dead_letter);
    }

    SECTION("a dead letter that is not a letter") {
        auto options = options_for(3, 2);
        options.dead_letter = '7';
        CHECK(error_of(options) == wjs::SeedError::bad_dead_letter);
    }

    SECTION("a filler that holds the dead letter") {
        auto options = options_for(3, 2);
        options.filler = "AINX";
        CHECK(error_of(options) == wjs::SeedError::bad_filler);
    }

    SECTION("a filler that is not letters") {
        auto options = options_for(3, 0);
        options.filler = "A1";
        CHECK(error_of(options) == wjs::SeedError::bad_filler);
    }
}

TEST_CASE("a dead letter that is never used need not be a good one", "[seeding]") {
    // With no dead jewels the dead letter is not looked at.
    const auto set = make_set();
    auto options = options_for(3, 0);
    options.dead_letter = 'S';

    CHECK(wjs::seed_board(*set.find("B"), options, {}, 1).has_value());
}

TEST_CASE("the recent targets remember the last boards only", "[seeding]") {
    wjs::RecentTargets recent(2);

    recent.record({"sat", "sit"});
    recent.record({"tap"});
    CHECK(recent.boards() == 2);
    CHECK(recent.contains("SAT"));  // case-insensitive
    CHECK(recent.contains("tap"));

    recent.record({"pin"});
    CHECK(recent.boards() == 2);
    CHECK_FALSE(recent.contains("sat"));  // the oldest board is forgotten
    CHECK(recent.contains("tap"));
    CHECK(recent.contains("pin"));
    CHECK_FALSE(recent.contains("nip"));
}

TEST_CASE("each seeding error has a message", "[seeding]") {
    for (const auto error :
         {wjs::SeedError::bad_size, wjs::SeedError::bad_range, wjs::SeedError::empty_index,
          wjs::SeedError::bad_dead_letter, wjs::SeedError::bad_filler}) {
        CHECK_FALSE(wjs::to_string(error).empty());
    }
}

TEST_CASE("a seeded board always holds a word of the phase", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");

    // No board without a word is ever dealt (the author, 2026-10-16). The
    // phonics board plants its targets, so it holds one.
    for (const std::size_t size : {3U, 4U, 5U}) {
        const auto options = options_for(size, size == 3 ? 0 : 2);
        for (std::uint32_t seed_value = 1; seed_value <= 100; ++seed_value) {
            const auto seeded = seed(index, options, seed_value);
            INFO("size " << size << " seed " << seed_value);
            CHECK(!seeded.viable.empty());
        }
    }
}

TEST_CASE("targets can be limited to short words", "[seeding]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 3);
    options.max_target_letters = 3;

    for (std::uint32_t seed_value = 1; seed_value <= 50; ++seed_value) {
        const auto board = seed(index, options, seed_value);

        INFO("seed " << seed_value);
        for (const auto& target : board.targets) {
            CHECK(target.size() <= 3);
        }
    }
}

TEST_CASE("a limit shorter than any word leaves nothing to plant", "[seeding]") {
    const auto set = make_set();
    auto options = options_for(4, 0);
    options.max_target_letters = 2;

    const auto result = wjs::seed_board(*set.find("B"), options, {}, 1);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == wjs::SeedError::empty_index);
}

TEST_CASE("a guided fill keeps the count within the range and the words apart",
          "[seeding][guided]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 0);
    options.guided_fill = true;
    options.max_target_letters = 3;

    std::size_t fallbacks = 0;
    for (std::uint32_t seed_value = 1; seed_value <= 100; ++seed_value) {
        const auto board = seed(index, options, seed_value);

        INFO("seed " << seed_value << " board " << text_of(board.board));
        fallbacks += board.fallback ? 1U : 0U;

        // The fill never takes the count over the top of the range.
        if (!board.fallback) {
            CHECK(board.viable.size() >= options.min_viable);
            CHECK(board.viable.size() <= options.max_viable);
        }
        CHECK(board.targets.size() >= options.min_viable);
        CHECK(wjs::solve(board.board, index.dictionary()).size() == board.viable.size());

        // The dead jewels listed are the tiles with the dead letter, and there may
        // be more than were asked for (none were).
        std::size_t dead_tiles = 0;
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const bool dead = board.board.tile({row, column}) == "X";
                dead_tiles += dead ? 1U : 0U;
                const bool listed = std::ranges::any_of(board.dead, [&](const wjs::Position& p) {
                    return p == wjs::Position{row, column};
                });
                CHECK(dead == listed);
            }
        }
        CHECK(dead_tiles == board.dead.size());
        CHECK(board.solves > board.tries);
    }
    CHECK(fallbacks <= 10);
}

TEST_CASE("a guided fill leaves most tiles as letters", "[seeding][guided]") {
    // Not a claim about a real phase: on this one, with 3-letter targets, fewer
    // than half the tiles are dead jewels, where the random fill needs 12 of 16
    // to make a board of a few words.
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 0);
    options.guided_fill = true;
    options.max_target_letters = 3;

    std::size_t dead_tiles = 0;
    for (std::uint32_t seed_value = 1; seed_value <= 50; ++seed_value) {
        dead_tiles += seed(index, options, seed_value).dead.size();
    }

    CHECK(dead_tiles < 50 * 8);
}

TEST_CASE("a guided fill is a function of the seed like any other", "[seeding][guided]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 0);
    options.guided_fill = true;
    options.max_target_letters = 3;

    for (std::uint32_t seed_value = 1; seed_value <= 10; ++seed_value) {
        const auto first = seed(index, options, seed_value);
        const auto second = seed(index, options, seed_value);

        CHECK(text_of(first.board) == text_of(second.board));
        CHECK(first.solves == second.solves);
    }
}

TEST_CASE("a guided fill needs a dead letter that the phase does not use", "[seeding][guided]") {
    const auto set = make_set();
    auto options = options_for(4, 0);  // no dead jewels asked for
    options.guided_fill = true;
    options.dead_letter = 'S';

    const auto result = wjs::seed_board(*set.find("B"), options, {}, 1);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == wjs::SeedError::bad_dead_letter);
}

TEST_CASE("a guided fill with letters outside the phase leaves no dead jewel",
          "[seeding][guided]") {
    // The "letters not yet taught" filler: letters no word of the phase uses can
    // always be put somewhere, so nothing has to be made dead.
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = options_for(4, 0);
    options.guided_fill = true;
    options.max_target_letters = 3;
    options.filler = "AINPSTBCDFGH";

    for (std::uint32_t seed_value = 1; seed_value <= 30; ++seed_value) {
        const auto board = seed(index, options, seed_value);

        INFO("seed " << seed_value << " board " << text_of(board.board));
        CHECK(board.dead.empty());
    }
}

TEST_CASE("the phonics board options keep one letter out and let the rest in",
          "[seeding][guided]") {
    const auto set = make_set();
    const auto& index = *set.find("B");

    const auto options = wjs::phonics_board_options(index, 4);

    REQUIRE(options.has_value());
    CHECK(options->size == 4);
    CHECK(options->guided_fill);
    CHECK(options->max_target_letters == 3);
    CHECK(options->dead_cells == 0);
    // The phase's letters are AINPST, so the first letter no word uses is B.
    CHECK(options->dead_letter == 'B');
    CHECK(options->filler.size() == 25);
    CHECK(options->filler.find('B') == std::string::npos);
    CHECK(options->filler.find('Z') != std::string::npos);
}

TEST_CASE("the dead jewels are listed in row order when the planted words are already too many",
          "[seeding][guided]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    auto options = wjs::phonics_board_options(index, 3);
    REQUIRE(options.has_value());
    // One word is wanted, and any word may be planted: PAINT holds PAIN, which
    // is two words already, and a fill cannot take a word away. That planting is
    // given up, the board is the nearest and every tile the word left is dead.
    options->max_target_letters = 0;
    options->min_viable = 1;
    options->max_viable = 1;
    options->max_tries = 1;  // the first planting is the board, as the nearest

    std::size_t with_dead = 0;
    for (std::uint32_t seed_value = 1; seed_value <= 200; ++seed_value) {
        const auto board = seed(index, *options, seed_value);

        INFO("seed " << seed_value);
        CHECK(
            std::ranges::is_sorted(board.dead, [](const wjs::Position& a, const wjs::Position& b) {
                return a.row != b.row ? a.row < b.row : a.column < b.column;
            }));
        if (!board.dead.empty()) {
            ++with_dead;
        }
    }
    CHECK(with_dead > 0);  // the path under test was taken
}

TEST_CASE("a phonics board has the phase's letters first and no dead jewels", "[seeding][guided]") {
    const auto set = make_set();
    const auto& index = *set.find("B");
    const auto options = wjs::phonics_board_options(index, 4);
    REQUIRE(options.has_value());

    std::size_t fallbacks = 0;
    std::size_t taught = 0;
    std::size_t tiles = 0;
    for (std::uint32_t seed_value = 1; seed_value <= 100; ++seed_value) {
        const auto board = seed(index, *options, seed_value);

        INFO("seed " << seed_value << " board " << text_of(board.board));
        if (board.fallback) {
            ++fallbacks;
        }
        CHECK(board.dead.empty());
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const char letter = board.board.tile({row, column})[0];
                CHECK(letter != options->dead_letter);
                ++tiles;
                if (index.alphabet().find(letter) != std::string::npos) {
                    ++taught;
                }
            }
        }
        // Every word of the phase on the board is a word of the phase: nothing
        // about an untaught letter can add one.
        for (const auto& found : board.viable) {
            CHECK(index.find_word(found.word) != nullptr);
        }
    }

    CHECK(fallbacks <= 10);
    // The letters of the phase come first, and the others fill what is left.
    CHECK(taught * 10 >= tiles * 4);
}

TEST_CASE("a phase that uses every letter has none to keep out", "[seeding][guided]") {
    std::string graphemes = "# phase: A\n";
    for (char letter = 'a'; letter <= 'z'; ++letter) {
        graphemes += std::string(1, letter) + "  /t/  clip:t\n";
    }
    std::istringstream graphemes_in(graphemes);
    std::istringstream words_in("# phase: A\nabc  a b c\n");
    const auto parsed_graphemes = wjs::parse_graphemes(graphemes_in, "g");
    const auto parsed_words = wjs::parse_words(words_in, "w");
    REQUIRE(parsed_graphemes.has_value());
    REQUIRE(parsed_words.has_value());
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("abc"));
    const auto built = wjs::PhaseSet::build({*parsed_graphemes}, {*parsed_words}, dictionary);
    REQUIRE(built.has_value());

    const auto options = wjs::phonics_board_options(*built->find("A"), 4);

    REQUIRE_FALSE(options.has_value());
    CHECK(options.error() == wjs::SeedError::bad_dead_letter);
}
