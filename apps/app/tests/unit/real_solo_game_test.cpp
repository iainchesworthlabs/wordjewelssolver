// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fake_solo_game.hpp"
#include "parent_controller.hpp"
#include "real_solo_game.hpp"
#include "solo_game_factory.hpp"
#include "solo_game_setup.hpp"
#include "solo_model.hpp"
#include "wjs/board.hpp"
#include "wjs/calendar.hpp"
#include "wjs/dice_sets.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_session.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/session_store.hpp"
#include "wjs/solver.hpp"
#include "wjs/store.hpp"

// The Solo Play port over the library's session (apps/app/src/solo/real_solo_game.*):
// a table of the mapping, run against a tiny phase written here. The session's own
// rules are tested in libs/core/tests/unit/game_session_test.cpp; what is held down here is
// that each call of the port is the call of the session it is meant to be, that
// nothing the session keeps back (a word that is not today's) is handed to the
// screens, what is handed to the store, and that a run begins where the last one
// ended (the game in progress, and the size the child chose last).
//
// TEST DATA. Two phases written for these tests (the words of the session's own
// tests, with placeholder sounds) and a word list that holds them and a few real
// words that are in neither. None of it is a phase of the application or content for
// a child; the real phase files have to be written and reviewed by a person
// qualified to do it, and the repository has none.

namespace {

using wjs::BoardSize;
using wjs::Position;
using wjs::SessionState;
using wjs::app::RealSoloGame;
using wjs::app::SoloState;
using wjs::app::WordKind;
using wjs::app::WordTier;

constexpr std::string_view graphemes_a =
    "# phase: A\n"
    "a  /t1/  clip:t1\n"
    "i  /t2/  clip:t2\n"
    "n  /t3/  clip:t3\n"
    "p  /t4/  clip:t4\n"
    "s  /t5/  clip:t5\n"
    "t  /t6/  clip:t6\n";
constexpr std::string_view graphemes_b = "# phase: B\nai  /t7/  clip:t7\n";
constexpr std::string_view words_a =
    "# phase: A\n"
    "nip  n i p\n"
    "pan  p a n\n"
    "pat  p a t\n"
    "pin  p i n\n"
    "sat  s a t\n"
    "sit  s i t\n"
    "tap  t a p\n";
constexpr std::string_view words_b =
    "# phase: B\n"
    "pain  p ai n\n"
    "paint  p ai n t\n"
    "saint  s ai n t\n"
    "stain  s t ai n\n";

// Real words of the word list that no phase has: they must never be named.
constexpr std::array<std::string_view, 9> extra_words = {"tip", "pit", "nap", "ant", "tan",
                                                         "at",  "in",  "it",  "tab"};
constexpr std::array<std::string_view, 11> phase_words = {
    "nip", "pan", "pat", "pin", "sat", "sit", "tap", "pain", "paint", "saint", "stain"};

const wjs::Dictionary& english() {
    static const wjs::Dictionary dictionary = [] {
        wjs::Dictionary words;
        for (const std::string_view word : phase_words) {
            REQUIRE(words.insert(word));
        }
        for (const std::string_view word : extra_words) {
            REQUIRE(words.insert(word));
        }
        return words;
    }();
    return dictionary;
}

const wjs::PhaseSet& phases() {
    static const wjs::PhaseSet set = [] {
        const auto graphemes = [](std::string_view text, std::string_view name) {
            std::istringstream in{std::string(text)};
            auto parsed = wjs::parse_graphemes(in, name);
            REQUIRE(parsed.has_value());
            return *parsed;
        };
        const auto words = [](std::string_view text, std::string_view name) {
            std::istringstream in{std::string(text)};
            auto parsed = wjs::parse_words(in, name);
            REQUIRE(parsed.has_value());
            return *parsed;
        };
        auto built =
            wjs::PhaseSet::build({graphemes(graphemes_a, "a.g"), graphemes(graphemes_b, "b.g")},
                                 {words(words_a, "a.w"), words(words_b, "b.w")}, english());
        REQUIRE(built.has_value());
        return *std::move(built);
    }();
    return set;
}

const wjs::PhaseIndex& phase_a() {
    return *phases().find("A");
}
const wjs::PhaseIndex& phase_b() {
    return *phases().find("B");
}

constexpr wjs::Day today = wjs::day_from_civil(2026, 10, 16);
const auto device_day = [] { return today; };

using Path = std::vector<Position>;

// The board of the cases below, 3 by 3; `B` is the dead letter of both phases (the
// first letter of A-Z that none of their words uses), so the six B tiles are dead
// jewels. The words and one path for each, by hand:
//
//     P A T      PAT    (0,0) (0,1) (0,2)      TAP   (0,2) (0,1) (0,0)
//     B I N      PIN    (0,0) (1,1) (1,2)      NIP   (1,2) (1,1) (0,0)
//     B B B      PAN    (0,0) (0,1) (1,2)
//
// and, in phase B as well, PAIN (0,0) (0,1) (1,1) (1,2) and PAINT, the same and
// then (0,2).
constexpr std::string_view hand_tiles = "P A T/B I N/B B B";
const Path pat{{0, 0}, {0, 1}, {0, 2}};
const Path tap{{0, 2}, {0, 1}, {0, 0}};
const Path pin{{0, 0}, {1, 1}, {1, 2}};
const Path nip{{1, 2}, {1, 1}, {0, 0}};
const Path pan{{0, 0}, {0, 1}, {1, 2}};
const Path pain{{0, 0}, {0, 1}, {1, 1}, {1, 2}};
const Path paint{{0, 0}, {0, 1}, {1, 1}, {1, 2}, {0, 2}};
// Real words of the word list that are not today's, and some that are not words.
const Path tip{{0, 2}, {1, 1}, {0, 0}};  // TIP
const Path at{{0, 1}, {0, 2}};           // AT, two letters
const Path pab{{0, 0}, {0, 1}, {1, 0}};  // PAB, no word
const Path apart{{0, 0}, {0, 2}};        // not touching: not a path
const Path one_tile{{0, 0}};             // a touch to hear a letter

// The options of a phonics session: the game under test plays that board.
wjs::SessionOptions phonics(wjs::SessionOptions options = {}) {
    options.mode = wjs::BoardMode::phonics;
    return options;
}

wjs::SessionData data_of(const wjs::PhaseIndex& phase) {
    return {.dictionary = english(), .phase = &phase};
}

wjs::BoardRecord hand_record(std::string_view phase, std::vector<wjs::Find> finds = {}) {
    wjs::BoardRecord record;
    record.mode = wjs::BoardMode::phonics;
    record.phase = std::string(phase);
    record.size = BoardSize::three;
    record.tiles = std::string(hand_tiles);
    record.viable =
        phase == "A" ? std::vector<std::string>{"NIP", "PAN", "PAT", "PIN", "TAP"}
                     : std::vector<std::string>{"NIP", "PAIN", "PAINT", "PAN", "PAT", "PIN", "TAP"};
    record.finds = std::move(finds);
    return record;
}

wjs::Find jewel(std::string word) {
    return {.word = std::move(word), .assisted = false, .when = today};
}

const wjs::PhaseIndex& phase_named(std::string_view name) {
    return name == "A" ? phase_a() : phase_b();
}

// A session playing the hand board of `phase`, with `finds` already in its tray.
wjs::GameSession hand_session(std::string_view phase, std::vector<wjs::Find> finds = {},
                              const wjs::SessionOptions& options = {}) {
    wjs::SessionSnapshot snapshot;
    snapshot.state = SessionState::playing;
    snapshot.mode = wjs::BoardMode::phonics;
    snapshot.size = BoardSize::three;
    snapshot.seed = 77;
    snapshot.board = hand_record(phase, std::move(finds));
    auto restored =
        wjs::GameSession::restore(snapshot, data_of(phase_named(phase)), phonics(options));
    REQUIRE(restored.has_value());
    return *std::move(restored);
}

wjs::GameSession dealing_session(const wjs::SessionOptions& options = {}) {
    auto created = wjs::GameSession::create(data_of(phase_a()), phonics(options));
    REQUIRE(created.has_value());
    return *std::move(created);
}

// A session that has dealt a board of its own and is in `state`.
wjs::GameSession reach(SessionState state) {
    wjs::GameSession session = dealing_session();
    if (state == SessionState::box) {
        return session;
    }
    REQUIRE(session.shake().has_value());
    if (state == SessionState::shaking) {
        return session;
    }
    REQUIRE(session.still());
    if (state == SessionState::dropping) {
        return session;
    }
    REQUIRE(session.landed());
    if (state == SessionState::playing) {
        return session;
    }
    for (const wjs::FoundWord& word : wjs::solve(*session.board(), phase_a().dictionary())) {
        static_cast<void>(session.trace(word.path, today));
    }
    REQUIRE(session.state() == SessionState::cleared);
    if (state == SessionState::cleared) {
        return session;
    }
    REQUIRE(session.rest());
    return session;
}

// A game over a store of its own.
struct Table {
    wjs::Store store = wjs::Store::in_memory();
    RealSoloGame game;

    // The hand board of `phase`, in play.
    explicit Table(std::string_view phase, std::vector<wjs::Find> finds = {}, bool help = true)
        : game(hand_session(phase, std::move(finds)), help, &phase_named(phase), store,
               device_day) {}

