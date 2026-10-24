// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/deal.hpp"
#include "wjs/dice.hpp"
#include "wjs/dice_sets.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/game_session.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/seeding.hpp"
#include "wjs/settings.hpp"
#include "wjs/solver.hpp"

// TEST DATA. Two phases written for these tests (the seven and four words the
// seeding tests use, with placeholder sounds) and a dictionary that holds them
// and a few real words that are not in either. None of it is a phase of the
// application or content for a child; the real phase files have to be written
// and reviewed by a person qualified to do it.
//
// Most cases play a board written out here, brought in through restore(): its
// words and paths are known without asking the session or the solver. The
// cases about dealing use the session's own boards.

namespace {

using wjs::BoardSize;
using wjs::Position;
using wjs::SessionState;
using wjs::TraceKind;

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

// Real words of the dictionary that no phase has. `tip` stands for any word a
// child must never be shown: it is in the dictionary, and so must never be named.
constexpr std::array<std::string_view, 9> extra_words = {"tip", "pit", "nap", "ant", "tan",
                                                         "at",  "in",  "it",  "tab"};

const wjs::Dictionary& english() {
    static const wjs::Dictionary dictionary = [] {
        wjs::Dictionary words;
        for (const std::string_view word :
             {"nip", "pan", "pat", "pin", "sat", "sit", "tap", "pain", "paint", "saint", "stain"}) {
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

// The cases up to "the standard board" below are the phonics board's, which a
// parent's switch gives and the default (the standard board) does not: they play
// their phase over `phonics(options)`.
wjs::SessionOptions phonics(wjs::SessionOptions options = {}) {
    options.mode = wjs::BoardMode::phonics;
    return options;
}

wjs::SessionData data_of(const wjs::PhaseIndex& phase) {
    return {.dictionary = english(), .phase = &phase};
}

wjs::GameSession make(const wjs::PhaseIndex& phase, const wjs::SessionOptions& options = {}) {
    auto created = wjs::GameSession::create(data_of(phase), phonics(options));
    REQUIRE(created.has_value());
    return *std::move(created);
}

wjs::GameSession make(const wjs::SessionOptions& options = {}) {
    return make(phase_a(), options);
}

using Path = std::vector<Position>;
using wjs::test::text_of;

// The find of a trace that found a word; the case stops here if it found none,
// rather than reading an empty find.
const wjs::Find& found_of(const wjs::TraceOutcome& outcome) {
    REQUIRE(outcome.find.has_value());
    return *outcome.find;
}

// The board of the cases below, 3 by 3, in phase A; `B` is the phase's dead
// letter (the first letter of A-Z that no word uses). Its five words and one path
// for each, by hand:
//
//     P A T      PAT  (0,0) (0,1) (0,2)      TAP  (0,2) (0,1) (0,0)
//     B I N      PIN  (0,0) (1,1) (1,2)      NIP  (1,2) (1,1) (0,0)
//     B B B      PAN  (0,0) (0,1) (1,2)
constexpr std::string_view hand_tiles = "P A T/B I N/B B B";
const Path pat{{0, 0}, {0, 1}, {0, 2}};
const Path tap{{0, 2}, {0, 1}, {0, 0}};
const Path pin{{0, 0}, {1, 1}, {1, 2}};
const Path nip{{1, 2}, {1, 1}, {0, 0}};
const Path pan{{0, 0}, {0, 1}, {1, 2}};
// Real words of the dictionary that are not today's, and some that are not words.
const Path tip{{0, 2}, {1, 1}, {0, 0}};  // TIP
const Path at{{0, 1}, {0, 2}};           // AT, two letters
const Path pab{{0, 0}, {0, 1}, {1, 0}};  // PAB, no word
const Path apart{{0, 0}, {0, 2}};        // not touching: not a path

wjs::BoardRecord hand_record(std::vector<wjs::Find> finds = {}) {
    wjs::BoardRecord record;
    record.mode = wjs::BoardMode::phonics;
    record.phase = "A";
    record.size = BoardSize::three;
    record.tiles = std::string(hand_tiles);
    record.viable = {"NIP", "PAN", "PAT", "PIN", "TAP"};
    record.finds = std::move(finds);
    return record;
}

wjs::SessionSnapshot playing_snapshot(std::vector<wjs::Find> finds = {}) {
    wjs::SessionSnapshot snapshot;
    snapshot.state = SessionState::playing;
    snapshot.mode = wjs::BoardMode::phonics;
    snapshot.size = BoardSize::three;
    snapshot.seed = 77;
    snapshot.board = hand_record(std::move(finds));
    return snapshot;
}

wjs::GameSession restore(const wjs::SessionSnapshot& snapshot,
                         const wjs::SessionOptions& options = {}) {
    auto restored = wjs::GameSession::restore(snapshot, data_of(phase_a()), phonics(options));
    REQUIRE(restored.has_value());
    return *std::move(restored);
}

// A session playing the hand board.
wjs::GameSession hand_session(const wjs::SessionOptions& options = {}) {
    return restore(playing_snapshot(), options);
}

// A session that has dealt a board of its own and is in `state`.
wjs::GameSession reach(SessionState state, const wjs::SessionOptions& options = {}) {
    wjs::GameSession session = make(options);
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
    // Every word the solver finds on the board, traced along its path.
    for (const wjs::FoundWord& word : wjs::solve(*session.board(), phase_a().dictionary())) {
        static_cast<void>(session.trace(word.path, 0));
    }
    REQUIRE(session.state() == SessionState::cleared);
    if (state == SessionState::cleared) {
        return session;
    }
    REQUIRE(session.rest());
    REQUIRE(state == SessionState::resting);
    return session;
}

wjs::Find jewel(std::string word, wjs::Stamp when = 0) {
    return {.word = std::move(word), .assisted = false, .when = when};
}

std::vector<std::string> words_of(std::span<const wjs::Find> finds) {
    std::vector<std::string> words;
    for (const wjs::Find& find : finds) {
        words.push_back(find.word);
    }
    return words;
}

}  // namespace

// --- naming ------------------------------------------------------------------

TEST_CASE("the states are named as the design names its screens", "[session]") {
    CHECK(wjs::to_string(SessionState::box) == "solo.box");
    CHECK(wjs::to_string(SessionState::shaking) == "solo.shaking");
    CHECK(wjs::to_string(SessionState::dropping) == "solo.dropping");
    CHECK(wjs::to_string(SessionState::playing) == "solo.playing");
    CHECK(wjs::to_string(SessionState::cleared) == "solo.cleared");
    CHECK(wjs::to_string(SessionState::resting) == "solo.resting");
}

TEST_CASE("only the sizes of the first release have a name", "[session]") {
    CHECK(wjs::board_size_for(3) == BoardSize::three);
    CHECK(wjs::board_size_for(4) == BoardSize::four);
    CHECK(wjs::board_size_for(5) == BoardSize::five);
    CHECK(wjs::board_size_for(6) == BoardSize::six);
    for (const std::size_t other : {0U, 1U, 2U, 7U, 8U, 16U}) {
        CHECK_FALSE(wjs::board_size_for(other).has_value());
    }
    CHECK(wjs::side(BoardSize::four) == 4U);
    CHECK(wjs::side(BoardSize::six) == 6U);
}

TEST_CASE("the phonics board is played at 3x3 and 4x4, the standard board at all four",
          "[session]") {
    for (const BoardSize size : {BoardSize::three, BoardSize::four}) {
        CHECK(wjs::is_played_at(wjs::BoardMode::phonics, size));
        CHECK(wjs::is_played_at(wjs::BoardMode::standard, size));
    }
    for (const BoardSize size : {BoardSize::five, BoardSize::six}) {
        CHECK_FALSE(wjs::is_played_at(wjs::BoardMode::phonics, size));
        CHECK(wjs::is_played_at(wjs::BoardMode::standard, size));
    }
}

// --- making a session --------------------------------------------------------

TEST_CASE("a new session is the closed box, with nothing on the table", "[session]") {
    const wjs::GameSession session = make();
    CHECK(session.state() == SessionState::box);
    CHECK(session.size() == BoardSize::three);
    CHECK(session.board() == nullptr);
    CHECK(session.dead_tiles().empty());
    CHECK(session.finds().empty());
    CHECK(session.stars() == 0);
    CHECK_FALSE(session.lit_tile().has_value());
    CHECK_FALSE(session.record().has_value());
    CHECK_FALSE(session.last_deal().has_value());
}

TEST_CASE("the first board's size is the option's", "[session]") {
    wjs::SessionOptions options;
    options.size = BoardSize::four;
    CHECK(make(options).size() == BoardSize::four);
}

TEST_CASE("options that cannot make a board are refused", "[session]") {
    wjs::SessionOptions inverted;
    inverted.min_viable = 5;
    inverted.max_viable = 3;
    wjs::SessionOptions none;
    none.max_seedings = 0;
    wjs::SessionOptions empty_range;
    empty_range.min_viable = 0;
    empty_range.max_viable = 0;
    for (const wjs::SessionOptions& options : {inverted, none, empty_range}) {
        const auto created = wjs::GameSession::create(data_of(phase_a()), phonics(options));
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error() == wjs::SessionError::bad_options);
    }
}

TEST_CASE("a phase that uses every letter has none to leave out, and is refused", "[session]") {
    // Two words that between them spell the alphabet: no letter is left for a
    // dead jewel. TEST DATA, with a dictionary of its own.
    std::string graphemes = "# phase: A\n";
    for (char letter = 'a'; letter <= 'z'; ++letter) {
        graphemes += std::string(1, letter) + "  /x/  clip:" + std::string(1, letter) + "\n";
    }
    const std::string words =
        "# phase: A\n"
        "abcdefghijklm  a b c d e f g h i j k l m\n"
        "nopqrstuvwxyz  n o p q r s t u v w x y z\n";
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("abcdefghijklm"));
    REQUIRE(dictionary.insert("nopqrstuvwxyz"));
    std::istringstream graphemes_in{graphemes};
    std::istringstream words_in{words};
    const auto built = wjs::PhaseSet::build({*wjs::parse_graphemes(graphemes_in, "g")},
                                            {*wjs::parse_words(words_in, "w")}, dictionary);
    REQUIRE(built.has_value());

    const auto created =
        wjs::GameSession::create({.dictionary = dictionary, .phase = built->find("A")}, phonics());
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error() == wjs::SessionError::bad_phase);
}

TEST_CASE("a phase with no word of three letters or fewer deals no board and stays in the box",
          "[session]") {
    // The phonics board plants words of three letters at most. TEST DATA.
    const std::string graphemes =
        "# phase: A\na  /x/  clip:a\nb  /x/  clip:b\nc  /x/  clip:c\n"
        "d  /x/  clip:d\n";
    const std::string words = "# phase: A\nabcd  a b c d\n";
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("abcd"));
    std::istringstream graphemes_in{graphemes};
    std::istringstream words_in{words};
    const auto built = wjs::PhaseSet::build({*wjs::parse_graphemes(graphemes_in, "g")},
                                            {*wjs::parse_words(words_in, "w")}, dictionary);
    REQUIRE(built.has_value());

    auto created =
        wjs::GameSession::create({.dictionary = dictionary, .phase = built->find("A")}, phonics());
    REQUIRE(created.has_value());
    const auto shaken = created->shake();
    REQUIRE_FALSE(shaken.has_value());
    CHECK(shaken.error() == wjs::SessionError::no_board);
    CHECK(created->state() == SessionState::box);
    CHECK(created->board() == nullptr);
    CHECK_FALSE(created->record().has_value());
}

// --- every transition --------------------------------------------------------

TEST_CASE("the box leads to shaking, dropping and playing, one event each", "[session]") {
    wjs::GameSession session = make();
    REQUIRE(session.shake().has_value());
    CHECK(session.state() == SessionState::shaking);
    CHECK(session.still());
    CHECK(session.state() == SessionState::dropping);
    CHECK(session.landed());
    CHECK(session.state() == SessionState::playing);
}

TEST_CASE("the board is dealt at the shake, before anything moves", "[session]") {
    for (const BoardSize size : {BoardSize::three, BoardSize::four}) {
        wjs::SessionOptions options;
        options.size = size;
        wjs::GameSession session = make(options);
        CHECK(session.board() == nullptr);
        REQUIRE(session.shake().has_value());
        // In `shaking` already, a finished board.
        REQUIRE(session.board() != nullptr);
        CHECK(session.board()->rows() == wjs::side(size));
        CHECK(session.board()->columns() == wjs::side(size));
        CHECK(session.last_deal().has_value());
    }
}

