// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assist_game.hpp"
#include "wjs/board.hpp"
#include "wjs/calendar.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/scan/confirmation.hpp"
#include "wjs/session_store.hpp"
#include "wjs/store.hpp"

// The Family Assistant's port (apps/app/src/scan/assist_game.*): a typed board, as the
// check screen makes one, through BoardDraft::confirm() to AssistGame. It holds
// down what a traced path is on a board that counts a few words (a viable word, a
// real word that is not today's and a string of letters), the start-tile help,
// the tray, and what the board leaves in the store. On the standard board the
// same rules and the same record hold every real word, and the middle state never
// arises (docs/design/game.md: the real-not-today's cue is the phonics board's).
//
// TEST DATA. The words here are made up for these tests and are not content for
// a child.

namespace {

using wjs::Position;

constexpr wjs::Day today = wjs::day_from_civil(2026, 10, 19);

wjs::Dictionary dictionary_with(std::initializer_list<std::string_view> words) {
    wjs::Dictionary dictionary;
    for (const std::string_view word : words) {
        REQUIRE(dictionary.insert(word));
    }
    return dictionary;
}

// A board typed into an empty check screen: one letter a tile, one char a row.
// This is the check screen's own road to a Board (BoardDraft::empty, set_tile,
// confirm), not a Board made around it.
wjs::Board typed_board(std::initializer_list<std::string_view> rows) {
    const std::size_t side = rows.size();
    auto draft = wjs::scan::BoardDraft::empty(side, side);
    REQUIRE(draft.has_value());
    std::size_t row_index = 0;
    for (const std::string_view row : rows) {
        REQUIRE(row.size() == side);
        for (std::size_t column = 0; column < side; ++column) {
            REQUIRE(draft->set_tile({row_index, column}, std::string(1, row[column])).has_value());
        }
        ++row_index;
    }
    auto board = draft->confirm();
    REQUIRE(board.has_value());
    return std::move(*board);
}

// The 3x3 the cases play:
//
//   S A T
//   P I N
//   X Y Z
//
// SAT, SIT, PINT, TAP and PIN are traceable on it; SAP is not a word.
wjs::Board the_board() {
    return typed_board({"SAT", "PIN", "XYZ"});
}

const std::vector<Position> sat{{0, 0}, {0, 1}, {0, 2}};
const std::vector<Position> sit{{0, 0}, {1, 1}, {0, 2}};
const std::vector<Position> pint{{1, 0}, {1, 1}, {1, 2}, {0, 2}};
const std::vector<Position> tap{{0, 2}, {0, 1}, {1, 0}};
const std::vector<Position> sap{{0, 0}, {0, 1}, {1, 0}};

}  // namespace