    // A session already made, over phase A.
    explicit Table(wjs::GameSession session)
        : game(std::move(session), true, &phase_a(), store, device_day) {}
};

std::string lower_case(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::vector<std::string> words_of(const std::vector<wjs::app::SoloFound>& found) {
    std::vector<std::string> words;
    for (const auto& entry : found) {
        words.push_back(entry.word);
    }
    return words;
}

}  // namespace

// --- the box -----------------------------------------------------------------

TEST_CASE("a new game is the closed box with the options' size and help", "[app][real]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::SessionOptions options;
    options.size = BoardSize::four;
    options.help = false;
    auto made = RealSoloGame::create(data_of(phase_a()), store, phonics(options), device_day);
    REQUIRE(made.has_value());
    const RealSoloGame& game = *made;

    CHECK(game.state() == SoloState::box);
    CHECK(game.size() == 4);
    REQUIRE(game.sizes().size() == 2);
    CHECK(game.sizes()[0] == 3);
    CHECK(game.sizes()[1] == 4);
    CHECK_FALSE(game.helpAllowed());
    CHECK(game.board().empty());
    CHECK(game.found().empty());
    CHECK(game.history().empty());
    CHECK_FALSE(game.litTile().has_value());
}

TEST_CASE("a session that cannot be made is the session's error", "[app][real]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::SessionOptions options;
    options.min_viable = 6;
    options.max_viable = 2;
    const auto made = RealSoloGame::create(data_of(phase_a()), store, phonics(options), device_day);
    REQUIRE_FALSE(made.has_value());
    CHECK(made.error() == wjs::SessionError::bad_options);
}

TEST_CASE("the size is chosen in the box and nowhere else", "[app][real]") {
    Table in_box(dealing_session());
    in_box.game.chooseSize(4);
    CHECK(in_box.game.size() == 4);
    in_box.game.chooseSize(5);  // no such size
    CHECK(in_box.game.size() == 4);
    in_box.game.chooseSize(3);
    CHECK(in_box.game.size() == 3);

    Table playing("A");
    playing.game.chooseSize(4);
    CHECK(playing.game.size() == 3);
}

// --- shake, settled, the board -----------------------------------------------

TEST_CASE("the shake deals the board before anything moves, and settled lands it", "[app][real]") {
    Table table(dealing_session());
    RealSoloGame& game = table.game;

    REQUIRE(game.shake());
    CHECK(game.state() == SoloState::shaking);
    const auto& board = game.board();
    CHECK(board.rows == 3);
    CHECK(board.columns == 3);
    REQUIRE(board.tiles.size() == 9);
    // The scene's board is the session's, with a dead jewel as "" and the seed of
    // the seeding that made it.
    const wjs::Board* dealt = game.session().board();
    REQUIRE(dealt != nullptr);
    const auto dead = game.session().dead_tiles();
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            const Position position{row, column};
            const bool is_dead = std::ranges::find(dead, position) != dead.end();
            CHECK(board.tile(position) == (is_dead ? std::string{} : dealt->tile(position)));
        }
    }
    REQUIRE(game.session().last_deal().has_value());
    CHECK(board.seed == game.session().last_deal()->seed);
    CHECK(game.found().empty());

    game.settled();
    CHECK(game.state() == SoloState::playing);
    CHECK(game.session().state() == SessionState::playing);
}

TEST_CASE("settled is the session's still and landed, whichever it is waiting for", "[app][real]") {
    {
        wjs::GameSession session = reach(SessionState::dropping);
        Table table(std::move(session));
        REQUIRE(table.game.state() == SoloState::dropping);
        table.game.settled();
        CHECK(table.game.state() == SoloState::playing);
    }
    {
        Table table(reach(SessionState::playing));
        table.game.settled();  // late, or twice: nothing changes
        CHECK(table.game.state() == SoloState::playing);
    }
    {
        Table table(dealing_session());
        table.game.settled();  // no board yet
        CHECK(table.game.state() == SoloState::box);
    }
}

TEST_CASE("a shake that deals no board answers false and stays in the box", "[app][real]") {
    // The phase plants words of three letters at most; this one has only four.
    const std::string graphemes =
        "# phase: A\na  /x/  clip:a\nb  /x/  clip:b\nc  /x/  clip:c\nd  /x/  clip:d\n";
    const std::string words = "# phase: A\nabcd  a b c d\n";
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("abcd"));
    std::istringstream graphemes_in{graphemes};
    std::istringstream words_in{words};
    const auto built = wjs::PhaseSet::build({*wjs::parse_graphemes(graphemes_in, "g")},
                                            {*wjs::parse_words(words_in, "w")}, dictionary);
    REQUIRE(built.has_value());
    wjs::Store store = wjs::Store::in_memory();
    auto made = RealSoloGame::create({.dictionary = dictionary, .phase = built->find("A")}, store,
                                     phonics(), device_day);
    REQUIRE(made.has_value());

    CHECK_FALSE(made->shake());
    CHECK(made->state() == SoloState::box);
    CHECK(made->board().empty());
}

TEST_CASE("every state of the session is the port's, by the design's names", "[app][real]") {
    struct Row {
        SessionState session;
        SoloState solo;
    };
    const std::array rows = {
        Row{SessionState::box, SoloState::box},
        Row{SessionState::shaking, SoloState::shaking},
        Row{SessionState::dropping, SoloState::dropping},
        Row{SessionState::playing, SoloState::playing},
        Row{SessionState::cleared, SoloState::cleared},
        Row{SessionState::resting, SoloState::resting},
    };
    for (const Row& row : rows) {
        INFO(wjs::to_string(row.session));
        Table table(reach(row.session));
        CHECK(table.game.state() == row.solo);
        // The screens' ids are the design's without the prefix.
        CHECK("solo." + std::string(wjs::app::to_string(row.solo)) ==
              std::string(wjs::to_string(row.session)));
    }
}

TEST_CASE("a game over a session in play shows its board, dead jewels and tray", "[app][real]") {
    Table table("B", {jewel("PAT"), jewel("PAIN")});
    const RealSoloGame& game = table.game;

    CHECK(game.state() == SoloState::playing);
    REQUIRE(game.board().tiles.size() == 9);
    const std::vector<std::string> expected = {"P", "A", "T", "", "I", "N", "", "", ""};
    CHECK(game.board().tiles == expected);
    CHECK(game.board().seed == 0);  // a restored session has dealt nothing
    REQUIRE(game.found().size() == 2);
    CHECK(game.found()[0].word == "pat");
    CHECK(game.found()[0].tier == WordTier::spark);
    CHECK(game.found()[0].stars == 1);
    CHECK(game.found()[1].word == "pain");
    CHECK(game.found()[1].tier == WordTier::power);
    CHECK(game.found()[1].stars == 2);
}

// --- a traced path -----------------------------------------------------------

TEST_CASE("each answer of a trace is the port's, and only a word of today's is named",
          "[app][real]") {
    struct Row {
        std::string_view what;
        const Path* path;
        WordKind kind;
        std::string_view word;  // lower-case, for a word of today's only
        WordTier tier;
        int stars;
        bool already;
        bool sticker;
    };
    // In order, on one board: a word found stays found.
    const std::array rows = {
        Row{"a spark word", &pat, WordKind::viable, "pat", WordTier::spark, 1, false, false},
        Row{"the same word again", &pat, WordKind::viable, "pat", WordTier::spark, 1, true, false},
        Row{"a word the other way round", &tap, WordKind::viable, "tap", WordTier::spark, 1, false,
            false},
        Row{"a power word and its sticker", &pain, WordKind::viable, "pain", WordTier::power, 2,
            false, true},
        Row{"a mega word", &paint, WordKind::viable, "paint", WordTier::mega, 5, false, false},
        Row{"a real word that is not today's", &tip, WordKind::realNotTodays, "", WordTier::spark,
            0, false, false},
        Row{"a real word of two letters", &at, WordKind::realNotTodays, "", WordTier::spark, 0,
            false, false},
        Row{"letters that are no word", &pab, WordKind::notAWord, "", WordTier::spark, 0, false,
            false},
        Row{"tiles that do not touch", &apart, WordKind::notAWord, "", WordTier::spark, 0, false,
            false},
        Row{"a single tile", &one_tile, WordKind::notAWord, "", WordTier::spark, 0, false, false},
    };

    Table table("B");
    for (const Row& row : rows) {
        INFO(row.what);
        const auto outcome = table.game.submit(*row.path);
        CHECK(outcome.kind == row.kind);
        CHECK(outcome.found.word == row.word);
        CHECK(outcome.found.tier == row.tier);
        CHECK(outcome.found.stars == row.stars);
        CHECK(outcome.alreadyFound == row.already);
        CHECK(outcome.sticker == row.sticker);
        CHECK_FALSE(outcome.cleared);
        CHECK_FALSE(outcome.newBest);
    }
    // Only the finds made it to the tray, in the order found.
    CHECK(words_of(table.game.found()) == std::vector<std::string>{"pat", "tap", "pain", "paint"});
}