TEST_CASE("each event applies in its own state and in no other", "[session]") {
    enum class Event { choose_size, shake, still, landed, trace, request_help, rest, again, back };
    struct Row {
        Event event;
        SessionState from;
        SessionState to;
    };
    // The design's flow (docs/design/game.md), one row for each edge. `trace`
    // finds a word, which leaves the state in `playing` on a board with several.
    const std::vector<Row> edges = {
        {Event::choose_size, SessionState::box, SessionState::box},
        {Event::shake, SessionState::box, SessionState::shaking},
        {Event::still, SessionState::shaking, SessionState::dropping},
        {Event::landed, SessionState::dropping, SessionState::playing},
        {Event::trace, SessionState::playing, SessionState::playing},
        {Event::request_help, SessionState::playing, SessionState::playing},
        {Event::rest, SessionState::cleared, SessionState::resting},
        {Event::again, SessionState::resting, SessionState::box},
        {Event::back, SessionState::shaking, SessionState::box},
        {Event::back, SessionState::dropping, SessionState::box},
        {Event::back, SessionState::playing, SessionState::box},
    };
    const auto applies = [&](Event event, SessionState state) {
        return std::ranges::any_of(
            edges, [&](const Row& row) { return row.event == event && row.from == state; });
    };
    const auto next = [&](Event event, SessionState state) {
        for (const Row& row : edges) {
            if (row.event == event && row.from == state) {
                return row.to;
            }
        }
        return state;
    };
    const auto send = [](wjs::GameSession& session, Event event) {
        switch (event) {
            case Event::choose_size:
                return session.choose_size(BoardSize::four);
            case Event::shake:
                return session.shake().has_value();
            case Event::still:
                return session.still();
            case Event::landed:
                return session.landed();
            case Event::trace: {
                // The first word of the board, along its path.
                const auto words = wjs::solve(*session.board(), phase_a().dictionary());
                return session.trace(words.front().path, 1).kind == TraceKind::found;
            }
            case Event::request_help:
                return session.request_help().has_value();
            case Event::rest:
                return session.rest();
            case Event::again:
                return session.again();
            case Event::back:
                return session.back();
        }
        return false;
    };

    constexpr std::array all_events = {Event::choose_size, Event::shake, Event::still,
                                       Event::landed,      Event::trace, Event::request_help,
                                       Event::rest,        Event::again, Event::back};
    for (const SessionState state :
         {SessionState::box, SessionState::shaking, SessionState::dropping, SessionState::playing,
          SessionState::cleared, SessionState::resting}) {
        for (const Event event : all_events) {
            INFO("state " << wjs::to_string(state) << ", event " << static_cast<int>(event));
            wjs::GameSession session = reach(state);
            // `trace` needs a board: in the box there is none to ask the solver
            // about, and the event is not one the box takes.
            if (event == Event::trace && state == SessionState::box) {
                CHECK(session.trace(pat, 1).kind == TraceKind::ignored);
                CHECK(session.state() == SessionState::box);
                continue;
            }
            const auto before = session.snapshot();
            const bool applied = send(session, event);
            CHECK(applied == applies(event, state));
            CHECK(session.state() == next(event, state));
            if (!applied) {
                // A refused event changes nothing.
                CHECK(session.snapshot() == before);
            }
        }
    }
}

TEST_CASE("the size is chosen on the box and nowhere else", "[session]") {
    wjs::GameSession session = make();
    CHECK(session.choose_size(BoardSize::four));
    CHECK(session.size() == BoardSize::four);
    CHECK(session.choose_size(BoardSize::three));
    CHECK(session.size() == BoardSize::three);

    REQUIRE(session.shake().has_value());
    CHECK_FALSE(session.choose_size(BoardSize::four));
    CHECK(session.size() == BoardSize::three);
    CHECK(session.board()->rows() == 3U);
}

TEST_CASE("a shake out of the box is a refused event, not a new deal", "[session]") {
    wjs::GameSession session = reach(SessionState::playing);
    const auto before = session.snapshot();
    const auto shaken = session.shake();
    REQUIRE_FALSE(shaken.has_value());
    CHECK(shaken.error() == wjs::SessionError::wrong_state);
    CHECK(session.snapshot() == before);
}

TEST_CASE("the back control puts the lid back on from shaking, dropping and playing", "[session]") {
    for (const SessionState state :
         {SessionState::shaking, SessionState::dropping, SessionState::playing}) {
        wjs::GameSession session = reach(state);
        CHECK(session.back());
        CHECK(session.state() == SessionState::box);
        CHECK(session.board() == nullptr);
        CHECK(session.finds().empty());
        CHECK(session.stars() == 0);
        // The box can be shaken again.
        CHECK(session.shake().has_value());
    }
}

TEST_CASE("again puts the lid back on and the next shake deals another board", "[session]") {
    wjs::GameSession session = reach(SessionState::resting);
    const std::string first = session.record()->tiles;
    REQUIRE(session.again());
    CHECK(session.state() == SessionState::box);
    CHECK(session.board() == nullptr);
    REQUIRE(session.shake().has_value());
    REQUIRE(session.still());
    REQUIRE(session.landed());
    CHECK(session.finds().empty());
    CHECK(session.stars() == 0);
    // A new board, new tray: the stream has moved on.
    CHECK(session.record()->finds.empty());
    CHECK(session.record()->tiles != first);
}

// --- tracing -----------------------------------------------------------------

TEST_CASE("a viable path is a find, with the stars of its tier", "[session]") {
    wjs::GameSession session = hand_session();
    const wjs::TraceOutcome outcome = session.trace(pat, 4242);
    CHECK(outcome.kind == TraceKind::found);
    REQUIRE(outcome.find.has_value());
    CHECK(found_of(outcome).word == "PAT");
    CHECK(found_of(outcome).when == 4242);
    CHECK_FALSE(found_of(outcome).assisted);
    CHECK(found_of(outcome).tier() == wjs::Tier::spark);
    CHECK(outcome.stars() == 1);
    CHECK_FALSE(outcome.sticker());
    CHECK_FALSE(outcome.cleared);
    CHECK(session.state() == SessionState::playing);
    CHECK(session.stars() == 1);
    CHECK(words_of(session.finds()) == std::vector<std::string>{"PAT"});
}

TEST_CASE("a word read the other way round is the other word", "[session]") {
    wjs::GameSession session = hand_session();
    CHECK(found_of(session.trace(pat, 0)).word == "PAT");
    CHECK(found_of(session.trace(tap, 0)).word == "TAP");
}

TEST_CASE("a word found again is not a new jewel and earns nothing", "[session]") {
    wjs::GameSession session = hand_session();
    REQUIRE(session.trace(pat, 10).kind == TraceKind::found);
    const wjs::TraceOutcome again = session.trace(pat, 20);
    CHECK(again.kind == TraceKind::found_again);
    REQUIRE(again.find.has_value());
    CHECK(found_of(again).word == "PAT");
    CHECK(found_of(again).when == 10);  // the find as it was recorded
    CHECK(again.stars() == 0);
    CHECK_FALSE(again.sticker());
    CHECK(session.stars() == 1);
    CHECK(session.finds().size() == 1U);
}

TEST_CASE("a real word that is not today's gets the neutral cue and no word", "[session]") {
    wjs::GameSession session = hand_session();
    for (const Path* path : {&tip, &at}) {
        const wjs::TraceOutcome outcome = session.trace(*path, 5);
        CHECK(outcome.kind == TraceKind::real_not_todays);
        CHECK_FALSE(outcome.find.has_value());
        CHECK(outcome.stars() == 0);
        CHECK_FALSE(outcome.cleared);
    }
    // Nothing was recorded.
    CHECK(session.finds().empty());
    CHECK(session.stars() == 0);
    CHECK(session.state() == SessionState::playing);
}

TEST_CASE("a real word of two letters is a real word, not 'not a word'", "[session]") {
    // The rules call it not a word because it cannot count (a word under three
    // letters has no tier). The session does not: a child who is told a real
    // word is not one has been taught something false.
    wjs::GameSession session = hand_session();
    CHECK(session.trace(at, 0).kind == TraceKind::real_not_todays);
}

TEST_CASE("anything else fades: not a word, with no word", "[session]") {
    wjs::GameSession session = hand_session();
    for (const Path* path : {&pab, &apart}) {
        const wjs::TraceOutcome outcome = session.trace(*path, 5);
        CHECK(outcome.kind == TraceKind::not_a_word);
        CHECK_FALSE(outcome.find.has_value());
        CHECK(outcome.stars() == 0);
    }
    CHECK(session.finds().empty());
}

TEST_CASE("a path that is not a path is not a word, even if its letters were one", "[session]") {
    wjs::GameSession session = hand_session();
    const Path repeated{{0, 0}, {0, 1}, {0, 0}};           // PAP, a tile twice
    const Path off_board{{0, 0}, {0, 1}, {0, 2}, {0, 3}};  // a tile off the board
    CHECK(session.trace(repeated, 0).kind == TraceKind::not_a_word);
    CHECK(session.trace(off_board, 0).kind == TraceKind::not_a_word);
}

TEST_CASE("a touch on one tile, or nothing, is not a word attempt", "[session]") {
    wjs::GameSession session = hand_session();
    const Path one{{0, 1}};  // A
    for (const Path& path : {Path{}, one}) {
        const wjs::TraceOutcome outcome = session.trace(path, 0);
        CHECK(outcome.kind == TraceKind::ignored);
        CHECK_FALSE(outcome.find.has_value());
    }
}

TEST_CASE("a trace outside `playing` is ignored", "[session]") {
    for (const SessionState state :
         {SessionState::box, SessionState::shaking, SessionState::dropping, SessionState::cleared,
          SessionState::resting}) {
        wjs::GameSession session = reach(state);
        const auto before = session.snapshot();
        const Path along = state == SessionState::box
                               ? pat
                               : wjs::solve(*session.board(), phase_a().dictionary())[0].path;
        CHECK(session.trace(along, 0).kind == TraceKind::ignored);
        CHECK(session.snapshot() == before);
    }
}

TEST_CASE("the tiers pay one, two and five stars and a Power word carries a sticker", "[session]") {
    // Phase B on a board with words of three, four and five letters. 3 by 3,
    // with `B` for the dead letter:
    //
    //     P A I      PAN, PAT, SAT, TAP      three letters
    //     S T N      PAIN                    four letters
    //     B B B      PAINT, SAINT, STAIN     five letters
    wjs::SessionSnapshot snapshot;
    snapshot.state = SessionState::playing;
    snapshot.mode = wjs::BoardMode::phonics;
    wjs::BoardRecord record;
    record.mode = wjs::BoardMode::phonics;
    record.phase = "B";
    record.tiles = "P A I/S T N/B B B";
    record.viable = {"PAIN", "PAINT", "PAN", "PAT", "SAINT", "SAT", "STAIN", "TAP"};
    snapshot.board = record;
    auto restored = wjs::GameSession::restore(snapshot, data_of(phase_b()), phonics());
    REQUIRE(restored.has_value());
    wjs::GameSession& session = *restored;

    const Path pain{{0, 0}, {0, 1}, {0, 2}, {1, 2}};
    const Path paint{{0, 0}, {0, 1}, {0, 2}, {1, 2}, {1, 1}};
    const Path pan_here{{0, 0}, {0, 1}, {1, 2}};

    const auto spark = session.trace(pan_here, 0);
    CHECK(found_of(spark).tier() == wjs::Tier::spark);
    CHECK(spark.stars() == 1);
    CHECK_FALSE(spark.sticker());

    const auto power = session.trace(pain, 0);
    CHECK(found_of(power).word == "PAIN");
    CHECK(found_of(power).tier() == wjs::Tier::power);
    CHECK(power.stars() == 2);
    CHECK(power.sticker());

    const auto mega = session.trace(paint, 0);
    CHECK(found_of(mega).word == "PAINT");
    CHECK(found_of(mega).tier() == wjs::Tier::mega);
    CHECK(mega.stars() == 5);
    CHECK_FALSE(mega.sticker());

    CHECK(session.stars() == 1 + 2 + 5);
}

// --- clearing ----------------------------------------------------------------

TEST_CASE("the last viable word clears the board, and nothing before it does", "[session]") {
    wjs::GameSession session = hand_session();
    for (const Path* path : {&pat, &tap, &pin, &nip}) {
        const wjs::TraceOutcome outcome = session.trace(*path, 0);
        CHECK(outcome.kind == TraceKind::found);
        CHECK_FALSE(outcome.cleared);
        CHECK(session.state() == SessionState::playing);
    }
    const wjs::TraceOutcome last = session.trace(pan, 0);
    CHECK(last.kind == TraceKind::found);
    CHECK(last.cleared);
    CHECK(session.state() == SessionState::cleared);
    CHECK(session.finds().size() == 5U);
    CHECK(session.stars() == 5);
    REQUIRE(session.record().has_value());
    CHECK(session.record()->cleared());
}

TEST_CASE("after the last word nothing is traced, and the celebration then the rest follow",
          "[session]") {
    wjs::GameSession session = hand_session();
    for (const Path* path : {&pat, &tap, &pin, &nip, &pan}) {
        static_cast<void>(session.trace(*path, 0));
    }
    REQUIRE(session.state() == SessionState::cleared);
    CHECK(session.trace(pat, 0).kind == TraceKind::ignored);
    CHECK_FALSE(session.request_help().has_value());
    CHECK_FALSE(session.back());  // the screens leave Solo Play without the session
    CHECK_FALSE(session.again());
    CHECK(session.rest());
    CHECK(session.state() == SessionState::resting);
    CHECK(session.trace(pat, 0).kind == TraceKind::ignored);
    CHECK_FALSE(session.rest());
    CHECK_FALSE(session.back());
    CHECK(session.again());
    CHECK(session.state() == SessionState::box);
}

TEST_CASE("a board dealt by the session can be played to its end by following the solver",
          "[session]") {
    // The solver is what says which words the board holds; the session has to
    // agree with it, word for word, on a board it dealt itself.
    for (const BoardSize size : {BoardSize::three, BoardSize::four}) {
        for (std::uint32_t seed = 1; seed <= 25; ++seed) {
            wjs::SessionOptions options;
            options.size = size;
            options.seed = seed;
            wjs::GameSession session = make(options);
            REQUIRE(session.shake().has_value());
            REQUIRE(session.still());
            REQUIRE(session.landed());

            const auto words = wjs::solve(*session.board(), phase_a().dictionary());
            REQUIRE_FALSE(words.empty());
            std::size_t found = 0;
            for (const wjs::FoundWord& word : words) {
                INFO("size " << wjs::side(size) << ", seed " << seed << ", word " << word.word);
                CHECK(session.state() == SessionState::playing);
                const wjs::TraceOutcome outcome = session.trace(word.path, 0);
                CHECK(outcome.kind == TraceKind::found);
                ++found;
                CHECK(outcome.cleared == (found == words.size()));
            }
            CHECK(session.state() == SessionState::cleared);
            CHECK(session.finds().size() == words.size());
        }
    }
}