TEST_CASE("a typed board is played for the words that count, and one of today's is not named",
          "[app][scan][assist]") {
    // What counts on this board, and the English words that tell a real word from
    // a string of letters. Only SAT, SIT and PINT count here.
    const wjs::Dictionary dictionary = dictionary_with({"SAT", "SIT", "PINT", "TAP", "PIN", "NIP"});
    const wjs::Dictionary active = dictionary_with({"SAT", "SIT", "PINT"});
    wjs::Store store = wjs::Store::in_memory();
    wjs::app::AssistGame game(the_board(), dictionary, active, store, [] { return today; });

    CHECK(game.board().rows() == 3);
    CHECK(game.board().columns() == 3);
    CHECK(game.viableCount() == 3);  // SAT, SIT, PINT
    CHECK(game.found().empty());
    CHECK(game.stars() == 0);

    // Help: the start tile of the shortest word not yet found, nearest the top
    // left. SAT and SIT are three letters and both start at the top-left tile;
    // SAT sorts first. PINT is longer, so it waits.
    CHECK(game.showStart() == Position{0, 0});
    CHECK(game.litTile() == Position{0, 0});

    SECTION("viable, real-not-today's and not a word") {
        const wjs::app::AssistTraceOutcome real = game.submit(tap);
        CHECK(real.kind == wjs::app::AssistWordKind::realNotTodays);
        // The design never shows or reads a word that is not today's: the
        // outcome carries none.
        CHECK(real.found.word.empty());
        CHECK(game.found().empty());

        const wjs::app::AssistTraceOutcome other = game.submit(sap);
        CHECK(other.kind == wjs::app::AssistWordKind::notAWord);
        CHECK(other.found.word.empty());
        CHECK(game.found().empty());

        // A real word that counts: a jewel, its tier and its stars.
        const wjs::app::AssistTraceOutcome found = game.submit(sat);
        CHECK(found.kind == wjs::app::AssistWordKind::viable);
        CHECK(found.found.word == "sat");
        CHECK(found.found.tier == wjs::Tier::spark);
        CHECK(found.found.stars == 1);
        CHECK_FALSE(found.alreadyFound);
        // The start tile was lit for this word when it was found.
        CHECK(found.found.assisted);
        CHECK(game.found().size() == 1);
        CHECK(game.stars() == 1);

        // The same word again: no new jewel.
        const wjs::app::AssistTraceOutcome again = game.submit(sat);
        CHECK(again.kind == wjs::app::AssistWordKind::viable);
        CHECK(again.alreadyFound);
        CHECK(game.found().size() == 1);
        CHECK(game.stars() == 1);
    }

    SECTION("the lit start stays until its word is found") {
        CHECK(game.submit(sat).found.assisted);

        // The next shortest is SIT. Finding PINT while it is lit leaves the light
        // where it was: that word is not found.
        CHECK(game.showStart() == Position{0, 0});
        const wjs::app::AssistTraceOutcome longer = game.submit(pint);
        CHECK(longer.kind == wjs::app::AssistWordKind::viable);
        CHECK_FALSE(longer.found.assisted);
        CHECK(game.litTile() == Position{0, 0});

        // SIT at last; then nothing is left, and the same question answers
        // nothing rather than another word.
        const wjs::app::AssistTraceOutcome shortest = game.submit(sit);
        CHECK(shortest.found.assisted);
        CHECK_FALSE(game.litTile().has_value());
        CHECK_FALSE(game.showStart().has_value());
        CHECK(game.stars() == 4);  // SAT 1, PINT 2 (a Power word), SIT 1
    }

    SECTION("what a board leaves in the store") {
        CHECK(game.submit(sat).kind == wjs::app::AssistWordKind::viable);
        CHECK(game.showStart() == Position{0, 0});  // SIT
        CHECK(game.submit(pint).kind == wjs::app::AssistWordKind::viable);
        CHECK(game.submit(sit).kind == wjs::app::AssistWordKind::viable);

        CHECK(store.data().boards.empty());
        game.leave();
        // Leaving again keeps one board, not two.
        game.leave();
        CHECK(game.recorded());

        REQUIRE(store.data().boards.size() == 1);
        const wjs::StoredBoard& board = store.data().boards.front();
        CHECK(board.size == 3);
        CHECK(board.phase.empty());  // the standard board's form
        CHECK(board.tiles == "S,A,T/P,I,N/X,Y,Z");
        CHECK(board.viable_total == 3);  // SAT, SIT, PINT; the words are not listed
        CHECK(board.viable.empty());
        CHECK(board.found == 3);
        CHECK(board.stars == 4);
        CHECK(board.cleared);  // all three words that counted were found

        const std::vector<wjs::FindRecord> finds = wjs::finds_on_board(store.data(), board.serial);
        REQUIRE(finds.size() == 3);
        CHECK(finds[0].word == "sat");
        CHECK(finds[0].start_tile_used);
        CHECK(finds[0].board_size == 3);
        CHECK(finds[0].phase.empty());
        CHECK(finds[0].day == today);
        CHECK(finds[1].word == "pint");
        CHECK_FALSE(finds[1].start_tile_used);  // SIT was the word the light was on
        CHECK(finds[2].word == "sit");
        CHECK(finds[2].start_tile_used);
        CHECK(store.data().stars == 4);

        // The panel's history: the board left, newest first, with its finds.
        const std::vector<wjs::app::AssistHistoryEntry> history = game.history();
        REQUIRE(history.size() == 1);
        CHECK(history.front().rows == 3);
        CHECK(history.front().columns == 3);
        CHECK(history.front().tiles.size() == 9);
        CHECK(history.front().tiles.front() == "S");
        REQUIRE(history.front().found.size() == 3);
        CHECK(history.front().found.front().word == "sat");
        CHECK(history.front().found.front().stars == 1);
    }
}

TEST_CASE("the standard board counts every real word and the middle state never arises",
          "[app][scan][assist]") {
    const wjs::Dictionary dictionary = dictionary_with({"SAT", "SIT", "PINT", "TAP", "PIN", "NIP"});
    wjs::Store store = wjs::Store::in_memory();
    wjs::app::AssistGame game(the_board(), dictionary, store, [] { return today; });

    // Every word of the dictionary that is on the board counts: SAT, SIT, PINT,
    // TAP and PIN (NIP too).
    CHECK(game.viableCount() == 6);

    // A real word is viable here, and a string of letters still is not.
    CHECK(game.submit(tap).kind == wjs::app::AssistWordKind::viable);
    CHECK(game.submit(sap).kind == wjs::app::AssistWordKind::notAWord);
    CHECK(game.found().size() == 1);
    CHECK(game.found().front().word == "tap");

    game.leave();
    REQUIRE(store.data().boards.size() == 1);
    CHECK(store.data().boards.front().viable_total == 6);
    CHECK(store.data().boards.front().phase.empty());
}