TEST_CASE("a path is answered not a word, and changes nothing, outside play", "[app][real]") {
    Table table(dealing_session());
    CHECK(table.game.submit(pat).kind == WordKind::notAWord);
    CHECK(table.game.found().empty());
    CHECK(table.store.data().boards.empty());
}

TEST_CASE("the last word clears the board; celebrated rests it and Again closes the box",
          "[app][real]") {
    Table table("A");
    RealSoloGame& game = table.game;

    // The same call out of `playing` does nothing.
    game.celebrated();
    game.again();
    CHECK(game.state() == SoloState::playing);

    for (const Path* path : {&pat, &tap, &pin, &nip}) {
        const auto outcome = game.submit(*path);
        REQUIRE(outcome.kind == WordKind::viable);
        CHECK_FALSE(outcome.cleared);
    }
    const auto last = game.submit(pan);
    CHECK(last.kind == WordKind::viable);
    CHECK(last.cleared);
    // The first board of a size has no best to beat.
    CHECK_FALSE(last.newBest);
    CHECK(game.state() == SoloState::cleared);
    CHECK(game.found().size() == 5);

    game.again();  // the celebration is not over
    CHECK(game.state() == SoloState::cleared);
    game.celebrated();
    CHECK(game.state() == SoloState::resting);
    game.again();
    CHECK(game.state() == SoloState::box);
}

// --- what the store is handed ------------------------------------------------

TEST_CASE("each find is handed to the store with the day", "[app][real]") {
    Table table("A");
    REQUIRE(table.game.submit(pat).kind == WordKind::viable);

    const wjs::StoreData& data = table.store.data();
    REQUIRE(data.boards.size() == 1);
    CHECK(data.boards[0].phase == "A");
    CHECK(data.boards[0].size == 3);
    CHECK(data.boards[0].found == 1);
    CHECK(data.boards[0].viable_total == 5);
    REQUIRE(data.finds.size() == 1);
    CHECK(data.finds[0].word == "pat");
    CHECK(data.finds[0].stars == 1);
    CHECK(data.finds[0].day == today);
    CHECK_FALSE(data.finds[0].start_tile_used);
    REQUIRE(data.vault.size() == 1);
    CHECK(data.vault[0].word == "pat");

    // Found again is nothing new.
    REQUIRE(table.game.submit(pat).alreadyFound);
    CHECK(table.store.data().finds.size() == 1);
}

TEST_CASE("a clear that beats the best of its size is the new best, once, on the last word",
          "[app][real]") {
    wjs::Store store = wjs::Store::in_memory();
    // An earlier board of this size, cleared for one star: the best to beat.
    wjs::BoardSnapshot earlier;
    earlier.phase = "B";
    earlier.size = 3;
    earlier.tiles = "X Y Z/X Y Z/X Y Z";
    earlier.viable = {"PAN"};
    earlier.finds = {{.word = "pan", .assisted = false, .day = today - 1, .stars = 1}};
    REQUIRE(store.record(earlier, today - 1).stored);
    REQUIRE(store.best_stars(3) == 1);

    RealSoloGame game(hand_session("B"), true, &phase_b(), store, device_day);
    // Seven words, five of them 1 star, PAIN 2 and PAINT 5: twelve stars.
    const std::array<const Path*, 7> all = {&pat, &tap, &pin, &nip, &pan, &pain, &paint};
    for (std::size_t i = 0; i < all.size(); ++i) {
        INFO(i);
        const auto outcome = game.submit(*all[i]);
        REQUIRE(outcome.kind == WordKind::viable);
        CHECK(outcome.cleared == (i + 1 == all.size()));
        CHECK(outcome.newBest == (i + 1 == all.size()));
    }
    CHECK(store.best_stars(3) == 12);
}

TEST_CASE("a sticker is for the first time a word is found on this device", "[app][real]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::BoardSnapshot earlier;
    earlier.phase = "B";
    earlier.size = 3;
    earlier.tiles = "X Y Z/X Y Z/X Y Z";
    earlier.viable = {"PAIN"};
    earlier.finds = {{.word = "pain", .assisted = false, .day = today - 1, .stars = 2}};
    REQUIRE(store.record(earlier, today - 1).stored);

    RealSoloGame game(hand_session("B"), true, &phase_b(), store, device_day);
    const auto again = game.submit(pain);
    CHECK(again.kind == WordKind::viable);
    CHECK_FALSE(again.alreadyFound);  // not on this board's tray
    CHECK_FALSE(again.sticker);       // but on this device's vault

    // A tier with no sticker never gets one, new or not.
    CHECK_FALSE(game.submit(pat).sticker);
}

TEST_CASE("the lid hands the board left to the store, found or not", "[app][real]") {
    SECTION("a board with a find and one with none") {
        Table table("A");
        table.game.submit(pat);
        table.game.lidOn();
        CHECK(table.game.state() == SoloState::box);
        CHECK(table.store.data().boards.size() == 1);

        // The board is kept after the lid goes on, so the screens never read air.
        CHECK(table.game.board().tiles.size() == 9);

        // Another board, left at once.
        wjs::Store& store = table.store;
        RealSoloGame second(hand_session("B"), true, &phase_b(), store, device_day);
        second.lidOn();
        CHECK(second.state() == SoloState::box);
        CHECK(store.data().boards.size() == 2);
        CHECK(store.data().boards[1].found == 0);
    }
    SECTION("a board the child never saw is not a board") {
        Table table(dealing_session());
        REQUIRE(table.game.shake());
        table.game.lidOn();
        CHECK(table.game.state() == SoloState::box);
        CHECK(table.store.data().boards.empty());
    }
    SECTION("the lid does nothing in the box or after the last word") {
        Table table("A");
        for (const Path* path : {&pat, &tap, &pin, &nip, &pan}) {
            table.game.submit(*path);
        }
        REQUIRE(table.game.state() == SoloState::cleared);
        table.game.lidOn();
        CHECK(table.game.state() == SoloState::cleared);
        table.game.celebrated();
        table.game.again();
        table.game.lidOn();
        CHECK(table.game.state() == SoloState::box);
        CHECK(table.store.data().boards.size() == 1);
    }
}

TEST_CASE("the history is the store's boards, newest first, with their finds", "[app][real]") {
    wjs::Store store = wjs::Store::in_memory();
    {
        RealSoloGame first(hand_session("A"), true, &phase_a(), store, device_day);
        first.submit(pat);
        first.submit(tap);
        first.lidOn();
    }
    {
        RealSoloGame second(hand_session("B"), true, &phase_b(), store, device_day);
        second.submit(pain);
        second.lidOn();

        const auto history = second.history();
        REQUIRE(history.size() == 2);

        // Newest first: the phase B board, whose dead letter this game knows.
        const std::vector<std::string> own = {"P", "A", "T", "", "I", "N", "", "", ""};
        CHECK(history[0].board.rows == 3);
        CHECK(history[0].board.columns == 3);
        CHECK(history[0].board.tiles == own);
        REQUIRE(history[0].found.size() == 1);
        CHECK(history[0].found[0].word == "pain");
        CHECK(history[0].found[0].tier == WordTier::power);
        CHECK(history[0].found[0].stars == 2);

        // The phase A board, from a game of another phase: its letters as they are.
        const std::vector<std::string> other = {"P", "A", "T", "B", "I", "N", "B", "B", "B"};
        CHECK(history[1].board.tiles == other);
        CHECK(words_of(history[1].found) == std::vector<std::string>{"pat", "tap"});
    }
    // The phase A game reads its own board with the dead jewels blank.
    const RealSoloGame third(hand_session("A"), true, &phase_a(), store, device_day);
    const auto history = third.history();
    REQUIRE(history.size() == 2);
    const std::vector<std::string> mine = {"P", "A", "T", "", "I", "N", "", "", ""};
    CHECK(history[1].board.tiles == mine);
}

// --- help ------------------------------------------------------------------

TEST_CASE("the start tile is the session's, lit until its word is found", "[app][real]") {
    Table table("A");
    RealSoloGame& game = table.game;
    CHECK(game.helpAllowed());
    CHECK_FALSE(game.litTile().has_value());  // asking lights nothing

    // The shortest word nearest the top left: PAN, which starts at the top left.
    const auto tile = game.startTile();
    REQUIRE(tile.has_value());
    CHECK(*tile == Position{0, 0});
    CHECK(game.litTile() == tile);
    CHECK(game.session().lit_tile() == tile);

    // Another word found: the same tile stays lit.
    game.submit(tap);
    CHECK(game.litTile() == tile);
    // Its own word found: gone.
    game.submit(pan);
    CHECK_FALSE(game.litTile().has_value());

    // An assisted find reaches the store as one.
    REQUIRE(table.store.data().finds.size() == 2);
    CHECK_FALSE(table.store.data().finds[0].start_tile_used);
    CHECK(table.store.data().finds[1].start_tile_used);
}

TEST_CASE("with the parent's help switch off there is no start tile", "[app][real]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::SessionOptions options;
    options.help = false;
    RealSoloGame game(hand_session("A", {}, options), false, &phase_a(), store, device_day);

    CHECK_FALSE(game.helpAllowed());
    CHECK_FALSE(game.startTile().has_value());
    CHECK_FALSE(game.litTile().has_value());
}

// --- the grapheme blocks ---------------------------------------------------