// --- the help ----------------------------------------------------------------

TEST_CASE("the help lights the start tile of the shortest word nearest the top left", "[session]") {
    wjs::GameSession session = hand_session();
    // All five words are three letters. The nearest first tile to the top left
    // is (0,0): PAN, PAT, PIN start there; the word that sorts first is PAN. The
    // lit tile is that word's first tile.
    const auto tile = session.request_help();
    REQUIRE(tile.has_value());
    CHECK(*tile == Position{0, 0});
    CHECK(session.lit_tile() == tile);

    // The path is the word's own, from the same first tile: the second rung of
    // the help reads its first two tiles without being given the word.
    const std::vector<Position> path = session.lit_path();
    REQUIRE(path.size() >= 3);
    CHECK(path.front() == *tile);
}

TEST_CASE("the same press gives the same tile until its word is found", "[session]") {
    wjs::GameSession session = hand_session();
    const auto first = session.request_help();
    REQUIRE(first.has_value());
    CHECK(session.request_help() == first);
    // Finding another word does not move it.
    REQUIRE(session.trace(tap, 0).kind == TraceKind::found);
    CHECK(session.lit_tile() == first);
    CHECK(session.request_help() == first);
}

TEST_CASE("a word found while its tile is lit is an assisted find, and the light goes out",
          "[session]") {
    wjs::GameSession session = hand_session();
    REQUIRE(session.request_help().has_value());  // PAN, from (0,0)
    const wjs::TraceOutcome helped = session.trace(pan, 0);
    REQUIRE(helped.find.has_value());
    CHECK(found_of(helped).word == "PAN");
    CHECK(found_of(helped).assisted);
    CHECK_FALSE(session.lit_tile().has_value());

    // The next press names the next word, and a word found without it is not
    // assisted.
    REQUIRE(session.request_help().has_value());  // PAT
    const wjs::TraceOutcome unhelped = session.trace(tap, 0);
    CHECK_FALSE(found_of(unhelped).assisted);
    REQUIRE(session.record().has_value());
    CHECK(session.record()->finds[0].assisted);
    CHECK_FALSE(session.record()->finds[1].assisted);
}

TEST_CASE("the help answers with a tile and never a word", "[session]") {
    // The only thing the session hands out about the help is a position: the
    // child does the finding. The word it is for is in the snapshot, because
    // the light has to come back with the board.
    wjs::GameSession session = hand_session();
    REQUIRE(session.request_help().has_value());
    const auto snapshot = session.snapshot();
    REQUIRE(snapshot.lit.has_value());
    CHECK(*snapshot.lit == "PAN");
}

TEST_CASE("the words left of a start count the ones that begin with it", "[session]") {
    wjs::GameSession session = hand_session();
    REQUIRE(session.request_help().has_value());  // PAN, from (0,0)

    // PAN, PAT and PIN all begin at (0,0); NIP begins at (1,2). Nothing is
    // found yet, so the count is all of them.
    CHECK(session.words_with_start(std::array<Position, 1>{Position{0, 0}}) == 3);
    CHECK(session.words_with_start(std::array<Position, 1>{Position{1, 2}}) == 1);
    CHECK(session.words_with_start({}) == 0);

    // The first two tiles narrow the count to the words that share them: PAN
    // and PAT both run P-A, while PIN runs P-I.
    const std::vector<Position> path = session.lit_path();
    REQUIRE(path.size() >= 2);
    const std::array<Position, 2> two{path[0], path[1]};
    CHECK(session.words_with_start(two) == 2);

    // A find is not among the ones left.
    REQUIRE(session.trace(pan, 0).kind == TraceKind::found);
    CHECK(session.words_with_start(std::array<Position, 1>{Position{0, 0}}) == 2);

    // The help put away by hand: the light goes out, and nothing else changes.
    REQUIRE(session.request_help().has_value());  // PAT
    session.dismiss_help();
    CHECK_FALSE(session.lit_tile().has_value());
    CHECK(session.words_with_start(std::array<Position, 1>{Position{0, 0}}) == 2);
}

TEST_CASE("with the parent's switch off the help gives nothing", "[session]") {
    wjs::SessionOptions options;
    options.help = false;
    wjs::GameSession session = hand_session(options);
    CHECK_FALSE(session.request_help().has_value());
    CHECK_FALSE(session.lit_tile().has_value());
}

TEST_CASE("the help follows every word of the board to its end", "[session]") {
    // Ask, trace the word the light is for (the snapshot names it), and ask
    // again: each word is lit once, found assisted, and the board clears.
    const std::map<std::string, const Path*> path_of = {
        {"NIP", &nip}, {"PAN", &pan}, {"PAT", &pat}, {"PIN", &pin}, {"TAP", &tap}};
    wjs::GameSession session = hand_session();
    std::vector<std::string> order;
    while (session.request_help()) {
        // A help that never ends is a failure, not a hung test run.
        REQUIRE(order.size() < path_of.size());
        const std::string word = *session.snapshot().lit;
        order.push_back(word);
        const wjs::TraceOutcome outcome = session.trace(*path_of.at(word), 0);
        CHECK(outcome.kind == TraceKind::found);
        CHECK(found_of(outcome).assisted);
    }
    // Shortest first, then nearest the top left, then alphabetical: PAN, PAT, PIN
    // start at the top left tile, then NIP (1,2) is nearer than TAP (0,2).
    CHECK(order == std::vector<std::string>{"PAN", "PAT", "PIN", "TAP", "NIP"});
    CHECK(session.state() == SessionState::cleared);
}

TEST_CASE("there is no help outside `playing`", "[session]") {
    for (const SessionState state :
         {SessionState::box, SessionState::shaking, SessionState::dropping, SessionState::cleared,
          SessionState::resting}) {
        wjs::GameSession session = reach(state);
        CHECK_FALSE(session.request_help().has_value());
        CHECK_FALSE(session.lit_tile().has_value());
    }
}

// --- dealing: determinism and the range --------------------------------------

namespace {

// Everything a session shows after a shake, played through: a string two
// sessions can be compared by.
std::string play_one(wjs::GameSession& session) {
    REQUIRE(session.shake().has_value());
    REQUIRE(session.still());
    REQUIRE(session.landed());
    std::string log = session.record()->tiles + "|";
    for (const wjs::FoundWord& word : wjs::solve(*session.board(), phase_a().dictionary())) {
        const auto help = session.request_help();
        if (help) {
            log += std::to_string(help->row) + "," + std::to_string(help->column) + ";";
        }
        const wjs::TraceOutcome outcome = session.trace(word.path, 9);
        log += std::string(wjs::to_string(outcome.kind)) + ":" + found_of(outcome).word + ":" +
               std::to_string(outcome.stars()) + ";";
    }
    REQUIRE(session.rest());
    REQUIRE(session.again());
    return log;
}

}  // namespace

TEST_CASE("the same seed deals the same boards and gives the same outcomes", "[session]") {
    for (const BoardSize size : {BoardSize::three, BoardSize::four}) {
        wjs::SessionOptions options;
        options.size = size;
        options.seed = 2024;
        wjs::GameSession one = make(options);
        wjs::GameSession two = make(options);
        for (int board = 0; board < 6; ++board) {
            const std::string played = play_one(one);
            CHECK(played == play_one(two));
            CHECK(one.snapshot() == two.snapshot());
            CHECK(one.last_deal() == two.last_deal());
        }
    }
}

TEST_CASE("a session is the same however it is copied", "[session]") {
    wjs::SessionOptions options;
    options.seed = 5;
    wjs::GameSession original = make(options);
    wjs::GameSession copy = original;
    CHECK(play_one(original) == play_one(copy));
    CHECK(original.snapshot() == copy.snapshot());
}

TEST_CASE("different seeds deal different boards", "[session]") {
    std::set<std::string> boards;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        wjs::SessionOptions options;
        options.size = BoardSize::four;
        options.seed = seed;
        wjs::GameSession session = make(options);
        REQUIRE(session.shake().has_value());
        REQUIRE(session.still());
        REQUIRE(session.landed());
        boards.insert(session.record()->tiles);
    }
    CHECK(boards.size() >= 30U);
}

TEST_CASE("one seed is a whole run of boards, and they are not all the same", "[session]") {
    wjs::SessionOptions options;
    options.seed = 99;
    wjs::GameSession session = make(options);
    std::set<std::string> boards;
    for (int board = 0; board < 20; ++board) {
        REQUIRE(session.shake().has_value());
        REQUIRE(session.still());
        REQUIRE(session.landed());
        boards.insert(session.record()->tiles);
        REQUIRE(session.back());
    }
    CHECK(boards.size() >= 12U);
}

namespace {

// Property-style: many seeds of one size, the phase's own words. The count the
// board holds is checked against the solver, not against the session's own
// list. The seeds are dealt in ranges, each its own test case, so that ctest
// runs the ranges at once: the 600 deals were the longest case under the
// sanitizers.
void check_boards_are_dealt_with_a_word(BoardSize size, std::uint32_t first, std::uint32_t last) {
    std::size_t in_range = 0;
    std::size_t boards = 0;
    for (std::uint32_t seed = first; seed <= last; ++seed) {
        wjs::SessionOptions options;
        options.size = size;
        options.seed = seed;
        wjs::GameSession session = make(options);
        REQUIRE(session.shake().has_value());
        REQUIRE(session.still());
        REQUIRE(session.landed());

        const auto words = wjs::solve(*session.board(), phase_a().dictionary());
        const wjs::DealReport& deal = *session.last_deal();
        INFO("size " << wjs::side(size) << ", seed " << seed);
        CHECK(words.size() >= 1U);  // never none
        CHECK(deal.viable == words.size());
        CHECK(deal.in_range == (words.size() >= 3U && words.size() <= 5U));
        CHECK(session.record()->viable.size() == words.size());
        CHECK(deal.seedings >= 1U);
        CHECK(deal.seedings <= wjs::SessionOptions{}.max_seedings);
        ++boards;
        if (deal.in_range) {
            ++in_range;
        }
    }
    // The test data is a handful of words, which the seeding lands nearly every
    // time (docs/testing.md, "Seeding: what was measured"); several seedings
    // make a miss rarer still.
    INFO(in_range << " of " << boards << " in range");
    CHECK(in_range == boards);
}

}  // namespace

TEST_CASE(
    "a board is never dealt with no word on it, and three to five is what it lands: 3x3, seeds 1 "
    "to 100",
    "[session]") {
    check_boards_are_dealt_with_a_word(BoardSize::three, 1, 100);
}

TEST_CASE(
    "a board is never dealt with no word on it, and three to five is what it lands: 3x3, seeds 101 "
    "to 200",
    "[session]") {
    check_boards_are_dealt_with_a_word(BoardSize::three, 101, 200);
}

TEST_CASE(
    "a board is never dealt with no word on it, and three to five is what it lands: 3x3, seeds 201 "
    "to 300",
    "[session]") {
    check_boards_are_dealt_with_a_word(BoardSize::three, 201, 300);
}

TEST_CASE(
    "a board is never dealt with no word on it, and three to five is what it lands: 4x4, seeds 1 "
    "to 100",
    "[session]") {
    check_boards_are_dealt_with_a_word(BoardSize::four, 1, 100);
}

TEST_CASE(
    "a board is never dealt with no word on it, and three to five is what it lands: 4x4, seeds 101 "
    "to 200",
    "[session]") {
    check_boards_are_dealt_with_a_word(BoardSize::four, 101, 200);
}

TEST_CASE(
    "a board is never dealt with no word on it, and three to five is what it lands: 4x4, seeds 201 "
    "to 300",
    "[session]") {
    check_boards_are_dealt_with_a_word(BoardSize::four, 201, 300);
}

TEST_CASE("the range and the number of seedings are the options'", "[session]") {
    wjs::SessionOptions options;
    options.max_seedings = 1;
    options.min_viable = 2;
    options.max_viable = 2;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        options.seed = seed;
        wjs::GameSession session = make(options);
        REQUIRE(session.shake().has_value());
        const wjs::DealReport& deal = *session.last_deal();
        CHECK(deal.seedings == 1U);
        CHECK(deal.in_range == (deal.viable == 2U));
        CHECK(deal.viable >= 1U);
    }
}

TEST_CASE("a seeding that misses the range is tried again, and the nearest is kept", "[session]") {
    // An impossible range: no board of this phase holds nine words, so every
    // seeding misses. The session spends all its seedings and keeps the board
    // nearest the range, which still holds a word.
    wjs::SessionOptions options;
    options.min_viable = 9;
    options.max_viable = 9;
    options.max_seedings = 4;
    wjs::GameSession session = make(options);
    REQUIRE(session.shake().has_value());
    const wjs::DealReport& deal = *session.last_deal();
    CHECK(deal.seedings == 4U);
    CHECK_FALSE(deal.in_range);
    CHECK(deal.viable >= 1U);
    CHECK(deal.viable < 9U);
}

TEST_CASE("the board kept is the nearest of the seedings, the first of two as near", "[session]") {
    // The same four seedings made by hand, from the same stream of seeds: the
    // session keeps the board of the one nearest the range, and the earlier one
    // when two are as near. Phase B holds eleven words in all, so no board lands in
    // a range of twelve, and the seedings differ in how near they come.
    wjs::SessionOptions options;
    options.min_viable = 12;
    options.max_viable = 12;
    options.max_seedings = 4;
    auto by_hand = wjs::phonics_board_options(phase_b(), 3);
    REQUIRE(by_hand.has_value());
    by_hand->min_viable = 12;
    by_hand->max_viable = 12;

    std::size_t later_than_first = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        options.seed = seed;
        std::uint32_t expected_seed = seed;
        std::string expected_tiles;
        std::size_t expected_distance = 0;
        std::size_t expected_viable = 0;
        std::uint32_t stream = seed;
        for (std::size_t seeding = 0; seeding < options.max_seedings; ++seeding) {
            const auto seeded = wjs::seed_board(phase_b(), *by_hand, wjs::RecentTargets{}, stream);
            REQUIRE(seeded.has_value());
            const std::size_t viable = seeded->viable.size();
            const std::size_t distance = viable > 12 ? viable - 12 : 12 - viable;
            if (seeding == 0 || distance < expected_distance) {
                expected_distance = distance;
                expected_viable = viable;
                expected_seed = stream;
                expected_tiles = text_of(seeded->board);
            }
            stream = wjs::next_deal_seed(stream);
        }
        if (expected_seed != seed) {
            ++later_than_first;
        }

        wjs::GameSession session = make(phase_b(), options);
        REQUIRE(session.shake().has_value());
        INFO("seed " << seed);
        CHECK(session.last_deal()->seedings == 4U);
        CHECK(session.last_deal()->seed == expected_seed);
        CHECK(session.last_deal()->viable == expected_viable);
        REQUIRE(session.still());
        REQUIRE(session.landed());
        CHECK(text_of(*session.board()) == expected_tiles);
    }
    // The cases above are not all the first seeding: the nearest is not simply the first.
    CHECK(later_than_first > 0);
}

TEST_CASE("the focus sounds the parent ticked reach the seeding", "[session]") {
    // Boards prefer targets with a grapheme that makes a ticked sound. `n` is
    // /t3/ in this test data, and NIP, PAN and PIN are the words with it: every
    // board holds one.
    wjs::SessionOptions options;
    options.focus_sounds = {"n"};
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        options.seed = seed;
        wjs::GameSession session = make(options);
        REQUIRE(session.shake().has_value());
        REQUIRE(session.still());
        REQUIRE(session.landed());
        const std::vector<std::string> viable = session.record()->viable;
        INFO("seed " << seed);
        CHECK(std::ranges::any_of(viable, [](const std::string& word) {
            return word == "NIP" || word == "PAN" || word == "PIN";
        }));
    }
}

TEST_CASE("a focus sound ticked in the settings is dealt, from the settings to the board",
          "[session]") {
    // The path the app takes: what the parent ticked goes through
    // Settings::normalized (which writes letters in lower case) and into the
    // session's options, and the dealt boards carry the grapheme. A target is a
    // word of three letters at most, so the grapheme is one that NIP, PAN and
    // PIN have.
    wjs::Settings settings;
    settings.focus_sounds = {"N"};
    settings = settings.normalized();
    REQUIRE(settings.focus_sounds == std::vector<std::string>{"n"});

    wjs::SessionOptions options;
    options.focus_sounds = settings.focus_sounds;
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        options.seed = seed;
        wjs::GameSession session = make(options);
        REQUIRE(session.shake().has_value());
        REQUIRE(session.still());
        REQUIRE(session.landed());
        const std::vector<std::string> viable = session.record()->viable;
        INFO("seed " << seed);
        CHECK(std::ranges::any_of(viable, [](const std::string& word) {
            return word == "NIP" || word == "PAN" || word == "PIN";
        }));
    }
}

TEST_CASE("the dead jewels are the tiles of the letter the phase leaves out", "[session]") {
    for (const BoardSize size : {BoardSize::three, BoardSize::four}) {
        for (std::uint32_t seed = 1; seed <= 50; ++seed) {
            wjs::SessionOptions options;
            options.size = size;
            options.seed = seed;
            wjs::GameSession session = make(options);
            REQUIRE(session.shake().has_value());
            const wjs::Board& board = *session.board();
            std::vector<Position> dead;
            for (std::size_t row = 0; row < board.rows(); ++row) {
                for (std::size_t column = 0; column < board.columns(); ++column) {
                    if (board.tile({row, column}) == "B") {
                        dead.push_back({row, column});
                    }
                }
            }
            const auto listed = session.dead_tiles();
            CHECK(std::ranges::equal(dead, listed));
        }
    }
}

// --- the store ---------------------------------------------------------------

TEST_CASE("the record holds the board, the words on it and the finds", "[session]") {
    wjs::GameSession session = hand_session();
    static_cast<void>(session.trace(pan, 7));
    static_cast<void>(session.trace(tap, 8));
    const auto record = session.record();
    REQUIRE(record.has_value());
    CHECK(record->phase == "A");
    CHECK(record->size == BoardSize::three);
    CHECK(record->tiles == "P A T/B I N/B B B");
    CHECK(record->viable == std::vector<std::string>{"NIP", "PAN", "PAT", "PIN", "TAP"});
    REQUIRE(record->finds.size() == 2U);
    CHECK(record->finds[0].word == "PAN");
    CHECK(record->finds[0].when == 7);
    CHECK(record->finds[1].word == "TAP");
    CHECK(record->stars() == 2);
    CHECK_FALSE(record->cleared());
}

TEST_CASE("the record of a board that is left is still there to be saved", "[session]") {
    wjs::GameSession session = reach(SessionState::playing);
    const auto tiles = session.record()->tiles;
    const auto word = wjs::solve(*session.board(), phase_a().dictionary())[0];
    static_cast<void>(session.trace(word.path, 3));
    REQUIRE(session.back());
    REQUIRE(session.record().has_value());
    CHECK(session.record()->tiles == tiles);
    CHECK(session.record()->finds.size() == 1U);
    CHECK_FALSE(session.record()->cleared());

    // Until the next shake begins another board.
    REQUIRE(session.shake().has_value());
    CHECK_FALSE(session.record().has_value());
    REQUIRE(session.back());
    // A board backed out of before the dice landed was never seen: no record.
    CHECK_FALSE(session.record().has_value());
}

TEST_CASE("a board that was cleared says so in its record", "[session]") {
    wjs::GameSession session = reach(SessionState::resting);
    REQUIRE(session.record().has_value());
    CHECK(session.record()->cleared());
    CHECK(session.record()->finds.size() == session.record()->viable.size());
}

TEST_CASE("a snapshot brings a session back as it was, in each state", "[session]") {
    for (const SessionState state :
         {SessionState::box, SessionState::playing, SessionState::resting}) {
        wjs::GameSession session = reach(state);
        if (state == SessionState::playing) {
            const auto words = wjs::solve(*session.board(), phase_a().dictionary());
            static_cast<void>(session.trace(words[0].path, 12));
            static_cast<void>(session.request_help());
        }
        const wjs::SessionSnapshot snapshot = session.snapshot();
        CHECK(snapshot.state == state);

        auto restored = wjs::GameSession::restore(snapshot, data_of(phase_a()), phonics());
        REQUIRE(restored.has_value());
        CHECK(restored->state() == state);
        CHECK(restored->snapshot() == snapshot);
        CHECK(restored->stars() == session.stars());
        CHECK(words_of(restored->finds()) == words_of(session.finds()));
        CHECK(restored->lit_tile() == session.lit_tile());
        CHECK(std::ranges::equal(restored->dead_tiles(), session.dead_tiles()));

        // From the box the two go on the same way: the same next board. (After
        // a board the original remembers its targets and a restored session does
        // not, so the one after may differ; the snapshot does not keep them.)
        if (state == SessionState::box) {
            REQUIRE(restored->shake().has_value());
            REQUIRE(session.shake().has_value());
            CHECK(restored->snapshot() == session.snapshot());
            REQUIRE(restored->still());
            REQUIRE(session.still());
            REQUIRE(restored->landed());
            REQUIRE(session.landed());
            CHECK(restored->record()->tiles == session.record()->tiles);
        }
    }
}

TEST_CASE("a snapshot in shaking or dropping is the box, with the seed moved on", "[session]") {
    for (const SessionState state : {SessionState::shaking, SessionState::dropping}) {
        wjs::GameSession session = reach(state);
        const wjs::SessionSnapshot snapshot = session.snapshot();
        CHECK(snapshot.state == SessionState::box);
        CHECK_FALSE(snapshot.board.has_value());
        CHECK_FALSE(snapshot.lit.has_value());
        CHECK(snapshot.seed != wjs::SessionOptions{}.seed);
    }
}

TEST_CASE("a snapshot in cleared is saved as resting", "[session]") {
    wjs::GameSession session = reach(SessionState::cleared);
    CHECK(session.snapshot().state == SessionState::resting);
}

TEST_CASE("a restored session keeps the lit tile and the finds' flags", "[session]") {
    wjs::GameSession session = hand_session();
    REQUIRE(session.request_help().has_value());
    static_cast<void>(session.trace(pan, 5));  // assisted
    static_cast<void>(session.trace(tap, 6));  // not
    REQUIRE(session.request_help().has_value());
    const auto lit = session.lit_tile();

    auto restored = wjs::GameSession::restore(session.snapshot(), data_of(phase_a()), phonics());
    REQUIRE(restored.has_value());
    CHECK(restored->lit_tile() == lit);
    REQUIRE(restored->finds().size() == 2U);
    CHECK(restored->finds()[0].assisted);
    CHECK_FALSE(restored->finds()[1].assisted);
    CHECK(restored->finds()[0].when == 5);
    // The light goes out when its word is found, as it did before.
    const auto next = restored->request_help();
    REQUIRE(next.has_value());
}

TEST_CASE("restore reads the size and the seed from the snapshot, not the options", "[session]") {
    wjs::SessionOptions options;
    options.size = BoardSize::four;
    options.seed = 1;
    wjs::SessionSnapshot snapshot;
    snapshot.size = BoardSize::three;
    snapshot.seed = 123456;
    const wjs::GameSession session = restore(snapshot, options);
    CHECK(session.size() == BoardSize::three);
    CHECK(session.snapshot().seed == 123456U);
}

TEST_CASE("a snapshot of another phase is refused", "[session]") {
    auto snapshot = playing_snapshot();
    snapshot.board->phase = "B";
    const auto restored = wjs::GameSession::restore(snapshot, data_of(phase_a()), phonics());
    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error() == wjs::SessionError::wrong_phase);
}

TEST_CASE("a board the phase no longer reads the same way is refused", "[session]") {
    // The record says four words; the phase on the board finds five.
    auto snapshot = playing_snapshot();
    snapshot.board->viable = {"PAN", "PAT", "PIN", "TAP"};
    const auto restored = wjs::GameSession::restore(snapshot, data_of(phase_a()), phonics());
    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error() == wjs::SessionError::board_changed);

    // A board with no word in it at all is no board to play.
    auto none = playing_snapshot();
    none.board->tiles = "B B B/B B B/B B B";
    none.board->viable = {};
    const auto empty = wjs::GameSession::restore(none, data_of(phase_a()), phonics());
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error() == wjs::SessionError::board_changed);
}

TEST_CASE("a snapshot that no session makes is refused", "[session]") {
    const auto refused = [](const wjs::SessionSnapshot& snapshot) {
        const auto restored = wjs::GameSession::restore(snapshot, data_of(phase_a()), phonics());
        return restored.has_value() ? std::nullopt : std::optional(restored.error());
    };
    const auto bad = std::optional(wjs::SessionError::bad_snapshot);

    SECTION("a state the store never holds") {
        for (const SessionState state :
             {SessionState::shaking, SessionState::dropping, SessionState::cleared}) {
            auto snapshot = playing_snapshot();
            snapshot.state = state;
            CHECK(refused(snapshot) == bad);
        }
    }
    SECTION("a board in the box, or none in play") {
        auto boxed = playing_snapshot();
        boxed.state = SessionState::box;
        CHECK(refused(boxed) == bad);
        auto missing = playing_snapshot();
        missing.board.reset();
        CHECK(refused(missing) == bad);
        auto resting = playing_snapshot();
        resting.state = SessionState::resting;
        resting.board.reset();
        CHECK(refused(resting) == bad);
    }
    SECTION("a light in the box") {
        wjs::SessionSnapshot snapshot;
        snapshot.lit = "PAT";
        CHECK(refused(snapshot) == bad);
    }
    SECTION("tiles that are not the size of the board") {
        auto snapshot = playing_snapshot();
        snapshot.board->tiles = "P A/B I";
        CHECK(refused(snapshot) == bad);
        auto garbled = playing_snapshot();
        garbled.board->tiles = "P A T/B I";
        CHECK(refused(garbled) == bad);
        auto unreadable = playing_snapshot();
        unreadable.board->tiles = "P A 7/B I N/B B B";
        CHECK(refused(unreadable) == bad);
    }
    SECTION("a record of one size on a snapshot of another") {
        auto snapshot = playing_snapshot();
        snapshot.size = BoardSize::four;
        CHECK(refused(snapshot) == bad);
    }
    SECTION("a find that is not on the board") {
        // SAT is a word of the phase, but not one this board holds.
        auto snapshot = playing_snapshot({jewel("SAT")});
        CHECK(refused(snapshot) == bad);
        auto dictionary_only = playing_snapshot({jewel("TIP")});
        CHECK(refused(dictionary_only) == bad);
    }
    SECTION("a word found twice") {
        auto snapshot = playing_snapshot({jewel("PAT"), jewel("pat", 1)});
        CHECK(refused(snapshot) == bad);
    }
    SECTION("a board that is playing but has every word found, or resting with some left") {
        auto all = playing_snapshot(
            {jewel("NIP"), jewel("PAN"), jewel("PAT"), jewel("PIN"), jewel("TAP")});
        CHECK(refused(all) == bad);
        auto resting = playing_snapshot({jewel("PAT")});
        resting.state = SessionState::resting;
        CHECK(refused(resting) == bad);
    }
    SECTION("a light on a word already found, one not on the board, or in a board at rest") {
        auto found = playing_snapshot({jewel("PAT")});
        found.lit = "PAT";
        CHECK(refused(found) == bad);
        auto away = playing_snapshot();
        away.lit = "SAT";
        CHECK(refused(away) == bad);
        auto at_rest = playing_snapshot(
            {jewel("NIP"), jewel("PAN"), jewel("PAT"), jewel("PIN"), jewel("TAP")});
        at_rest.state = SessionState::resting;
        at_rest.lit = "PAT";
        CHECK(refused(at_rest) == bad);
    }
}