TEST_CASE("the blocks of a path are the binder's, over the phase's graphemes", "[app][real]") {
    Table table("B");
    // P, then the digraph AI as one block of two tiles, then N.
    const auto blocks = table.game.blocks(pain);
    REQUIRE(blocks.size() == 3);
    CHECK(blocks[0].first == 0);
    CHECK(blocks[0].tiles == 1);
    CHECK(blocks[1].first == 1);
    CHECK(blocks[1].tiles == 2);
    CHECK(blocks[1].grapheme == "ai");
    CHECK(blocks[2].first == 3);
    CHECK(blocks[2].tiles == 1);

    // Phase A has no digraph: every tile is a block of its own.
    Table single("A");
    CHECK(single.game.blocks(pain).size() == 4);

    // No board, no blocks; a position off the board is not read.
    Table closed(dealing_session());
    CHECK(closed.game.blocks(pat).empty());
    const Path off{{0, 0}, {5, 5}};
    CHECK(table.game.blocks(off).empty());
}

// --- the program's choice of game --------------------------------------------

TEST_CASE("the application plays no game until it is told what to play", "[app][real]") {
    {
        // Not the tests' stand-in, which no program plays: a box that cannot be
        // shaken, so a model made without a game never pretends to play one.
        const auto none = wjs::app::make_solo_game();
        REQUIRE(none != nullptr);
        CHECK(dynamic_cast<wjs::app::FakeSoloGame*>(none.get()) == nullptr);
        CHECK(none->state() == SoloState::box);
        CHECK_FALSE(none->shake());
        CHECK(none->state() == SoloState::box);
        CHECK(none->board().empty());
        CHECK_FALSE(none->helpAllowed());
        CHECK(none->history().empty());
    }

    wjs::Store store = wjs::Store::in_memory();
    {
        const wjs::app::ScopedSoloGameMaker installed([&store] {
            return std::make_unique<RealSoloGame>(hand_session("A"), true, &phase_a(), store,
                                                  device_day);
        });
        const auto game = wjs::app::make_solo_game();
        CHECK(dynamic_cast<RealSoloGame*>(game.get()) != nullptr);
        CHECK(game->state() == SoloState::playing);
    }
    const auto after = wjs::app::make_solo_game();
    CHECK(dynamic_cast<RealSoloGame*>(after.get()) == nullptr);
    CHECK(after->state() == SoloState::box);
}

// --- loading what the game needs -----------------------------------------------

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static int counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("wjs_solo_setup_test_" + std::to_string(stamp) + "_" + std::to_string(++counter));
        std::filesystem::create_directories(path_);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

// The standard board's dice of this file, as a directory holds them: the faces
// are the test words' letters, so a roll holds words of the list above.
void write_dice(const TempDirectory& directory) {
    for (std::size_t side = 3; side <= 6; ++side) {
        const std::string name = std::to_string(side) + "x" + std::to_string(side) + ".txt";
        std::string dice;
        for (std::size_t die = 0; die < side * side; ++die) {
            dice += "p a t i n s\n";
        }
        write(directory.path() / "dice" / name, dice);
    }
}

// The phases, the word list and the dice of this file, as a directory holds them.
void write_data(const TempDirectory& directory) {
    write(directory.path() / "phonics" / "phase-a.graphemes.txt", graphemes_a);
    write(directory.path() / "phonics" / "phase-a.words.txt", words_a);
    write(directory.path() / "phonics" / "phase-b.graphemes.txt", graphemes_b);
    write(directory.path() / "phonics" / "phase-b.words.txt", words_b);
    std::string list;
    for (const auto word : phase_words) {
        list += std::string(word) + "\n";
    }
    for (const auto word : extra_words) {
        list += std::string(word) + "\n";
    }
    write(directory.path() / "en.txt", list);
    write_dice(directory);
}

wjs::app::SoloGameOptions options_in(const TempDirectory& directory) {
    wjs::app::SoloGameOptions options;
    options.phase_directory = directory.path() / "phonics";
    options.dictionary = directory.path() / "en.txt";
    options.dice_directory = directory.path() / "dice";
    options.seed = 5;
    options.today = device_day;
    return options;
}

}  // namespace

TEST_CASE("the setup loads a phase from a directory and makes a game from it", "[app][real]") {
    const TempDirectory directory;
    write_data(directory);

    wjs::Store store = wjs::Store::in_memory();
    auto opened = wjs::app::SoloGameSetup::open(options_in(directory), store);
    REQUIRE(opened.has_value());
    auto& setup = **opened;

    // The files carry no review header: test data, and said so.
    CHECK_FALSE(setup.reviewed());
    CHECK(setup.unreviewed_files().size() == 4);
    // With no phase named, the settings' phase: "A" on a new device.
    REQUIRE(setup.phase() != nullptr);
    CHECK(setup.phase()->phase() == "A");
    CHECK(setup.phases().phases().size() == 2);

    // Two games of one fixed seed deal the same board.
    const auto first = setup.make();
    const auto second = setup.make();
    REQUIRE(first->shake());
    REQUIRE(second->shake());
    CHECK(first->board().tiles == second->board().tiles);
    CHECK(first->board().seed == second->board().seed);
    CHECK(first->state() == SoloState::shaking);
}

TEST_CASE("the setup makes each game from the parent's settings", "[app][real]") {
    const TempDirectory directory;
    write_data(directory);
    wjs::Store store = wjs::Store::in_memory();
    wjs::Settings settings = store.settings();
    settings.help = wjs::HelpLevel::off;
    settings.board_size = 4;
    settings.phase = "B";
    store.update_settings(settings);

    auto options = options_in(directory);
    auto opened = wjs::app::SoloGameSetup::open(options, store);
    REQUIRE(opened.has_value());
    REQUIRE((*opened)->phase() != nullptr);
    CHECK((*opened)->phase()->phase() == "B");

    const auto game = (*opened)->make();
    CHECK_FALSE(game->helpAllowed());
    CHECK(game->size() == 4);

    // A phase named on the command line wins over the settings.
    options.phase = "A";
    auto named = wjs::app::SoloGameSetup::open(options, store);
    REQUIRE(named.has_value());
    CHECK((*named)->phase()->phase() == "A");
}

TEST_CASE("a game made by the setup writes its finds to the store's file", "[app][real]") {
    const TempDirectory directory;
    write_data(directory);
    const auto options = options_in(directory);
    const auto file = directory.path() / "kept" / "store.wjs";

    {
        auto kept = wjs::Store::open(file);
        auto opened = wjs::app::SoloGameSetup::open(options, kept.store);
        REQUIRE(opened.has_value());
        const auto game = (*opened)->make();
        REQUIRE(game->shake());
        game->settled();
        // Every word the solver finds on the dealt board, traced along its path.
        const auto* real = dynamic_cast<RealSoloGame*>(game.get());
        REQUIRE(real != nullptr);
        const auto solved = wjs::solve(*real->session().board(), english());
        REQUIRE_FALSE(solved.empty());
        CHECK(game->submit(solved.front().path).kind == WordKind::viable);
        game->lidOn();
    }

    // The next run reads the board back from the file.
    auto kept = wjs::Store::open(file);
    CHECK(kept.status == wjs::LoadStatus::loaded);
    auto reopened = wjs::app::SoloGameSetup::open(options, kept.store);
    REQUIRE(reopened.has_value());
    const auto game = (*reopened)->make();
    const auto history = game->history();
    REQUIRE(history.size() == 1);
    CHECK(history[0].found.size() == 1);
}

TEST_CASE("a phase the settings name that the directory lacks falls back to the first",
          "[app][real]") {
    const TempDirectory directory;
    write_data(directory);
    wjs::Store store = wjs::Store::in_memory();
    wjs::Settings settings = store.settings();
    settings.phase = "Z";  // chosen once, from files that have since changed
    store.update_settings(settings);

    auto options = options_in(directory);
    options.phase.clear();  // nothing asked on the command line
    const auto opened = wjs::app::SoloGameSetup::open(options, store);

    // The program starts, on the first phase it has, and does not stay out of the
    // game for a stale setting; a phase asked for by name that is not there is
    // still an error (below).
    REQUIRE(opened.has_value());
    REQUIRE(!(*opened)->phases().phases().empty());
    CHECK((*opened)->phase() == &(*opened)->phases().phases().front());
}