TEST_CASE("more snapshots that no session makes are refused, each for its own reason",
          "[session]") {
    const auto refused = [](const wjs::SessionSnapshot& snapshot) {
        const auto restored = wjs::GameSession::restore(snapshot, data_of(phase_a()), phonics());
        return restored.has_value() ? std::nullopt : std::optional(restored.error());
    };

    SECTION("a total that is not the words listed") {
        auto snapshot = playing_snapshot();
        snapshot.board->viable_total = snapshot.board->viable.size() + 1;
        CHECK(refused(snapshot) == std::optional(wjs::SessionError::bad_snapshot));
        // The same total as the list is what a session writes.
        auto same = playing_snapshot();
        same.board->viable_total = same.board->viable.size();
        CHECK(refused(same) == std::nullopt);
    }
    SECTION("the phase named in another case is another phase") {
        auto snapshot = playing_snapshot();
        snapshot.board->phase = "a";
        CHECK(refused(snapshot) == std::optional(wjs::SessionError::wrong_phase));
    }
    SECTION("a board of the other mode, however the snapshot is labelled") {
        auto snapshot = playing_snapshot();
        snapshot.mode = wjs::BoardMode::standard;
        snapshot.board->mode = wjs::BoardMode::standard;
        snapshot.board->phase.clear();
        CHECK(refused(snapshot) == std::optional(wjs::SessionError::wrong_mode));
    }
    SECTION("a box with a board in it") {
        auto snapshot = playing_snapshot();
        snapshot.state = SessionState::box;
        snapshot.lit.reset();
        CHECK(refused(snapshot) == std::optional(wjs::SessionError::bad_snapshot));
    }
    SECTION("a board with no tiles at all") {
        auto snapshot = playing_snapshot();
        snapshot.board->tiles.clear();
        CHECK(refused(snapshot) == std::optional(wjs::SessionError::bad_snapshot));
    }
    SECTION("a light in a board at rest") {
        auto snapshot = playing_snapshot(
            {jewel("NIP"), jewel("PAN"), jewel("PAT"), jewel("PIN"), jewel("TAP")});
        snapshot.state = SessionState::resting;
        CHECK(refused(snapshot) == std::nullopt);
        snapshot.lit = "NIP";
        CHECK(refused(snapshot) == std::optional(wjs::SessionError::bad_snapshot));
    }
}

TEST_CASE("a find in the snapshot may be in either case and comes back upper-case", "[session]") {
    const wjs::GameSession session = restore(playing_snapshot({jewel("pat", 3)}));
    REQUIRE(session.finds().size() == 1U);
    CHECK(session.finds()[0].word == "PAT");
    CHECK(session.finds()[0].when == 3);
}

// --- what the session never says ---------------------------------------------

TEST_CASE("no path of the board is answered wrongly, or names a word that is not today's",
          "[session]") {
    // Every simple path of two to nine tiles on the hand board, found by walking
    // the grid here and spelling each path from the tiles, and the answer held to
    // what the two word lists say of the spelling. A fresh session for each path,
    // so that one find does not change the answer to the next.
    const std::array<std::string, 9> tile = {"P", "A", "T", "B", "I", "N", "B", "B", "B"};
    const std::set<std::string> today = {"NIP", "PAN", "PAT", "PIN", "TAP"};

    std::size_t paths = 0;
    std::size_t named = 0;
    Path path;
    std::array<bool, 9> used{};
    std::string spelled;

    const auto check = [&] {
        wjs::GameSession session = hand_session();
        const wjs::TraceOutcome outcome = session.trace(path, 0);
        INFO("path of " << path.size() << " tiles spelling " << spelled);
        ++paths;
        if (today.contains(spelled)) {
            CHECK(outcome.kind == TraceKind::found);
            REQUIRE(outcome.find.has_value());
            CHECK(found_of(outcome).word == spelled);
            ++named;
            return;
        }
        // Nothing but a word of today's is ever named.
        CHECK_FALSE(outcome.find.has_value());
        if (english().contains(spelled)) {
            // A real word, of any length, is never "not a word".
            CHECK(outcome.kind == TraceKind::real_not_todays);
        } else {
            CHECK(outcome.kind == TraceKind::not_a_word);
        }
        // And nothing was kept.
        CHECK(session.finds().empty());
        CHECK(session.stars() == 0);
        const auto saved = session.snapshot();
        CHECK(saved.board->finds.empty());
        CHECK_FALSE(saved.lit.has_value());
    };

    const auto walk = [&](auto&& self, std::size_t row, std::size_t column) -> void {
        const std::size_t index = row * 3 + column;
        used[index] = true;
        path.push_back({row, column});
        spelled += tile[index];
        if (path.size() >= 2) {
            check();
        }
        for (std::size_t next_row = row == 0 ? 0 : row - 1;
             next_row <= std::min<std::size_t>(row + 1, 2); ++next_row) {
            for (std::size_t next_column = column == 0 ? 0 : column - 1;
                 next_column <= std::min<std::size_t>(column + 1, 2); ++next_column) {
                if (!used[next_row * 3 + next_column]) {
                    self(self, next_row, next_column);
                }
            }
        }
        spelled.pop_back();
        path.pop_back();
        used[index] = false;
    };
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            walk(walk, row, column);
        }
    }
    // 10,305 simple paths on a 3 by 3 board, 9 of them one tile long.
    CHECK(paths == 10'296U);
    CHECK(named >= 5U);
}

TEST_CASE("a word that is only in the dictionary is never handed out", "[session]") {
    // `tip` and `tab` stand for words a child must never meet. Trace them, ask
    // for help, clear the board, and read everything the session holds.
    wjs::GameSession session = hand_session();
    const Path tab{{0, 2}, {0, 1}, {1, 0}};  // T A B, a real word in this dictionary
    CHECK(session.trace(tip, 0).kind == TraceKind::real_not_todays);
    CHECK(session.trace(tab, 0).kind == TraceKind::real_not_todays);
    for (const Path* path : {&pan, &pat, &pin, &nip, &tap}) {
        static_cast<void>(session.request_help());
        const wjs::TraceOutcome outcome = session.trace(*path, 0);
        CHECK(outcome.kind == TraceKind::found);
        CHECK_FALSE(found_of(outcome).word == "TIP");
    }
    REQUIRE(session.state() == SessionState::cleared);

    const std::set<std::string> outside = {"TIP", "TAB", "PIT", "NAP", "ANT",
                                           "TAN", "AT",  "IN",  "IT"};
    const wjs::SessionSnapshot snapshot = session.snapshot();
    REQUIRE(snapshot.board.has_value());
    for (const wjs::Find& find : snapshot.board->finds) {
        CHECK_FALSE(outside.contains(find.word));
    }
    for (const std::string& word : snapshot.board->viable) {
        CHECK_FALSE(outside.contains(word));
    }
    for (const wjs::Find& find : session.finds()) {
        CHECK_FALSE(outside.contains(find.word));
    }
    CHECK_FALSE(snapshot.lit.has_value());
}

TEST_CASE("everything a dealt board holds is a word of the phase", "[session]") {
    for (const BoardSize size : {BoardSize::three, BoardSize::four}) {
        for (std::uint32_t seed = 1; seed <= 60; ++seed) {
            wjs::SessionOptions options;
            options.size = size;
            options.seed = seed;
            wjs::GameSession session = make(options);
            REQUIRE(session.shake().has_value());
            REQUIRE(session.still());
            REQUIRE(session.landed());
            for (const std::string& word : session.record()->viable) {
                INFO("word " << word);
                CHECK(phase_a().dictionary().contains(word));
            }
        }
    }
}

// --- the standard board ------------------------------------------------------
//
// The standard board is the default mode: a roll of dice, and every word of the
// dictionary of three letters or more counts (docs/design/mvp-library.md). The
// cases below play the session of the cases above under BoardMode::standard.
//
// TEST DATA. Dice that show only the letters of this file's words, and a board
// written out by hand. The cases about the project's own dice and the full
// dictionary read the shipped files and say so.

namespace {

using wjs::BoardMode;

constexpr std::string_view data_directory = WJS_DATA_DIRECTORY;

// One die for every tile of every size, with the letters the dictionary of this
// file can spell and a third of the faces a letter it cannot, so that some rolls
// hold words and some hold none.
wjs::DiceSets test_dice_of(std::string_view faces) {
    std::vector<wjs::DiceSet> sets;
    for (std::size_t side = wjs::DiceSets::smallest_side; side <= wjs::DiceSets::largest_side;
         ++side) {
        std::string text;
        for (std::size_t die = 0; die < side * side; ++die) {
            text += std::string(faces) + "\n";
        }
        auto set = wjs::DiceSet::parse(text);
        REQUIRE(set.has_value());
        sets.push_back(*std::move(set));
    }
    auto made = wjs::DiceSets::make(std::move(sets));
    REQUIRE(made.has_value());
    return *std::move(made);
}

const wjs::DiceSets& test_dice() {
    static const wjs::DiceSets dice = test_dice_of("P A T I N S X X X");
    return dice;
}

wjs::SessionData standard_data() {
    return {.dictionary = english(), .dice = &test_dice()};
}

wjs::GameSession make_standard(const wjs::SessionOptions& options = {}) {
    auto created = wjs::GameSession::create(standard_data(), options);
    REQUIRE(created.has_value());
    return *std::move(created);
}

// The board of the cases below, 3 by 3, the same letters as the phonics board
// above in the same places, with `X` where that has `B`. Its twelve words, and
// one path for each (the five of phase A have theirs above):
//
//     P A T      ANT  (0,1) (1,2) (0,2)      PAIN   (0,0) (0,1) (1,1) (1,2)
//     X I N      NAP  (1,2) (0,1) (0,0)      PAINT  (0,0) (0,1) (1,1) (1,2) (0,2)
//     X X X      PIT  (0,0) (1,1) (0,2)      TAN    (0,2) (0,1) (1,2)      TIP, above
//
// The dictionary also has AT, IN and IT, which are on it and are too short.
constexpr std::string_view standard_tiles = "P A T/X I N/X X X";
const Path pit_path{{0, 0}, {1, 1}, {0, 2}};
const Path nap_path{{1, 2}, {0, 1}, {0, 0}};
const Path ant_path{{0, 1}, {1, 2}, {0, 2}};
const Path tan_path{{0, 2}, {0, 1}, {1, 2}};
const Path pain_path{{0, 0}, {0, 1}, {1, 1}, {1, 2}};
const Path paint_path{{0, 0}, {0, 1}, {1, 1}, {1, 2}, {0, 2}};
const Path in_path{{1, 1}, {1, 2}};           // IN, two letters
const Path it_path{{1, 1}, {0, 2}};           // IT, two letters
const Path pax_path{{0, 0}, {0, 1}, {1, 0}};  // PAX, no word

struct HandWord {
    std::string_view word;
    const Path* path;
};
const std::array<HandWord, 12> standard_hand_words = {{
    {"ANT", &ant_path},
    {"NAP", &nap_path},
    {"NIP", &nip},
    {"PAIN", &pain_path},
    {"PAINT", &paint_path},
    {"PAN", &pan},
    {"PAT", &pat},
    {"PIN", &pin},
    {"PIT", &pit_path},
    {"TAN", &tan_path},
    {"TAP", &tap},
    {"TIP", &tip},
}};

wjs::BoardRecord standard_record(std::vector<wjs::Find> finds = {}) {
    wjs::BoardRecord record;
    record.mode = BoardMode::standard;
    record.size = BoardSize::three;
    record.tiles = std::string(standard_tiles);
    record.viable_total = standard_hand_words.size();
    record.finds = std::move(finds);
    return record;
}

wjs::SessionSnapshot standard_snapshot(std::vector<wjs::Find> finds = {}) {
    wjs::SessionSnapshot snapshot;
    snapshot.state = SessionState::playing;
    snapshot.mode = BoardMode::standard;
    snapshot.size = BoardSize::three;
    snapshot.seed = 77;
    snapshot.board = standard_record(std::move(finds));
    return snapshot;
}

wjs::GameSession restore_standard(const wjs::SessionSnapshot& snapshot,
                                  const wjs::SessionOptions& options = {}) {
    auto restored = wjs::GameSession::restore(snapshot, standard_data(), options);
    REQUIRE(restored.has_value());
    return *std::move(restored);
}

// A session playing the standard hand board.
wjs::GameSession standard_hand_session(const wjs::SessionOptions& options = {}) {
    return restore_standard(standard_snapshot(), options);
}

// A fresh standard session dealt to `playing`.
wjs::GameSession standard_playing(const wjs::SessionOptions& options) {
    wjs::GameSession session = make_standard(options);
    REQUIRE(session.shake().has_value());
    REQUIRE(session.still());
    REQUIRE(session.landed());
    return session;
}

}  // namespace

TEST_CASE("the standard board is the default, and it needs dice and no phase",
          "[session][standard]") {
    CHECK(wjs::SessionOptions{}.mode == BoardMode::standard);

    const wjs::GameSession session = make_standard();
    CHECK(session.mode() == BoardMode::standard);
    CHECK(session.state() == SessionState::box);
    CHECK(session.size() == BoardSize::three);
    CHECK(session.board() == nullptr);

    // Without the dice the default cannot be played, whatever else is given.
    const auto no_dice = wjs::GameSession::create({.dictionary = english()});
    REQUIRE_FALSE(no_dice.has_value());
    CHECK(no_dice.error() == wjs::SessionError::bad_dice);
    const auto phase_only = wjs::GameSession::create(data_of(phase_a()));
    REQUIRE_FALSE(phase_only.has_value());
    CHECK(phase_only.error() == wjs::SessionError::bad_dice);
}