TEST_CASE("the setup says why it cannot load", "[app][real]") {
    using Catch::Matchers::ContainsSubstring;
    const TempDirectory directory;
    write_data(directory);
    wjs::Store store = wjs::Store::in_memory();

    SECTION("no word list") {
        auto options = options_in(directory);
        options.dictionary = directory.path() / "missing.txt";
        const auto opened = wjs::app::SoloGameSetup::open(options, store);
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("could not be read"));
        CHECK_THAT(opened.error(), ContainsSubstring("--dictionary"));
    }
    SECTION("no calendar") {
        auto options = options_in(directory);
        options.today = nullptr;
        const auto opened = wjs::app::SoloGameSetup::open(options, store);
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("calendar"));
    }
    SECTION("no phase directory") {
        auto options = options_in(directory);
        options.phase_directory = directory.path() / "nowhere";
        const auto opened = wjs::app::SoloGameSetup::open(options, store);
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("no such directory"));
    }
    SECTION("a file with a problem names it") {
        write(directory.path() / "phonics" / "phase-a.words.txt", "# phase: A\npat  p a x\n");
        const auto opened = wjs::app::SoloGameSetup::open(options_in(directory), store);
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("phase-a.words.txt"));
    }
    SECTION("a phase that is not there") {
        auto options = options_in(directory);
        options.phase = "Z";
        const auto opened = wjs::app::SoloGameSetup::open(options, store);
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("no phase Z"));
        CHECK_THAT(opened.error(), ContainsSubstring("A B"));
    }
    SECTION("a phase that uses every letter cannot make a board") {
        std::string graphemes = "# phase: A\n";
        std::string words = "# phase: A\n";
        std::string list;
        const std::array<std::string_view, 9> triples = {"abc", "def", "ghi", "jkl", "mno",
                                                         "pqr", "stu", "vwx", "yza"};
        for (char letter = 'a'; letter <= 'z'; ++letter) {
            graphemes += std::string(1, letter) + "  /x/  clip:" + std::string(1, letter) + "\n";
        }
        for (const std::string_view word : triples) {
            words += std::string(word) + "  ";
            words += std::string(1, word[0]) + " " + std::string(1, word[1]) + " " +
                     std::string(1, word[2]) + "\n";
            list += std::string(word) + "\n";
        }
        const TempDirectory every;
        write(every.path() / "phonics" / "phase-a.graphemes.txt", graphemes);
        write(every.path() / "phonics" / "phase-a.words.txt", words);
        write(every.path() / "en.txt", list);
        write_dice(every);
        const auto opened = wjs::app::SoloGameSetup::open(options_in(every), store);
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("cannot be played"));
    }
    SECTION("no dice") {
        auto options = options_in(directory);
        options.dice_directory = directory.path() / "nowhere";
        const auto opened = wjs::app::SoloGameSetup::open(options, store);
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("dice"));
    }
}

// --- the parent area on the game's store ------------------------------------------

TEST_CASE("the parent area on the game's store sees what the game recorded",
          "[app][real][parent]") {
    Table table("A");
    wjs::app::ParentController controller;
    controller.useCalendar(device_day, 1);
    controller.useStore(&table.store);
    controller.usePhases(&phases());
    REQUIRE(controller.dashboard().value("empty").toBool());

    REQUIRE(table.game.submit(pat).kind == WordKind::viable);

    // The dashboard is worked out when it is opened, as of then.
    controller.refresh();
    const QVariantMap dashboard = controller.dashboard();
    CHECK_FALSE(dashboard.value("empty").toBool());
    const QVariantMap week = dashboard.value("week").toMap();
    CHECK(week.value("newWords").toInt() == 1);
    CHECK(week.value("longestWord").toString() == "pat");
    const QVariantList vault = dashboard.value("vault").toList();
    REQUIRE(vault.size() == 1);
    CHECK(vault.at(0).toMap().value("word").toString() == "pat");
}

TEST_CASE("a setting the parent changes is what the setup makes the next game from",
          "[app][real][parent]") {
    const TempDirectory directory;
    write_data(directory);
    wjs::Store store = wjs::Store::in_memory();
    auto opened = wjs::app::SoloGameSetup::open(options_in(directory), store);
    REQUIRE(opened.has_value());
    auto& setup = **opened;
    wjs::app::ParentController controller;
    controller.useStore(&store);
    controller.usePhases(&setup.phases());

    const auto before = setup.make();
    CHECK(before->helpAllowed());
    CHECK(before->size() == 3);

    CHECK(controller.setSetting("help", "off"));
    CHECK(controller.setSetting("boardSize", 4));

    const auto next = setup.make();
    CHECK_FALSE(next->helpAllowed());
    CHECK(next->size() == 4);
    // The game that was made before is the game it was.
    CHECK(before->helpAllowed());
    CHECK(before->size() == 3);
}

TEST_CASE("a sound the parent ticked is a sound the boards are dealt for", "[app][real][parent]") {
    const TempDirectory directory;
    write_data(directory);
    wjs::Store store = wjs::Store::in_memory();

    // What a run before this one left in the store: the phonics board over phase A,
    // with "n" ticked. The settings keep a ticked sound as its letters, which is the
    // string the library's seeding matches a grapheme on (the slashed sound matches
    // nothing), so this is the whole path of a tick: store, setup, session, board.
    // NIP, PAN and PIN are the words with "n" in this phase.
    wjs::Settings settings = store.settings();
    settings.board = wjs::BoardMode::phonics;
    settings.phase = "A";
    settings.focus_sounds = {"n"};
    store.update_settings(settings);
    auto opened = wjs::app::SoloGameSetup::open(options_in(directory), store);
    REQUIRE(opened.has_value());

    auto game = (*opened)->make();
    const auto* real = dynamic_cast<RealSoloGame*>(game.get());
    REQUIRE(real != nullptr);
    int boards_with_the_sound = 0;
    constexpr int boards = 12;
    for (int i = 0; i < boards; ++i) {
        REQUIRE(game->shake());
        game->settled();
        const auto record = real->session().record();
        REQUIRE(record.has_value());
        const bool has_n = std::ranges::any_of(record->viable, [](const std::string& word) {
            return word == "NIP" || word == "PAN" || word == "PIN";
        });
        boards_with_the_sound += has_n ? 1 : 0;
        game->lidOn();
    }
    // Targets are dealt for the ticked sound: it is on every board, not only on
    // those where chance put it.
    CHECK(boards_with_the_sound == boards);
}

TEST_CASE("a change in the parent area is the box's next game, through the model",
          "[app][real][parent]") {
    const TempDirectory directory;
    write_data(directory);
    wjs::Store store = wjs::Store::in_memory();
    auto opened = wjs::app::SoloGameSetup::open(options_in(directory), store);
    REQUIRE(opened.has_value());
    auto& setup = **opened;

    // The program's wiring: the maker is the setup's, and the shell tells the model
    // what the controller says (Main.qml's Connections).
    const wjs::app::ScopedSoloGameMaker installed([&setup] { return setup.make(); });
    wjs::app::SoloModel model;
    wjs::app::ParentController controller;
    controller.useStore(&store);
    controller.usePhases(&setup.phases());
    QObject::connect(&controller, &wjs::app::ParentController::storeEdited, &model,
                     &wjs::app::SoloModel::storeEdited);
    REQUIRE(model.state() == "box");
    CHECK(model.helpAllowed());

    CHECK(controller.setSetting("help", "off"));

    CHECK(model.state() == "box");
    CHECK_FALSE(model.helpAllowed());
}

// --- the game in progress ------------------------------------------------------

TEST_CASE("the store keeps the game in progress after each change", "[app][real]") {
    Table table("A");
    RealSoloGame& game = table.game;
    const std::optional<wjs::SavedSession>& saved = table.store.session();

    // A board on the table with nothing found.
    REQUIRE(saved.has_value());
    CHECK(saved->state == wjs::SavedState::playing);
    CHECK(saved->size == 3);
    REQUIRE(saved->board.has_value());
    CHECK(saved->board->phase == "A");
    CHECK(saved->board->finds.empty());
    CHECK_FALSE(saved->lit.has_value());

    // A find, with the day it was made.
    REQUIRE(game.submit(pat).kind == WordKind::viable);
    REQUIRE(saved->board->finds.size() == 1);
    CHECK(saved->board->finds[0].word == "pat");
    CHECK(saved->board->finds[0].day == today);

    // The start tile is kept while it is lit, and its word is found with help.
    REQUIRE(game.startTile().has_value());
    REQUIRE(saved->lit.has_value());
    const std::string lit = *saved->lit;
    const auto words = wjs::solve(*game.session().board(), phase_a().dictionary());
    const auto lit_word = std::ranges::find_if(
        words, [&](const wjs::FoundWord& word) { return lower_case(word.word) == lit; });
    REQUIRE(lit_word != words.end());
    REQUIRE(game.submit(lit_word->path).kind == WordKind::viable);
    CHECK_FALSE(saved->lit.has_value());
    REQUIRE(saved->board->finds.size() == 2);
    CHECK(saved->board->finds[1].assisted);

    // The last word clears the board, and a cleared board is kept as the resting
    // screen, which the celebration does not change.
    for (const wjs::FoundWord& word : words) {
        game.submit(word.path);
    }
    REQUIRE(game.state() == SoloState::cleared);
    CHECK(saved->state == wjs::SavedState::resting);
    CHECK(saved->board->finds.size() == 5);
    game.celebrated();
    CHECK(saved->state == wjs::SavedState::resting);

    // Again puts the lid on: nothing is in progress.
    game.again();
    CHECK_FALSE(saved.has_value());
}

TEST_CASE("a game in the box, the shake or the drop is no game in progress", "[app][real]") {
    // A game left on the table, which a new game replaces.
    Table left("A");
    REQUIRE(left.store.session().has_value());
    const std::optional<wjs::SavedSession>& saved = left.store.session();

    auto made = RealSoloGame::create(data_of(phase_a()), left.store, phonics(), device_day);
    REQUIRE(made.has_value());
    RealSoloGame& game = *made;
    CHECK_FALSE(saved.has_value());

    REQUIRE(game.shake());
    CHECK_FALSE(saved.has_value());
    game.settled();
    REQUIRE(saved.has_value());
    CHECK(saved->state == wjs::SavedState::playing);

    game.lidOn();
    CHECK_FALSE(saved.has_value());
}

TEST_CASE("the size the child chooses is the one the box offers next", "[app][real]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::Settings settings = store.settings();
    settings.help = wjs::HelpLevel::off;
    settings.phase = "B";
    store.update_settings(settings);
    auto made = RealSoloGame::create(data_of(phase_a()), store, phonics(), device_day);
    REQUIRE(made.has_value());
    RealSoloGame& game = *made;
    REQUIRE(store.settings().board_size == 3);

    struct Choice {
        std::size_t side;
        int written;  // the settings' board size after it
    };
    // A side that is no size is not a choice.
    const std::array<Choice, 6> choices = {{{4, 4}, {4, 4}, {5, 4}, {3, 3}, {0, 3}, {4, 4}}};
    for (const Choice& choice : choices) {
        INFO(choice.side);
        game.chooseSize(choice.side);
        CHECK(store.settings().board_size == choice.written);
    }
    // The rest of the settings are the parent's, and are not touched.
    CHECK(store.settings().help == wjs::HelpLevel::off);
    CHECK(store.settings().phase == "B");

    // Only the box takes a choice, so a later one is not the last used.
    REQUIRE(game.shake());
    game.chooseSize(3);
    CHECK(game.size() == 4);
    CHECK(store.settings().board_size == 4);
}

namespace {

// What a game shows the screens: enough to tell one game in progress from another.
struct Seen {
    SoloState state = SoloState::box;
    std::size_t size = 0;
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::vector<std::string> tiles;
    std::vector<std::string> found;
    std::optional<Position> lit;
};

Seen seen(const wjs::app::SoloGame& game) {
    Seen shown;
    shown.state = game.state();
    shown.size = game.size();
    shown.rows = game.board().rows;
    shown.columns = game.board().columns;
    shown.tiles = game.board().tiles;
    shown.found = words_of(game.found());
    shown.lit = game.litTile();
    return shown;
}

// One run of the program, as far as the game goes: the setup opened on the files and
// the store, the game it made, and a few moves a child's hand makes on it.
struct Run {
    wjs::app::SoloGameSetup& setup;
    wjs::app::SoloGame& game;

    // The shake and the drop: a board on the table.
    void deal() const {
        REQUIRE(game.shake());
        game.settled();
        REQUIRE(game.state() == SoloState::playing);
    }

    // Every word of the board in play, each with a path that spells it: the
    // words that count, which are the phase's on the phonics board and the
    // whole dictionary's on the standard board.
    [[nodiscard]] std::vector<wjs::FoundWord> words() const {
        const auto* real = dynamic_cast<const RealSoloGame*>(&game);
        REQUIRE(real != nullptr);
        const wjs::Dictionary& active = real->session().mode() == wjs::BoardMode::standard
                                            ? english()
                                            : setup.phase()->dictionary();
        return wjs::solve(*real->session().board(), active);
    }

    // The first `count` of them, found.
    void find(std::size_t count) const {
        const auto all = words();
        REQUIRE(all.size() >= count);
        for (std::size_t i = 0; i < count; ++i) {
            REQUIRE(game.submit(all[i].path).kind == WordKind::viable);
        }
    }
};

using Play = std::function<void(const Run&)>;

// What a run is made from: the setup's options and the store file the runs share.
struct RunOptions : wjs::app::SoloGameOptions {
    std::filesystem::path store;
};

// What the program has when it starts: the store opened from its file (main.cpp) and
// the setup over it. Held in place, because the setup points at the store.
struct OpenedRun {
    wjs::OpenResult store;
    std::unique_ptr<wjs::app::SoloGameSetup> setup;

    explicit OpenedRun(const RunOptions& options) : store(wjs::Store::open(options.store)) {
        auto opened = wjs::app::SoloGameSetup::open(options, store.store);
        REQUIRE(opened.has_value());
        setup = *std::move(opened);
    }
    OpenedRun(const OpenedRun&) = delete;
    OpenedRun& operator=(const OpenedRun&) = delete;
};

// A run: the setup opened as the program opens it, its game made and played with
// `play`, and everything closed again. What the game showed when it was left.
Seen run(const RunOptions& options, const Play& play = {}) {
    const OpenedRun opened(options);
    const auto game = opened.setup->make();
    if (play) {
        play(Run{*opened.setup, *game});
    }
    return seen(*game);
}

// The options of a run over a store file of its own, which the runs share. The
// store's switch is on the phonics board, which is what these runs play (the
// runs of the standard board clear the phase directory and keep the default).
RunOptions kept_options(const TempDirectory& directory) {
    write_data(directory);
    RunOptions options;
    static_cast<wjs::app::SoloGameOptions&>(options) = options_in(directory);
    options.store = directory.path() / "store.wjs";
    auto opened = wjs::Store::open(options.store);
    wjs::Settings settings = opened.store.settings();
    settings.board = wjs::BoardMode::phonics;
    opened.store.update_settings(settings);
    return options;
}

// The store's file as a run left it, with its game in progress changed by `edit`.
void tamper(const std::filesystem::path& file,
            const std::function<void(wjs::SavedSession&)>& edit) {
    auto opened = wjs::Store::open(file);
    REQUIRE(opened.store.session().has_value());
    wjs::SavedSession saved = *opened.store.session();
    edit(saved);
    REQUIRE(opened.store.save_session(saved));
}

}  // namespace

TEST_CASE("a run begins where the last one ended", "[app][real]") {
    struct Left {
        const char* name;
        Play play;
        SoloState next;  // what the next run opens on
        bool kept;       // with the board, the tray and the start tile of the run before
    };
    const std::array<Left, 7> rows = {{
        {"a board on the table", [](const Run& run) { run.deal(); }, SoloState::playing, true},
        {"two finds and the start tile lit",
         [](const Run& run) {
             run.deal();
             run.find(2);
             REQUIRE(run.game.startTile().has_value());
         },
         SoloState::playing, true},
        {"the last word found",
         [](const Run& run) {
             run.deal();
             run.find(run.words().size());
             REQUIRE(run.game.state() == SoloState::cleared);
         },
         SoloState::resting, true},
        {"the celebration over",
         [](const Run& run) {
             run.deal();
             run.find(run.words().size());
             run.game.celebrated();
             REQUIRE(run.game.state() == SoloState::resting);
         },
         SoloState::resting, true},
        {"Again",
         [](const Run& run) {
             run.deal();
             run.find(run.words().size());
             run.game.celebrated();
             run.game.again();
         },
         SoloState::box, false},
        {"the lid put back on",
         [](const Run& run) {
             run.deal();
             run.find(1);
             run.game.lidOn();
         },
         SoloState::box, false},
        {"the box mid-shake", [](const Run& run) { REQUIRE(run.game.shake()); }, SoloState::box,
         false},
    }};

    for (const Left& row : rows) {
        INFO(row.name);
        const TempDirectory directory;
        const auto options = kept_options(directory);

        const Seen before = run(options, row.play);
        const Seen after = run(options);

        CHECK(after.state == row.next);
        if (row.kept) {
            CHECK_FALSE(after.tiles.empty());
            CHECK(after.size == before.size);
            CHECK(after.rows == before.rows);
            CHECK(after.columns == before.columns);
            CHECK(after.tiles == before.tiles);
            CHECK(after.found == before.found);
            CHECK(after.lit == before.lit);
        } else {
            CHECK(after.tiles.empty());
            CHECK(after.found.empty());
            CHECK_FALSE(after.lit.has_value());
        }
    }
}

TEST_CASE("a game brought back goes on where it was, and the board is one board", "[app][real]") {
    const TempDirectory directory;
    const auto options = kept_options(directory);
    run(options, [](const Run& run) {
        run.deal();
        run.find(1);
    });

    std::size_t boards = 0;
    std::vector<std::string> history_words;
    const Seen second = run(options, [&](const Run& run) {
        REQUIRE(run.game.state() == SoloState::playing);
        // The first word is on the tray already, the second is new.
        run.find(2);
        run.game.lidOn();
        const auto history = run.game.history();
        boards = history.size();
        if (!history.empty()) {
            history_words = words_of(history.front().found);
        }
    });

    CHECK(second.state == SoloState::box);
    CHECK(boards == 1);
    CHECK(history_words.size() == 2);
    // The lid on is the end of that game: the next run starts anew.
    CHECK(run(options).state == SoloState::box);
}

TEST_CASE("the next run's box offers the size last chosen", "[app][real]") {
    const TempDirectory directory;
    const auto options = kept_options(directory);

    CHECK(run(options).size == 3);
    run(options, [](const Run& run) { run.game.chooseSize(4); });
    const Seen next = run(options);
    CHECK(next.state == SoloState::box);
    CHECK(next.size == 4);
    run(options, [](const Run& run) { run.game.chooseSize(3); });
    CHECK(run(options).size == 3);
}