TEST_CASE("an app that holds both the dice and a phase plays either, as the parent's switch says",
          "[session][standard]") {
    const wjs::SessionData both{.dictionary = english(), .dice = &test_dice(), .phase = &phase_a()};

    const auto standard = wjs::GameSession::create(both, {});
    REQUIRE(standard.has_value());
    CHECK(standard->mode() == BoardMode::standard);

    const auto phonics_board = wjs::GameSession::create(both, phonics());
    REQUIRE(phonics_board.has_value());
    CHECK(phonics_board->mode() == BoardMode::phonics);

    // And the phonics board still needs its phase.
    const auto no_phase = wjs::GameSession::create(standard_data(), phonics());
    REQUIRE_FALSE(no_phase.has_value());
    CHECK(no_phase.error() == wjs::SessionError::bad_phase);
}

TEST_CASE("each mode refuses the options it cannot play and ignores the other's",
          "[session][standard]") {
    wjs::SessionOptions no_rolls;
    no_rolls.max_rolls = 0;
    const auto refused = wjs::GameSession::create(standard_data(), no_rolls);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == wjs::SessionError::bad_options);

    // The phonics board's range and seedings are not the standard board's.
    wjs::SessionOptions phonics_only;
    phonics_only.min_viable = 9;
    phonics_only.max_viable = 1;
    phonics_only.max_seedings = 0;
    CHECK(wjs::GameSession::create(standard_data(), phonics_only).has_value());

    // And the standard board's rolls are not the phonics board's.
    CHECK(wjs::GameSession::create(data_of(phase_a()), phonics(no_rolls)).has_value());

    // The phonics board is played at 3x3 and 4x4; the standard at 3x3 to 6x6.
    for (const BoardSize size : {BoardSize::five, BoardSize::six}) {
        wjs::SessionOptions options;
        options.size = size;
        const auto five = wjs::GameSession::create(data_of(phase_a()), phonics(options));
        REQUIRE_FALSE(five.has_value());
        CHECK(five.error() == wjs::SessionError::bad_options);
        const auto standard = wjs::GameSession::create(standard_data(), options);
        REQUIRE(standard.has_value());
        CHECK(standard->size() == size);
    }
}

TEST_CASE("the size is chosen in the box, 3x3 to 6x6 on the standard board",
          "[session][standard]") {
    wjs::GameSession session = make_standard();
    for (const BoardSize size :
         {BoardSize::four, BoardSize::five, BoardSize::six, BoardSize::three}) {
        CHECK(session.choose_size(size));
        CHECK(session.size() == size);
    }

    // Nowhere else.
    REQUIRE(session.shake().has_value());
    CHECK_FALSE(session.choose_size(BoardSize::six));
    CHECK(session.size() == BoardSize::three);

    // The phonics board refuses the sizes it is not designed for, and keeps its own.
    wjs::GameSession phonics_board = make();
    CHECK_FALSE(phonics_board.choose_size(BoardSize::five));
    CHECK_FALSE(phonics_board.choose_size(BoardSize::six));
    CHECK(phonics_board.size() == BoardSize::three);
    CHECK(phonics_board.choose_size(BoardSize::four));
}

TEST_CASE("a standard board is dealt at the chosen size, from the dice, with no dead jewels",
          "[session][standard]") {
    for (const BoardSize size :
         {BoardSize::three, BoardSize::four, BoardSize::five, BoardSize::six}) {
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
            wjs::SessionOptions options;
            options.size = size;
            options.seed = seed;
            wjs::GameSession session = make_standard(options);
            REQUIRE(session.shake().has_value());
            REQUIRE(session.board() != nullptr);
            INFO("size " << wjs::side(size) << ", seed " << seed);
            CHECK(session.board()->rows() == wjs::side(size));
            CHECK(session.board()->columns() == wjs::side(size));
            CHECK(session.dead_tiles().empty());
            // Every tile is a face of the dice.
            for (std::size_t row = 0; row < session.board()->rows(); ++row) {
                for (std::size_t column = 0; column < session.board()->columns(); ++column) {
                    CHECK(std::string_view("PATINSX").find(session.board()->tile({row, column})) !=
                          std::string_view::npos);
                }
            }
        }
    }
}

TEST_CASE("the deal is one solve a roll, and the board holds the words the solver finds",
          "[session][standard]") {
    for (const BoardSize size :
         {BoardSize::three, BoardSize::four, BoardSize::five, BoardSize::six}) {
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
            wjs::SessionOptions options;
            options.size = size;
            options.seed = seed;
            wjs::GameSession session = standard_playing(options);
            INFO("size " << wjs::side(size) << ", seed " << seed);

            const auto& deal = session.last_deal();
            REQUIRE(deal.has_value());
            // The standard deal rolls: it seeds nothing and verifies nothing.
            CHECK(deal->rolls >= 1U);
            CHECK(deal->rolls <= options.max_rolls);
            CHECK(deal->seedings == 0U);
            CHECK(deal->tries == 0U);
            CHECK(deal->solves == deal->rolls);
            CHECK(deal->in_range);

            // The words that count are the dictionary's, counted by the solver.
            const auto solved = wjs::solve(*session.board(), english());
            CHECK(deal->viable == solved.size());
            CHECK(deal->viable >= 1U);
            const auto record = session.record();
            REQUIRE(record.has_value());
            CHECK(record->viable_total == solved.size());
            CHECK(record->viable_count() == solved.size());
        }
    }
}

TEST_CASE("the deal is the library's deal of the standard board, seed for seed",
          "[session][standard]") {
    const wjs::WordSets words = wjs::WordSets::standard(english());
    for (const BoardSize size : {BoardSize::three, BoardSize::four}) {
        for (std::uint32_t seed = 1; seed <= 30; ++seed) {
            wjs::SessionOptions options;
            options.size = size;
            options.seed = seed;
            wjs::GameSession session = make_standard(options);
            REQUIRE(session.shake().has_value());

            const auto dealt = wjs::deal_standard_board(*test_dice().find(wjs::side(size)), words,
                                                        seed, {.min_viable = 1, .max_rolls = 20});
            REQUIRE(dealt.has_value());
            INFO("size " << wjs::side(size) << ", seed " << seed);
            CHECK(text_of(*session.board()) == text_of(dealt->board));
            CHECK(session.last_deal()->rolls == dealt->rolls);
            CHECK(session.last_deal()->seed == dealt->seed);
            CHECK(session.last_deal()->viable == dealt->viable.size());
        }
    }
}

TEST_CASE("a roll that holds no word is rolled again, and never dealt", "[session][standard]") {
    // A seed whose first roll holds no word of this dictionary: found by asking
    // the dice and the solver, not the session.
    const wjs::WordSets words = wjs::WordSets::standard(english());
    std::optional<std::uint32_t> empty_seed;
    for (std::uint32_t seed = 1; seed <= 2000 && !empty_seed; ++seed) {
        const auto rolled = wjs::roll(*test_dice().find(3), seed);
        REQUIRE(rolled.has_value());
        if (wjs::viable_words(*rolled, words).empty()) {
            empty_seed = seed;
        }
    }
    REQUIRE(empty_seed.has_value());

    wjs::SessionOptions options;
    options.seed = *empty_seed;
    wjs::GameSession session = standard_playing(options);
    CHECK(session.last_deal()->rolls > 1U);
    CHECK(session.last_deal()->solves == session.last_deal()->rolls);
    CHECK(session.last_deal()->seed != *empty_seed);
    CHECK(session.last_deal()->viable >= 1U);
    CHECK_FALSE(wjs::viable_words(*session.board(), words).empty());
}

TEST_CASE("no board that holds a word can be rolled: no board, and the box stays shut",
          "[session][standard]") {
    // A word list with no word the dice can spell.
    wjs::Dictionary other;
    REQUIRE(other.insert("zebra"));
    wjs::GameSession session = [&] {
        auto created = wjs::GameSession::create({.dictionary = other, .dice = &test_dice()}, {});
        REQUIRE(created.has_value());
        return *std::move(created);
    }();

    const auto shaken = session.shake();
    REQUIRE_FALSE(shaken.has_value());
    CHECK(shaken.error() == wjs::SessionError::no_board);
    CHECK(session.state() == SessionState::box);
    CHECK(session.board() == nullptr);
    CHECK_FALSE(session.record().has_value());
    CHECK_FALSE(session.last_deal().has_value());
    // The seed did not move: the deal is all or nothing.
    CHECK(session.snapshot().seed == 1U);

    // And max_rolls is the number of boards made before it gives up.
    wjs::SessionOptions one_roll;
    one_roll.max_rolls = 1;
    std::size_t refused = 0;
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        wjs::SessionOptions options = one_roll;
        options.seed = seed;
        auto made = make_standard(options);
        const auto result = made.shake();
        if (!result) {
            CHECK(result.error() == wjs::SessionError::no_board);
            ++refused;
        } else {
            CHECK(made.last_deal()->rolls == 1U);
        }
    }
    // Some single rolls of these dice hold no word, and those are refused.
    CHECK(refused > 0U);
}

TEST_CASE("the same seed deals the same standard boards, and one seed is a run of them",
          "[session][standard]") {
    for (const BoardSize size : {BoardSize::three, BoardSize::six}) {
        wjs::SessionOptions options;
        options.size = size;
        options.seed = 2024;
        wjs::GameSession one = make_standard(options);
        wjs::GameSession two = make_standard(options);
        std::set<std::string> boards;
        for (int board = 0; board < 8; ++board) {
            REQUIRE(one.shake().has_value());
            REQUIRE(two.shake().has_value());
            CHECK(text_of(*one.board()) == text_of(*two.board()));
            CHECK(one.last_deal() == two.last_deal());
            CHECK(one.snapshot() == two.snapshot());
            boards.insert(text_of(*one.board()));
            // The next board is rolled from the seed after the one that made this.
            CHECK(one.snapshot().seed == wjs::next_deal_seed(one.last_deal()->seed));
            REQUIRE(one.back());
            REQUIRE(two.back());
        }
        CHECK(boards.size() >= 6U);
    }
}

TEST_CASE("a standard session is the same however it is copied", "[session][standard]") {
    wjs::SessionOptions options;
    options.seed = 5;
    wjs::GameSession original = make_standard(options);
    wjs::GameSession copy = original;
    REQUIRE(original.shake().has_value());
    REQUIRE(copy.shake().has_value());
    CHECK(text_of(*original.board()) == text_of(*copy.board()));
    CHECK(original.snapshot() == copy.snapshot());
}

TEST_CASE("every word of the dictionary of three letters or more is a find on a standard board",
          "[session][standard]") {
    // The hand board holds what the solver says it does.
    const auto board = wjs::Board::parse(standard_tiles);
    REQUIRE(board.has_value());
    std::set<std::string> solved;
    for (const wjs::FoundWord& word : wjs::solve(*board, english())) {
        solved.insert(word.word);
    }
    std::set<std::string> written;
    for (const HandWord& hand : standard_hand_words) {
        written.insert(std::string(hand.word));
    }
    CHECK(solved == written);

    // And each is a find on the session, by its own path.
    for (const HandWord& hand : standard_hand_words) {
        wjs::GameSession session = standard_hand_session();
        const wjs::TraceOutcome outcome = session.trace(*hand.path, 4);
        INFO("word " << hand.word);
        CHECK(outcome.kind == TraceKind::found);
        REQUIRE(outcome.find.has_value());
        CHECK(found_of(outcome).word == hand.word);
        CHECK(found_of(outcome).when == 4);
        CHECK(outcome.stars() >= 1);
    }
}

TEST_CASE("the same trace is a tick on the phonics board and a find on the standard",
          "[session][standard]") {
    // TIP is a real word that is not phase A's, so the phonics board does not
    // count it; with the dictionary as the word list, the standard board does.
    wjs::GameSession phonics_board = hand_session();
    const wjs::TraceOutcome ticked = phonics_board.trace(tip, 0);
    CHECK(ticked.kind == TraceKind::real_not_todays);
    CHECK_FALSE(ticked.find.has_value());
    CHECK(phonics_board.stars() == 0);

    wjs::GameSession standard = standard_hand_session();
    const wjs::TraceOutcome found = standard.trace(tip, 0);
    CHECK(found.kind == TraceKind::found);
    REQUIRE(found.find.has_value());
    CHECK(found_of(found).word == "TIP");
    CHECK(found.stars() == 1);
    CHECK(standard.stars() == 1);
}

TEST_CASE("the standard board pays the tiers as the phonics board does", "[session][standard]") {
    wjs::GameSession session = standard_hand_session();

    const auto spark = session.trace(pat, 0);
    CHECK(found_of(spark).tier() == wjs::Tier::spark);
    CHECK(spark.stars() == 1);
    CHECK_FALSE(spark.sticker());

    const auto power = session.trace(pain_path, 0);
    CHECK(found_of(power).tier() == wjs::Tier::power);
    CHECK(power.stars() == 2);
    CHECK(power.sticker());

    const auto mega = session.trace(paint_path, 0);
    CHECK(found_of(mega).tier() == wjs::Tier::mega);
    CHECK(mega.stars() == 5);
    CHECK_FALSE(mega.sticker());

    CHECK(session.stars() == 8);
    CHECK(words_of(session.finds()) == std::vector<std::string>{"PAT", "PAIN", "PAINT"});
}