TEST_CASE("a game that cannot come back is forgotten and a new one begins in the box",
          "[app][real]") {
    struct Gone {
        const char* name;
        std::function<void(wjs::SavedSession&)> edit;  // what the file is changed by
        std::string phase;                             // the phase of the next run, or ""
    };
    const std::array<Gone, 4> rows = {{
        {"the phase played is another now", nullptr, "B"},
        {"the phase no longer holds a word of the board",
         [](wjs::SavedSession& saved) {
             // A word that is not found yet: the store keeps no find that is not on the board.
             std::vector<std::string>& viable = saved.board->viable;
             const auto gone = std::ranges::find_if(viable, [&](const std::string& word) {
                 return std::ranges::none_of(saved.board->finds,
                                             [&](const wjs::FoundSnapshot& found) {
                                                 return lower_case(found.word) == lower_case(word);
                                             });
             });
             REQUIRE(gone != viable.end());
             viable.erase(gone);
             saved.board->viable_total = static_cast<int>(viable.size());
         },
         ""},
        {"a record that is resting but not cleared",
         [](wjs::SavedSession& saved) { saved.state = wjs::SavedState::resting; }, ""},
        {"a start tile lit for a word already found",
         [](wjs::SavedSession& saved) { saved.lit = saved.board->finds.front().word; }, ""},
    }};

    for (const Gone& row : rows) {
        INFO(row.name);
        const TempDirectory directory;
        auto options = kept_options(directory);
        run(options, [](const Run& run) {
            run.deal();
            run.find(1);
        });
        if (row.edit) {
            tamper(options.store, row.edit);
        }
        options.phase = row.phase;

        // Nothing is said: the next run opens in the box, with nothing on the table,
        // and what could not come back is gone from the file.
        const Seen after = run(options);
        CHECK(after.state == SoloState::box);
        CHECK(after.tiles.empty());
        CHECK(after.found.empty());
        CHECK_FALSE(wjs::Store::open(options.store).store.session().has_value());

        // And that run goes on to play a game of its own (the other phase of the test
        // data is too small to deal a board).
        if (row.phase.empty()) {
            CHECK(run(options, [](const Run& run) { run.deal(); }).state == SoloState::playing);
            CHECK(run(options).state == SoloState::playing);
        }
    }
}

TEST_CASE("a saved game of the other board mode is not played by this one", "[app][real]") {
    // The store keeps either board's game in progress. A standard game saved in
    // the file (no phase, the count of its words and no list), a later program's
    // or one edited by hand, is played under the standard switch and refused as
    // the other mode when the parent has switched to phonics: the session says
    // so, and the run opens in the box with the record cleared.
    const TempDirectory directory;
    auto options = kept_options(directory);
    run(options, [](const Run& run) {
        run.deal();
        run.find(1);
    });

    {
        auto opened = wjs::Store::open(options.store);
        REQUIRE(opened.store.session().has_value());
        wjs::StoreData data = opened.store.data();
        data.session->board->phase.clear();
        data.session->board->viable.clear();
        const auto parsed = wjs::Board::parse(data.session->board->tiles);
        REQUIRE(parsed.has_value());
        data.session->board->viable_total = static_cast<int>(wjs::solve(*parsed, english()).size());
        std::ofstream file(options.store, std::ios::binary | std::ios::trunc);
        file << wjs::serialize_store(data);
    }
    const auto kept = wjs::Store::open(options.store);
    REQUIRE(kept.store.session().has_value());
    REQUIRE(kept.store.session()->board.has_value());
    REQUIRE(kept.store.session()->board->phase.empty());

    SECTION("the standard switch plays it") {
        {
            auto opened = wjs::Store::open(options.store);
            wjs::Settings settings = opened.store.settings();
            settings.board = wjs::BoardMode::standard;
            opened.store.update_settings(settings);
        }
        const Seen after =
            run(options, [](const Run& run) { REQUIRE(run.game.state() == SoloState::playing); });
        CHECK(after.state == SoloState::playing);
    }
    SECTION("the phonics switch refuses it as the other mode") {
        {
            auto opened = wjs::Store::open(options.store);
            wjs::Settings settings = opened.store.settings();
            settings.board = wjs::BoardMode::phonics;
            opened.store.update_settings(settings);
        }
        const Seen after = run(options);
        CHECK(after.state == SoloState::box);
        CHECK(after.tiles.empty());
        CHECK(after.found.empty());
        CHECK_FALSE(wjs::Store::open(options.store).store.session().has_value());
    }
}

TEST_CASE("a start tile the parent no longer allows is not lit when the game comes back",
          "[app][real]") {
    const TempDirectory directory;
    const auto options = kept_options(directory);
    const Seen before = run(options, [](const Run& run) {
        run.deal();
        run.find(1);
        REQUIRE(run.game.startTile().has_value());
    });
    REQUIRE(before.lit.has_value());
    {
        auto opened = wjs::Store::open(options.store);
        wjs::Settings settings = opened.store.settings();
        settings.help = wjs::HelpLevel::off;
        opened.store.update_settings(settings);
    }

    bool allowed = true;
    const Seen after = run(options, [&](const Run& run) { allowed = run.game.helpAllowed(); });
    CHECK_FALSE(allowed);
    CHECK(after.state == SoloState::playing);
    CHECK(after.tiles == before.tiles);
    CHECK_FALSE(after.lit.has_value());
}

TEST_CASE("the model opens on the game that was left, with no shake", "[app][real]") {
    const TempDirectory directory;
    const auto options = kept_options(directory);

    SECTION("a board on the table, its tray and its lit tile") {
        std::vector<Position> third;
        const Seen before = run(options, [&](const Run& run) {
            run.deal();
            run.find(2);
            REQUIRE(run.game.startTile().has_value());
            third = run.words()[2].path;
        });
        REQUIRE(before.lit.has_value());

        const OpenedRun opened(options);
        wjs::app::SoloModel model(opened.setup->make());
        model.setGeometry(100, 10);

        CHECK(model.state() == "playing");
        CHECK(model.rows() == 3);
        CHECK(model.columns() == 3);
        QStringList tiles;
        for (const std::string& tile : before.tiles) {
            tiles.append(QString::fromStdString(tile));
        }
        CHECK(model.tiles() == tiles);
        CHECK(model.boardVersion() == 1);
        REQUIRE(model.found().size() == 2);
        CHECK(model.found()[0].toMap().value("word").toString().toStdString() == before.found[0]);
        CHECK(model.helpTile() == static_cast<int>(before.lit->row * 3 + before.lit->column));

        // It takes a word at once: the tiles tapped in order, and Done.
        for (const Position position : third) {
            model.tapTile(static_cast<int>(position.row * 3 + position.column));
        }
        model.done();
        CHECK(model.state() == "word-found");
        CHECK(model.found().size() == 3);
    }
    SECTION("the resting screen, and Again from it") {
        run(options, [](const Run& run) {
            run.deal();
            run.find(run.words().size());
        });

        const OpenedRun opened(options);
        wjs::app::SoloModel model(opened.setup->make());

        CHECK(model.state() == "resting");
        CHECK_FALSE(model.found().isEmpty());
        model.again();
        CHECK(model.state() == "box");
    }
}

// --- the standard board -------------------------------------------------------

namespace {

// The dice of a standard board for these cases: faces drawn from the test words'
// letters, so a roll of any size holds words of the word list above.
const wjs::DiceSets& standard_dice() {
    static const wjs::DiceSets dice = [] {
        std::vector<wjs::DiceSet> sets;
        for (std::size_t side = 3; side <= 6; ++side) {
            std::string text;
            for (std::size_t die = 0; die < side * side; ++die) {
                text += "p a t i n s\n";
            }
            auto set = wjs::DiceSet::parse(text);
            REQUIRE(set.has_value());
            sets.push_back(*std::move(set));
        }
        auto made = wjs::DiceSets::make(std::move(sets));
        REQUIRE(made.has_value());
        return *std::move(made);
    }();
    return dice;
}

wjs::SessionData standard_data() {
    return {.dictionary = english(), .dice = &standard_dice()};
}

}  // namespace

TEST_CASE("a standard game is the closed box with the standard board's four sizes",
          "[app][real][standard]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::SessionOptions options;
    options.size = BoardSize::six;
    auto made = RealSoloGame::create(standard_data(), store, options, device_day);
    REQUIRE(made.has_value());
    const RealSoloGame& game = *made;

    CHECK(game.state() == SoloState::box);
    CHECK(game.size() == 6);
    REQUIRE(game.sizes().size() == 4);
    CHECK(game.sizes()[0] == 3);
    CHECK(game.sizes()[1] == 4);
    CHECK(game.sizes()[2] == 5);
    CHECK(game.sizes()[3] == 6);
    CHECK(game.helpAllowed());
    CHECK(game.board().empty());
}

TEST_CASE("the standard board deals a roll of the dice, with no dead jewels",
          "[app][real][standard]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::SessionOptions options;
    options.size = BoardSize::three;
    options.seed = 9;
    auto made = RealSoloGame::create(standard_data(), store, options, device_day);
    REQUIRE(made.has_value());
    RealSoloGame& game = *made;

    REQUIRE(game.shake());
    CHECK(game.state() == SoloState::shaking);
    const auto& board = game.board();
    CHECK(board.rows == 3);
    CHECK(board.columns == 3);
    // A dice board has no dead jewels: every tile is the face of a die.
    for (const std::string& tile : board.tiles) {
        CHECK_FALSE(tile.empty());
    }
    REQUIRE(game.session().last_deal().has_value());
    CHECK(board.seed == game.session().last_deal()->seed);
    game.settled();

    // A dictionary word traced on the roll is a find, as on the phonics board,
    // and a standard board has no graphemes to bind into blocks.
    const auto words = wjs::solve(*game.session().board(), english());
    REQUIRE_FALSE(words.empty());
    const auto first = game.submit(words.front().path);
    CHECK(first.kind == WordKind::viable);
    CHECK(game.blocks(words.front().path).empty());

    // The store keeps the board by its count, and none of its words.
    REQUIRE(store.data().boards.size() == 1);
    CHECK(store.data().boards.front().phase.empty());
    CHECK(store.data().boards.front().viable.empty());
    CHECK(store.data().boards.front().viable_total > 0);

    // And the game in progress is a standard board: kept, and brought back.
    const auto& saved = store.session();
    REQUIRE(saved.has_value());
    REQUIRE(saved->board.has_value());
    CHECK(saved->board->phase.empty());
    CHECK(saved->board->viable.empty());
    CHECK(saved->board->viable_total > 0);
    const auto back = wjs::session_snapshot_of(*saved);
    REQUIRE(back.has_value());
    CHECK(back->mode == wjs::BoardMode::standard);
    const auto restored = wjs::GameSession::restore(*back, standard_data(), options);
    REQUIRE(restored.has_value());
    CHECK(restored->mode() == wjs::BoardMode::standard);
    CHECK(restored->state() == SessionState::playing);
    CHECK(restored->stars() == game.session().stars());
}

TEST_CASE("a standard board's scene board is the dice of its seed", "[app][real][standard]") {
    wjs::Store store = wjs::Store::in_memory();
    wjs::SessionOptions options;
    options.size = BoardSize::three;
    options.seed = 9;
    auto made = RealSoloGame::create(standard_data(), store, options, device_day);
    REQUIRE(made.has_value());
    RealSoloGame& game = *made;

    REQUIRE(game.shake());
    const wjs::app::SoloBoard& board = game.board();
    REQUIRE(game.session().last_deal().has_value());
    CHECK(board.seed == game.session().last_deal()->seed);

    // A dealt board is the roll of its seed: each tile has its die's six faces
    // and the one that is up, and they are exactly what the library's roll says.
    REQUIRE(board.faces.size() == board.tiles.size());
    REQUIRE(board.up.size() == board.tiles.size());
    const wjs::DiceSet* dice = standard_dice().find(3);
    REQUIRE(dice != nullptr);
    const auto rolled = wjs::roll_with_placements(*dice, board.seed);
    REQUIRE(rolled.has_value());
    REQUIRE(rolled->placements.size() == board.tiles.size());
    for (std::size_t tile = 0; tile < board.tiles.size(); ++tile) {
        INFO("tile " << tile);
        const wjs::Placement& placement = rolled->placements[tile];
        CHECK(board.faces[tile] == dice->die(placement.die));
        CHECK(board.up[tile] == static_cast<int>(placement.face));
        REQUIRE(board.up[tile] >= 0);
        REQUIRE(board.up[tile] < 6);
        CHECK(board.faces[tile][static_cast<std::size_t>(board.up[tile])] == board.tiles[tile]);
        CHECK(board.tiles[tile] ==
              rolled->board.tile({tile / board.columns, tile % board.columns}));
    }
}

TEST_CASE("a board that was not rolled has no dice for the scene", "[app][real]") {
    // The phonics board is seeded, not rolled: no faces, and the scene draws the
    // tile's letter on the face that is up.
    Table phonics(dealing_session());
    REQUIRE(phonics.game.shake());
    CHECK(phonics.game.board().faces.empty());
    CHECK(phonics.game.board().up.empty());

    // The fake game has no dice at all (apps/app/tests/unit/fake_solo_game_test.cpp holds
    // the same).
    wjs::app::FakeSoloGame fake;
    REQUIRE(fake.shake());
    CHECK(fake.board().faces.empty());
    CHECK(fake.board().up.empty());
}

TEST_CASE("the size chosen on the standard board's box is 3 to 6", "[app][real][standard]") {
    wjs::Store store = wjs::Store::in_memory();
    auto made = RealSoloGame::create(standard_data(), store, {}, device_day);
    REQUIRE(made.has_value());
    RealSoloGame& game = *made;

    game.chooseSize(5);
    CHECK(game.size() == 5);
    REQUIRE(game.shake());
    CHECK(game.board().rows == 5);
    CHECK(game.board().columns == 5);
    // A 5x5 roll of the test dice holds words of the test word list.
    CHECK_FALSE(wjs::solve(*game.session().board(), english()).empty());
}

TEST_CASE("the setup without a phase directory makes the standard game", "[app][real][standard]") {
    const TempDirectory directory;
    write_data(directory);
    auto options = options_in(directory);
    options.phase_directory.clear();

    wjs::Store store = wjs::Store::in_memory();
    auto opened = wjs::app::SoloGameSetup::open(options, store);
    REQUIRE(opened.has_value());
    auto& setup = **opened;

    // No phase: the parent area has none to choose from, and the game is the
    // standard board whatever the settings' switch says.
    CHECK(setup.phases().phases().empty());
    CHECK(setup.phase() == nullptr);
    CHECK(setup.reviewed());

    wjs::Settings settings = store.settings();
    settings.board = wjs::BoardMode::phonics;  // the parent chose it, no phase given
    store.update_settings(settings);

    const auto game = setup.make();
    CHECK(game->state() == SoloState::box);
    REQUIRE(game->sizes().size() == 4);
    REQUIRE(game->shake());
    CHECK(game->board().rows == 3);
    game->settled();
    const auto* real = dynamic_cast<RealSoloGame*>(game.get());
    REQUIRE(real != nullptr);
    CHECK(real->session().mode() == wjs::BoardMode::standard);
    for (const std::string& tile : game->board().tiles) {
        CHECK_FALSE(tile.empty());
    }
}

TEST_CASE("a standard run begins where the last one ended", "[app][real][standard]") {
    const TempDirectory directory;
    write_data(directory);
    auto options = options_in(directory);
    options.phase_directory.clear();
    const auto file = directory.path() / "kept" / "store.wjs";

    // Deal a board and find one word; close the run.
    {
        auto kept = wjs::Store::open(file);
        auto opened = wjs::app::SoloGameSetup::open(options, kept.store);
        REQUIRE(opened.has_value());
        const auto game = (*opened)->make();
        REQUIRE(game->shake());
        game->settled();
        REQUIRE(game->state() == SoloState::playing);
        const auto* real = dynamic_cast<RealSoloGame*>(game.get());
        REQUIRE(real != nullptr);
        const auto words = wjs::solve(*real->session().board(), english());
        REQUIRE_FALSE(words.empty());
        REQUIRE(game->submit(words.front().path).kind == WordKind::viable);
        REQUIRE(kept.store.session().has_value());
        CHECK(kept.store.session()->board->phase.empty());
        CHECK(kept.store.session()->board->viable_total > 0);
    }

    // The next run opens on the same board, with its find in the tray.
    auto kept = wjs::Store::open(file);
    CHECK(kept.status == wjs::LoadStatus::loaded);
    auto reopened = wjs::app::SoloGameSetup::open(options, kept.store);
    REQUIRE(reopened.has_value());
    const auto game = (*reopened)->make();
    CHECK(game->state() == SoloState::playing);
    CHECK(game->board().rows == 3);
    CHECK(game->board().columns == 3);
    REQUIRE_FALSE(game->board().tiles.empty());
    // A board the store brought back keeps no roll seed, so it has no dice to
    // draw: the scene draws the tile's letter on the face that is up.
    CHECK(game->board().faces.empty());
    CHECK(game->board().up.empty());
    REQUIRE(game->found().size() == 1);
    const auto* real = dynamic_cast<RealSoloGame*>(game.get());
    REQUIRE(real != nullptr);
    CHECK(real->session().mode() == wjs::BoardMode::standard);
}

TEST_CASE("the shipped dice and dictionary make the standard game", "[app][real][standard]") {
    wjs::app::SoloGameOptions options;
    options.dice_directory = std::filesystem::path{WJS_DATA_DIRECTORY} / "dice";
    options.dictionary = std::filesystem::path{WJS_DATA_DIRECTORY} / "dictionaries" / "en.txt";
    options.seed = 7;
    options.today = device_day;
    wjs::Store store = wjs::Store::in_memory();
    auto opened = wjs::app::SoloGameSetup::open(options, store);
    REQUIRE(opened.has_value());
    CHECK((*opened)->phases().phases().empty());
    const auto game = (*opened)->make();
    CHECK(game->state() == SoloState::box);
    REQUIRE(game->sizes().size() == 4);
    REQUIRE(game->shake());
    CHECK(game->board().rows == 3);
    CHECK(game->board().columns == 3);
    const auto* real = dynamic_cast<RealSoloGame*>(game.get());
    REQUIRE(real != nullptr);
    CHECK(real->session().mode() == wjs::BoardMode::standard);
    for (const std::string& tile : game->board().tiles) {
        CHECK_FALSE(tile.empty());
    }
}