TEST_CASE("on the standard board only a real word of fewer than three letters is a tick",
          "[session][standard]") {
    wjs::GameSession session = standard_hand_session();
    for (const Path* path : {&at, &in_path, &it_path}) {
        const wjs::TraceOutcome outcome = session.trace(*path, 0);
        CHECK(outcome.kind == TraceKind::real_not_todays);
        CHECK_FALSE(outcome.find.has_value());
        CHECK_FALSE(outcome.cleared);
        CHECK(outcome.stars() == 0);
    }
    CHECK(session.trace(pax_path, 0).kind == TraceKind::not_a_word);
    CHECK(session.trace(apart, 0).kind == TraceKind::not_a_word);
    const Path one_tile{{0, 0}};  // a touch to hear a letter
    CHECK(session.trace(one_tile, 0).kind == TraceKind::ignored);
    CHECK(session.finds().empty());
    CHECK(session.stars() == 0);
}

TEST_CASE("a word found again on the standard board is not a new jewel", "[session][standard]") {
    wjs::GameSession session = standard_hand_session();
    REQUIRE(session.trace(tip, 1).kind == TraceKind::found);
    const wjs::TraceOutcome again = session.trace(pit_path, 2);
    CHECK(again.kind == TraceKind::found);  // PIT is another word
    const wjs::TraceOutcome same = session.trace(tip, 3);
    CHECK(same.kind == TraceKind::found_again);
    REQUIRE(same.find.has_value());
    CHECK(found_of(same).when == 1);
    CHECK(same.stars() == 0);
    CHECK(session.finds().size() == 2U);
    CHECK(session.stars() == 2);
}

TEST_CASE("the standard board is cleared by every word of the dictionary on it",
          "[session][standard]") {
    wjs::GameSession session = standard_hand_session();
    for (std::size_t which = 0; which < standard_hand_words.size(); ++which) {
        CHECK(session.state() == SessionState::playing);
        const wjs::TraceOutcome outcome = session.trace(*standard_hand_words[which].path, 0);
        REQUIRE(outcome.kind == TraceKind::found);
        CHECK(outcome.cleared == (which + 1 == standard_hand_words.size()));
    }
    CHECK(session.state() == SessionState::cleared);
    // Ten words of three letters, one of four and one of five.
    CHECK(session.stars() == 10 * 1 + 1 * 2 + 1 * 5);
    CHECK(session.record()->cleared());
    CHECK(session.trace(pat, 0).kind == TraceKind::ignored);
    CHECK(session.rest());
    CHECK(session.again());
    CHECK(session.state() == SessionState::box);
}

// --- the standard board: the help ----------------------------------------------

TEST_CASE("the help of the standard board follows every word to its end", "[session][standard]") {
    // Ask, trace the word the light is for (the snapshot names it), and ask
    // again: each word is lit once, found assisted, and the board clears.
    std::map<std::string, const Path*> path_of;
    for (const HandWord& hand : standard_hand_words) {
        path_of[std::string(hand.word)] = hand.path;
    }
    wjs::GameSession session = standard_hand_session();
    std::vector<std::string> order;
    while (const auto tile = session.request_help()) {
        REQUIRE(order.size() < path_of.size());  // a help that never ends is a failure
        const std::string word = *session.snapshot().lit;
        order.push_back(word);
        // The tile that lights is the first tile of the word.
        CHECK(*tile == path_of.at(word)->front());
        CHECK(session.lit_tile() == tile);
        const wjs::TraceOutcome outcome = session.trace(*path_of.at(word), 0);
        CHECK(outcome.kind == TraceKind::found);
        CHECK(found_of(outcome).assisted);
    }
    // The shortest words first, the nearest the top left first among them, then
    // alphabetical: PAN, PAT, PIN and PIT start at the top left tile, ANT is
    // next to it, TAN, TAP and TIP start at (0,2), NAP and NIP at (1,2); then the
    // word of four letters and the word of five.
    CHECK(order == std::vector<std::string>{"PAN", "PAT", "PIN", "PIT", "ANT", "TAN", "TAP", "TIP",
                                            "NAP", "NIP", "PAIN", "PAINT"});
    CHECK(session.state() == SessionState::cleared);
}

TEST_CASE("the help of the standard board stays on its word until that word is found",
          "[session][standard]") {
    wjs::GameSession session = standard_hand_session();
    const auto first = session.request_help();
    REQUIRE(first.has_value());
    CHECK(session.request_help() == first);
    // Another word found: the light stays, and the find is not assisted.
    const wjs::TraceOutcome other = session.trace(tip, 0);
    CHECK(other.kind == TraceKind::found);
    CHECK_FALSE(found_of(other).assisted);
    CHECK(session.lit_tile() == first);
    // With the parent's switch off there is none.
    wjs::SessionOptions off;
    off.help = false;
    wjs::GameSession quiet = standard_hand_session(off);
    CHECK_FALSE(quiet.request_help().has_value());
}

// --- the standard board: the record and the snapshot -----------------------------

TEST_CASE("a standard board's record says its mode, has no phase and counts its words",
          "[session][standard]") {
    wjs::GameSession session = standard_hand_session();
    static_cast<void>(session.trace(tip, 7));
    static_cast<void>(session.trace(pain_path, 8));

    const auto record = session.record();
    REQUIRE(record.has_value());
    CHECK(record->mode == BoardMode::standard);
    CHECK(record->phase.empty());
    CHECK(record->size == BoardSize::three);
    CHECK(record->tiles == standard_tiles);
    // A board of the dictionary holds tens to hundreds of words, and nothing lists them.
    CHECK(record->viable.empty());
    CHECK(record->viable_total == 12U);
    CHECK(record->viable_count() == 12U);
    REQUIRE(record->finds.size() == 2U);
    CHECK(record->finds[0] == jewel("TIP", 7));
    CHECK(record->finds[1] == jewel("PAIN", 8));
    CHECK(record->stars() == 3);
    CHECK_FALSE(record->cleared());
}

TEST_CASE("a phonics board's record says its mode and its phase and lists its words",
          "[session][standard]") {
    const auto record = hand_session().record();
    REQUIRE(record.has_value());
    CHECK(record->mode == BoardMode::phonics);
    CHECK(record->phase == "A");
    CHECK(record->viable == std::vector<std::string>{"NIP", "PAN", "PAT", "PIN", "TAP"});
    CHECK(record->viable_total == 0U);
    CHECK(record->viable_count() == 5U);
}

TEST_CASE("a standard board's record is cleared when its finds are the words it held",
          "[session][standard]") {
    wjs::BoardRecord record = standard_record();
    CHECK_FALSE(record.cleared());
    for (std::size_t found = 0; found < record.viable_total; ++found) {
        record.finds.push_back(jewel("ANT"));
    }
    CHECK(record.cleared());
    // A board that held no word is not a cleared one.
    wjs::BoardRecord none;
    CHECK_FALSE(none.cleared());
}

TEST_CASE("a standard snapshot brings a session back as it was, in each state",
          "[session][standard]") {
    for (const SessionState state :
         {SessionState::box, SessionState::playing, SessionState::resting}) {
        wjs::GameSession session = make_standard();
        if (state != SessionState::box) {
            REQUIRE(session.shake().has_value());
            REQUIRE(session.still());
            REQUIRE(session.landed());
            const auto words = wjs::solve(*session.board(), english());
            if (state == SessionState::playing) {
                static_cast<void>(session.trace(words[0].path, 12));
                static_cast<void>(session.request_help());
            } else {
                for (const wjs::FoundWord& word : words) {
                    static_cast<void>(session.trace(word.path, 12));
                }
                REQUIRE(session.state() == SessionState::cleared);
                REQUIRE(session.rest());
            }
        }
        const wjs::SessionSnapshot snapshot = session.snapshot();
        INFO("state " << wjs::to_string(state));
        CHECK(snapshot.state == state);
        CHECK(snapshot.mode == BoardMode::standard);

        auto restored = wjs::GameSession::restore(snapshot, standard_data(), {});
        REQUIRE(restored.has_value());
        CHECK(restored->mode() == BoardMode::standard);
        CHECK(restored->state() == state);
        CHECK(restored->snapshot() == snapshot);
        CHECK(restored->stars() == session.stars());
        CHECK(words_of(restored->finds()) == words_of(session.finds()));
        CHECK(restored->lit_tile() == session.lit_tile());
        CHECK(restored->dead_tiles().empty());
        CHECK(restored->record() == session.record());
        if (state != SessionState::box) {
            REQUIRE(restored->board() != nullptr);
            CHECK(text_of(*restored->board()) == text_of(*session.board()));
        }

        // A standard board has no recent targets to forget, so from the box, and
        // from the end of a board, the two go on the same way.
        if (state != SessionState::playing) {
            if (state == SessionState::resting) {
                REQUIRE(restored->again());
                REQUIRE(session.again());
            }
            REQUIRE(restored->shake().has_value());
            REQUIRE(session.shake().has_value());
            CHECK(restored->snapshot() == session.snapshot());
            CHECK(text_of(*restored->board()) == text_of(*session.board()));
        }
    }
}

TEST_CASE("a standard snapshot in shaking or dropping is the box, with the seed moved on",
          "[session][standard]") {
    wjs::SessionOptions options;
    options.seed = 41;
    for (const SessionState state : {SessionState::shaking, SessionState::dropping}) {
        wjs::GameSession session = make_standard(options);
        REQUIRE(session.shake().has_value());
        if (state == SessionState::dropping) {
            REQUIRE(session.still());
        }
        const wjs::SessionSnapshot snapshot = session.snapshot();
        CHECK(snapshot.state == SessionState::box);
        CHECK_FALSE(snapshot.board.has_value());
        CHECK(snapshot.seed == wjs::next_deal_seed(session.last_deal()->seed));
        CHECK(snapshot.seed != 41U);
    }
}

TEST_CASE("a standard restore reads the size and the seed from the snapshot, not the options",
          "[session][standard]") {
    wjs::SessionOptions options;
    options.size = BoardSize::four;
    options.seed = 1;
    wjs::SessionSnapshot snapshot;
    snapshot.size = BoardSize::six;
    snapshot.seed = 123456;
    const wjs::GameSession session = restore_standard(snapshot, options);
    CHECK(session.size() == BoardSize::six);
    CHECK(session.snapshot().seed == 123456U);
    CHECK(session.snapshot().mode == BoardMode::standard);
}

TEST_CASE("a snapshot of the other board mode is refused", "[session][standard]") {
    const auto under_phonics =
        wjs::GameSession::restore(standard_snapshot(), data_of(phase_a()), phonics());
    REQUIRE_FALSE(under_phonics.has_value());
    CHECK(under_phonics.error() == wjs::SessionError::wrong_mode);

    const auto under_standard = wjs::GameSession::restore(playing_snapshot(), standard_data(), {});
    REQUIRE_FALSE(under_standard.has_value());
    CHECK(under_standard.error() == wjs::SessionError::wrong_mode);

    // An app that holds the dice and the phase both is refused the same.
    const wjs::SessionData both{.dictionary = english(), .dice = &test_dice(), .phase = &phase_a()};
    const auto standard_under_phonics =
        wjs::GameSession::restore(standard_snapshot(), both, phonics());
    REQUIRE_FALSE(standard_under_phonics.has_value());
    CHECK(standard_under_phonics.error() == wjs::SessionError::wrong_mode);
    CHECK(wjs::GameSession::restore(standard_snapshot(), both, {}).has_value());
    CHECK(wjs::GameSession::restore(playing_snapshot(), both, phonics()).has_value());
}

TEST_CASE("a game in the box comes back whichever mode it is played in", "[session][standard]") {
    // The store keeps no mode for a game with no board on the table: the parent
    // may have switched since, and the size and the seed are all there is.
    wjs::SessionSnapshot box;
    box.state = SessionState::box;
    box.size = BoardSize::four;
    box.seed = 9;
    const auto standard = wjs::GameSession::restore(box, standard_data(), {});
    REQUIRE(standard.has_value());
    CHECK(standard->mode() == BoardMode::standard);
    const auto phonics_board = wjs::GameSession::restore(box, data_of(phase_a()), phonics());
    REQUIRE(phonics_board.has_value());
    CHECK(phonics_board->mode() == BoardMode::phonics);
    CHECK(phonics_board->size() == BoardSize::four);

    // But a size the phonics board is not played at cannot be dealt from.
    box.size = BoardSize::five;
    CHECK(wjs::GameSession::restore(box, standard_data(), {}).has_value());
    const auto five = wjs::GameSession::restore(box, data_of(phase_a()), phonics());
    REQUIRE_FALSE(five.has_value());
    CHECK(five.error() == wjs::SessionError::bad_snapshot);
}

TEST_CASE("a standard snapshot that no session makes is refused", "[session][standard]") {
    const auto refused = [](const wjs::SessionSnapshot& snapshot) {
        const auto restored = wjs::GameSession::restore(snapshot, standard_data(), {});
        return restored.has_value() ? std::nullopt : std::optional(restored.error());
    };
    const auto bad = std::optional(wjs::SessionError::bad_snapshot);
    const auto changed = std::optional(wjs::SessionError::board_changed);

    SECTION("a snapshot and a board of two modes") {
        auto snapshot = standard_snapshot();
        snapshot.mode = BoardMode::phonics;
        CHECK(refused(snapshot) == bad);
    }
    SECTION("a phase on a board that has none") {
        auto snapshot = standard_snapshot();
        snapshot.board->phase = "A";
        CHECK(refused(snapshot) == bad);
    }
    SECTION("a list of words, which a standard board never has") {
        auto snapshot = standard_snapshot();
        snapshot.board->viable = {"PAT"};
        CHECK(refused(snapshot) == bad);
    }
    SECTION("no count of words") {
        auto snapshot = standard_snapshot();
        snapshot.board->viable_total = 0;
        CHECK(refused(snapshot) == bad);
    }
    SECTION("a count that is not the words the board holds") {
        for (const std::size_t total : {1U, 11U, 13U, 400U}) {
            auto snapshot = standard_snapshot();
            snapshot.board->viable_total = total;
            INFO("total " << total);
            CHECK(refused(snapshot) == changed);
        }
    }
    SECTION("a board that holds no word") {
        auto none = standard_snapshot();
        none.board->tiles = "X X X/X X X/X X X";
        none.board->viable_total = 1;
        CHECK(refused(none) == changed);
    }
    SECTION("a board in the box, or none in play") {
        auto boxed = standard_snapshot();
        boxed.state = SessionState::box;
        CHECK(refused(boxed) == bad);
        auto missing = standard_snapshot();
        missing.board.reset();
        CHECK(refused(missing) == bad);
    }
    SECTION("tiles that are not the size of the board") {
        auto snapshot = standard_snapshot();
        snapshot.board->tiles = "P A/X I";
        CHECK(refused(snapshot) == bad);
        auto record_four = standard_snapshot();
        record_four.board->size = BoardSize::four;
        CHECK(refused(record_four) == bad);
    }
    SECTION("a find that is not on the board, or is not a word that counts") {
        auto away = standard_snapshot({jewel("SAT")});
        CHECK(refused(away) == bad);
        // IN is on the board and a real word, but two letters is no word of the game.
        auto short_word = standard_snapshot({jewel("IN")});
        CHECK(refused(short_word) == bad);
        auto twice = standard_snapshot({jewel("TIP"), jewel("tip", 1)});
        CHECK(refused(twice) == bad);
    }
    SECTION("a board that is playing with every word found, or resting with some left") {
        std::vector<wjs::Find> all;
        for (const HandWord& hand : standard_hand_words) {
            all.push_back(jewel(std::string(hand.word)));
        }
        auto playing = standard_snapshot(all);
        CHECK(refused(playing) == bad);
        auto resting = standard_snapshot({jewel("TIP")});
        resting.state = SessionState::resting;
        CHECK(refused(resting) == bad);
        auto cleared = standard_snapshot(all);
        cleared.state = SessionState::resting;
        CHECK(refused(cleared) == std::nullopt);
    }
    SECTION("a light on a word already found, or one not on the board") {
        auto found = standard_snapshot({jewel("TIP")});
        found.lit = "TIP";
        CHECK(refused(found) == bad);
        auto away = standard_snapshot();
        away.lit = "SAT";
        CHECK(refused(away) == bad);
        auto lit = standard_snapshot();
        lit.lit = "TIP";
        CHECK(refused(lit) == std::nullopt);
    }
}

// --- the standard board: a tile of two letters ------------------------------------

TEST_CASE("a tile of two letters counts both on the standard board, and goes through a snapshot",
          "[session][standard]") {
    // QU I T / X E X: QUIT is four letters and QUITE five, as the tiers count them.
    wjs::Dictionary quiet;
    REQUIRE(quiet.insert("quit"));
    REQUIRE(quiet.insert("quite"));
    const wjs::SessionData data{.dictionary = quiet, .dice = &test_dice()};
    wjs::SessionSnapshot snapshot = standard_snapshot();
    snapshot.board->tiles = "QU I T/X E X/X X X";
    snapshot.board->viable_total = 2;
    auto restored = wjs::GameSession::restore(snapshot, data, {});
    REQUIRE(restored.has_value());
    wjs::GameSession& session = *restored;

    const Path quit{{0, 0}, {0, 1}, {0, 2}};
    const Path quite{{0, 0}, {0, 1}, {0, 2}, {1, 1}};
    const wjs::TraceOutcome first = session.trace(quit, 3);
    CHECK(first.kind == TraceKind::found);
    CHECK(found_of(first).word == "QUIT");
    CHECK(found_of(first).tier() == wjs::Tier::power);
    CHECK(first.stars() == 2);
    CHECK(first.sticker());

    // QUI is no word of this list.
    const Path qu_i{{0, 0}, {0, 1}};
    CHECK(session.trace(qu_i, 3).kind == TraceKind::not_a_word);

    // The snapshot keeps the tile as it was written, and restores from it.
    const wjs::SessionSnapshot saved = session.snapshot();
    REQUIRE(saved.board.has_value());
    CHECK(saved.board->tiles == "QU I T/X E X/X X X");
    CHECK(saved.board->viable_total == 2U);
    auto again = wjs::GameSession::restore(saved, data, {});
    REQUIRE(again.has_value());
    CHECK(again->snapshot() == saved);

    const wjs::TraceOutcome last = session.trace(quite, 4);
    CHECK(last.kind == TraceKind::found);
    CHECK(found_of(last).tier() == wjs::Tier::mega);
    CHECK(last.cleared);
    CHECK(session.state() == SessionState::cleared);
}

// --- the standard board: what the session never says -----------------------------

TEST_CASE("every path of the standard hand board is answered as its spelling says",
          "[session][standard]") {
    // Every simple path of two to nine tiles on the hand board, found by walking
    // the grid here and spelling each path from the tiles, and the answer held to
    // what the dictionary says of the spelling. A copy of one session for each
    // path, so that one find does not change the answer to the next.
    const std::array<std::string, 9> tile = {"P", "A", "T", "X", "I", "N", "X", "X", "X"};
    std::set<std::string> counting;
    for (const HandWord& hand : standard_hand_words) {
        counting.insert(std::string(hand.word));
    }
    const wjs::GameSession base = standard_hand_session();

    std::size_t paths = 0;
    std::size_t named = 0;
    Path path;
    std::array<bool, 9> used{};
    std::string spelled;

    const auto check = [&] {
        wjs::GameSession session = base;
        const wjs::TraceOutcome outcome = session.trace(path, 0);
        INFO("path of " << path.size() << " tiles spelling " << spelled);
        ++paths;
        if (spelled.size() >= wjs::min_word_letters && english().contains(spelled)) {
            // Every real word of three letters or more is one that counts.
            CHECK(counting.contains(spelled));
            CHECK(outcome.kind == TraceKind::found);
            REQUIRE(outcome.find.has_value());
            CHECK(found_of(outcome).word == spelled);
            ++named;
            return;
        }
        // Nothing else is ever named.
        CHECK_FALSE(outcome.find.has_value());
        if (english().contains(spelled)) {
            // A real word, of any length, is never "not a word".
            CHECK(outcome.kind == TraceKind::real_not_todays);
        } else {
            CHECK(outcome.kind == TraceKind::not_a_word);
        }
        CHECK(session.finds().empty());
        CHECK(session.stars() == 0);
        CHECK(session.snapshot().board->finds.empty());
    };

    const auto walk = [&](auto&& self, std::size_t row, std::size_t column) -> void {
        const std::size_t index = row * 3 + column;
        used[index] = true;
        path.push_back({row, column});
        spelled += tile[index];
        if (path.size() >= 2) {
            check();
        }
        for (std::size_t next_row = row == 0 ? 0 : row - 1;
             next_row <= std::min<std::size_t>(row + 1, 2); ++next_row) {
            for (std::size_t next_column = column == 0 ? 0 : column - 1;
                 next_column <= std::min<std::size_t>(column + 1, 2); ++next_column) {
                if (!used[next_row * 3 + next_column]) {
                    self(self, next_row, next_column);
                }
            }
        }
        spelled.pop_back();
        path.pop_back();
        used[index] = false;
    };
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            walk(walk, row, column);
        }
    }
    CHECK(paths == 10'296U);
    CHECK(named >= standard_hand_words.size());
}

// --- the standard board: the project's own dice and the full dictionary -------------

namespace {

const wjs::Dictionary& full_dictionary() {
    static const wjs::Dictionary dictionary = [] {
        auto loaded = wjs::Dictionary::load_file(std::filesystem::path(data_directory) /
                                                 "dictionaries" / "en.txt");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return dictionary;
}

const wjs::DiceSets& shipped_dice() {
    static const wjs::DiceSets dice = [] {
        auto loaded = wjs::DiceSets::load_directory(std::filesystem::path(data_directory) / "dice");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return dice;
}

wjs::SessionData shipped_data() {
    return {.dictionary = full_dictionary(), .dice = &shipped_dice()};
}

wjs::GameSession make_shipped(const wjs::SessionOptions& options) {
    auto created = wjs::GameSession::create(shipped_data(), options);
    REQUIRE(created.has_value());
    return *std::move(created);
}

}  // namespace

TEST_CASE("the project's dice deal a board that holds a word at every size",
          "[session][standard][data]") {
    for (const BoardSize size :
         {BoardSize::three, BoardSize::four, BoardSize::five, BoardSize::six}) {
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            wjs::SessionOptions options;
            options.size = size;
            options.seed = seed;
            wjs::GameSession session = make_shipped(options);
            REQUIRE(session.shake().has_value());
            REQUIRE(session.still());
            REQUIRE(session.landed());
            INFO("size " << wjs::side(size) << ", seed " << seed);

            REQUIRE(session.board() != nullptr);
            CHECK(session.board()->rows() == wjs::side(size));
            CHECK(session.board()->columns() == wjs::side(size));
            CHECK(session.dead_tiles().empty());
            const auto solved = wjs::solve(*session.board(), full_dictionary());
            CHECK_FALSE(solved.empty());
            CHECK(session.last_deal()->viable == solved.size());
            CHECK(session.record()->viable_total == solved.size());
        }
    }
}

TEST_CASE("the session deals the standard boards the library's deal does, pinned for two seeds",
          "[session][standard][data]") {
    // Seeds 12345 and 190 of the 3x3 dice, as the deal's own tests pin them
    // (deal_test.cpp): the first roll holds words; the second seed's first roll
    // (P N T/C N K/D N D) holds none and is rolled again from the seed after it.
    wjs::SessionOptions options;
    options.seed = 12345;
    wjs::GameSession plain = make_shipped(options);
    REQUIRE(plain.shake().has_value());
    CHECK(text_of(*plain.board()) == "O L R/D O U/W L B");
    CHECK(plain.last_deal()->rolls == 1U);
    CHECK(plain.last_deal()->seed == 12345U);

    options.seed = 190;
    wjs::GameSession again = make_shipped(options);
    REQUIRE(again.shake().has_value());
    CHECK(text_of(*again.board()) == "T E R/F S Y/G T D");
    CHECK(again.last_deal()->rolls == 2U);
    CHECK(again.last_deal()->seed == wjs::next_deal_seed(190));
    CHECK(again.last_deal()->solves == 2U);
}

TEST_CASE(
    "a standard board of the project's dice is cleared by every word the dictionary has on it",
    "[session][standard][data]") {
    wjs::SessionOptions options;
    options.seed = 7;
    wjs::GameSession session = make_shipped(options);
    REQUIRE(session.shake().has_value());
    REQUIRE(session.still());
    REQUIRE(session.landed());

    // The solver lists every word of the dictionary on the board; trace each.
    const auto words = wjs::solve(*session.board(), full_dictionary());
    REQUIRE(words.size() >= 5U);
    int stars = 0;
    for (const wjs::FoundWord& word : words) {
        const wjs::TraceOutcome outcome = session.trace(word.path, 20);
        REQUIRE(outcome.kind == TraceKind::found);
        REQUIRE(outcome.find.has_value());
        // The session names the word, and it is the dictionary's: every word of
        // three letters or more counts, and so is named, whatever it is.
        CHECK(found_of(outcome).word == word.word);
        CHECK(full_dictionary().contains(found_of(outcome).word));
        CHECK(found_of(outcome).word.size() >= wjs::min_word_letters);
        stars += outcome.stars();
    }
    CHECK(session.state() == SessionState::cleared);
    CHECK(session.stars() == stars);
    CHECK(session.finds().size() == words.size());
    CHECK(session.record()->cleared());

    // And the board at rest comes back from its snapshot, against the full list.
    REQUIRE(session.rest());
    const wjs::SessionSnapshot saved = session.snapshot();
    auto restored = wjs::GameSession::restore(saved, shipped_data(), {});
    REQUIRE(restored.has_value());
    CHECK(restored->snapshot() == saved);
}

TEST_CASE("a dealt standard board has the dice its reported seed rolls, face for face",
          "[session][standard][data]") {
    // A scene that shows the dice is given them by roll_with_placements on the seed
    // the deal reports (docs/design/dice-and-faces.md): the board that roll makes
    // must be the board the session holds, and the face each placement names the
    // letters of its tile. Held for the test dice and for the project's six-face dice.
    const std::array<wjs::SessionData, 2> worlds = {{standard_data(), shipped_data()}};
    for (const wjs::SessionData& data : worlds) {
        for (const BoardSize size :
             {BoardSize::three, BoardSize::four, BoardSize::five, BoardSize::six}) {
            for (std::uint32_t seed = 1; seed <= 6; ++seed) {
                wjs::SessionOptions options;
                options.size = size;
                options.seed = seed;
                auto created = wjs::GameSession::create(data, options);
                REQUIRE(created.has_value());
                wjs::GameSession session = *std::move(created);
                REQUIRE(session.shake().has_value());
                INFO("size " << wjs::side(size) << ", seed " << seed);

                const wjs::DiceSet& set = *data.dice->find(wjs::side(size));
                const auto rolled = wjs::roll_with_placements(set, session.last_deal()->seed);
                REQUIRE(rolled.has_value());
                CHECK(text_of(rolled->board) == text_of(*session.board()));
                REQUIRE(rolled->placements.size() == set.size());
                std::set<std::size_t> used;
                for (std::size_t tile = 0; tile < rolled->placements.size(); ++tile) {
                    const wjs::Placement& placement = rolled->placements[tile];
                    const wjs::Position position{tile / wjs::side(size), tile % wjs::side(size)};
                    CHECK(set.die(placement.die)[placement.face] ==
                          session.board()->tile(position));
                    used.insert(placement.die);
                }
                // Every die of the set is on the board once.
                CHECK(used.size() == set.size());
            }
        }
    }
}
