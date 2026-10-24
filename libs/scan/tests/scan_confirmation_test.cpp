// SPDX-License-Identifier: GPL-3.0-or-later
// The check between the reader and the solver: the draft a grown-up corrects, and
// the one way a reading becomes a Board.

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/scan/confirmation.hpp"
#include "wjs/solver.hpp"

namespace {

using namespace wjs;
using namespace wjs::scan;

// A reading of a 2 x 3 board, as a reader might make it:
//
//     C   A?  T
//     ??  O?  Qu
//
// where `?` is a doubted tile and `??` a cell the reader saw nothing in.
BoardReading reading_of_two_by_three() {
    BoardReading reading;
    reading.rows = 2;
    reading.columns = 3;
    const auto tile = [](std::string letters, double confidence, bool doubtful) {
        TileReading t;
        t.letters = std::move(letters);
        t.confidence = confidence;
        t.doubtful = doubtful;
        t.alternatives[0] = {"R", 0.5};
        t.alternatives[1] = {"H", 0.4};
        return t;
    };
    reading.tiles = {tile("C", 1.0, false), tile("A", 0.3, true), tile("T", 0.9, false),
                     tile("", 0.0, true),   tile("O", 0.4, true), tile("QU", 0.8, false)};
    return reading;
}

// The draft of that reading, which has to be made: it is a grid.
BoardDraft draft_of_two_by_three() {
    auto draft = BoardDraft::try_from_reading(reading_of_two_by_three());
    REQUIRE(draft.has_value());
    return *draft;
}

std::string text_of(const Board& board) {
    std::string text;
    for (std::size_t row = 0; row < board.rows(); ++row) {
        if (row > 0) {
            text += " / ";
        }
        for (std::size_t column = 0; column < board.columns(); ++column) {
            if (column > 0) {
                text += " ";
            }
            text += board.tile({row, column});
        }
    }
    return text;
}

}  // namespace

// What stands between a reading and the solver, held at compile time: there is no way
// to turn a BoardReading into a Board except through BoardDraft::confirm().
static_assert(!std::is_convertible_v<BoardReading, Board>);
static_assert(!std::is_constructible_v<Board, BoardReading>);
static_assert(!std::is_constructible_v<Board, const BoardReading&>);
static_assert(!std::is_convertible_v<BoardDraft, Board>);
static_assert(!std::is_constructible_v<BoardDraft, BoardReading>);  // only from_reading() makes one

TEST_CASE("a draft starts as the grid was read", "[scan][draft]") {
    const BoardDraft draft = draft_of_two_by_three();
    CHECK(draft.rows() == 2);
    CHECK(draft.columns() == 3);
    CHECK(draft.tile({0, 0}).letters == "C");
    CHECK(draft.tile({0, 0}).origin == TileOrigin::read);
    CHECK_FALSE(draft.tile({0, 0}).doubtful);
    CHECK(draft.tile({1, 2}).letters == "QU");
    CHECK(draft.tile({0, 1}).confidence == 0.3);
    CHECK(draft.tile({0, 1}).alternatives[0].letters == "R");

    // A cell the reader saw nothing in is empty, and it is rung like a doubted one.
    CHECK(draft.tile({1, 0}).origin == TileOrigin::empty);
    CHECK(draft.tile({1, 0}).doubtful);
    CHECK(draft.contains({1, 2}));
    CHECK_FALSE(draft.contains({2, 0}));
    CHECK_FALSE(draft.contains({0, 3}));
}

TEST_CASE("the doubted tiles are listed in reading order, and so are the empty ones",
          "[scan][draft]") {
    const BoardDraft draft = draft_of_two_by_three();
    const std::vector<Position> doubtful = draft.doubtful();
    REQUIRE(doubtful.size() == 3);
    CHECK(doubtful[0] == Position{0, 1});
    CHECK(doubtful[1] == Position{1, 0});
    CHECK(doubtful[2] == Position{1, 1});
    const std::vector<Position> unfilled = draft.unfilled();
    REQUIRE(unfilled.size() == 1);
    CHECK(unfilled[0] == Position{1, 0});
    CHECK_FALSE(draft.complete());
}

TEST_CASE("a board with a tile missing cannot be confirmed", "[scan][draft]") {
    const BoardDraft draft = draft_of_two_by_three();
    const auto board = draft.confirm();
    REQUIRE_FALSE(board.has_value());
    CHECK(board.error() == DraftError::incomplete);
}

TEST_CASE("a correction settles a tile and is kept upper-case", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.set_tile({1, 0}, "s").has_value());
    CHECK(draft.tile({1, 0}).letters == "S");
    CHECK(draft.tile({1, 0}).origin == TileOrigin::corrected);
    CHECK_FALSE(draft.tile({1, 0}).doubtful);
    CHECK(draft.tile({1, 0}).confidence == 1.0);
    CHECK(draft.tile({1, 0}).alternatives[0].letters.empty());

    // A tile of two letters, and a tile of three, are tiles.
    REQUIRE(draft.set_tile({0, 2}, "Qu").has_value());
    CHECK(draft.tile({0, 2}).letters == "QU");
    REQUIRE(draft.set_tile({0, 2}, "tch").has_value());
    CHECK(draft.tile({0, 2}).letters == "TCH");
}

TEST_CASE("letters that are not a tile are refused and the tile is left alone", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    for (const std::string_view bad : {"", "ABCD", "A1", "A B", "-", "\xC3\xA9"}) {
        const auto result = draft.set_tile({0, 0}, bad);
        INFO("letters '" << bad << "'");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == DraftError::bad_letters);
        CHECK(draft.tile({0, 0}).letters == "C");
        CHECK(draft.tile({0, 0}).origin == TileOrigin::read);
    }
    const auto off = draft.set_tile({5, 5}, "A");
    REQUIRE_FALSE(off.has_value());
    CHECK(off.error() == DraftError::off_grid);
}

TEST_CASE("accepting a doubted tile settles it without changing it", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.accept_tile({0, 1}).has_value());
    CHECK(draft.tile({0, 1}).letters == "A");
    CHECK(draft.tile({0, 1}).origin == TileOrigin::corrected);
    CHECK_FALSE(draft.tile({0, 1}).doubtful);
    CHECK(draft.doubtful().size() == 2);

    // A tile that was never doubted stays as it was read.
    REQUIRE(draft.accept_tile({0, 0}).has_value());
    CHECK(draft.tile({0, 0}).origin == TileOrigin::read);

    // There is nothing to accept in an empty tile.
    const auto empty = draft.accept_tile({1, 0});
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error() == DraftError::incomplete);
    CHECK(draft.accept_tile({9, 9}).error() == DraftError::off_grid);
}

TEST_CASE("clearing a tile leaves it missing and not doubted", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.clear_tile({0, 0}).has_value());
    CHECK(draft.tile({0, 0}).letters.empty());
    CHECK(draft.tile({0, 0}).origin == TileOrigin::empty);
    CHECK_FALSE(draft.tile({0, 0}).doubtful);
    CHECK(draft.unfilled().size() == 2);
    CHECK(draft.clear_tile({9, 0}).error() == DraftError::off_grid);
}

TEST_CASE("pressing Yes, play on a filled grid makes the board, doubted tiles and all",
          "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.set_tile({1, 0}, "S").has_value());
    REQUIRE(draft.complete());
    // Two tiles are still doubted. The grown-up looked and said yes: the board is made.
    CHECK(draft.doubtful().size() == 2);
    const auto board = draft.confirm();
    REQUIRE(board.has_value());
    CHECK(board->rows() == 2);
    CHECK(board->columns() == 3);
    CHECK(text_of(*board) == "C A T / S O QU");
    CHECK(board->tile({1, 2}) == "QU");
}

TEST_CASE("the solver is given the confirmed board and finds its words", "[scan][draft]") {
    // The whole flow, read to played: a reading, a correction, a confirmation, a solve.
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.set_tile({1, 0}, "S").has_value());
    REQUIRE(draft.set_tile({1, 1}, "A").has_value());  // the doubted O was an A
    const auto board = draft.confirm();
    REQUIRE(board.has_value());

    Dictionary dictionary;
    REQUIRE(dictionary.insert("cat"));
    REQUIRE(dictionary.insert("sat"));
    REQUIRE(dictionary.insert("cot"));  // needs the O the reader doubted
    const auto words = solve(*board, dictionary);
    REQUIRE(words.size() == 2);
    CHECK(words[0].word == "CAT");
    CHECK(words[1].word == "SAT");
}

TEST_CASE("a quarter turn turns the grid, and four are the grid as it was", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.set_tile({1, 0}, "S").has_value());
    const auto original = draft.confirm();
    REQUIRE(original.has_value());

    BoardDraft clockwise = draft;
    clockwise.turn(Turn::clockwise);
    CHECK(clockwise.rows() == 3);
    CHECK(clockwise.columns() == 2);
    CHECK(text_of(*clockwise.confirm()) == "S C / O A / QU T");

    BoardDraft counter = draft;
    counter.turn(Turn::counter_clockwise);
    CHECK(text_of(*counter.confirm()) == "T QU / A O / C S");

    BoardDraft round = draft;
    for (int k = 0; k < 4; ++k) {
        round.turn(Turn::clockwise);
    }
    CHECK(text_of(*round.confirm()) == text_of(*original));
    // A turn and its undoing.
    clockwise.turn(Turn::counter_clockwise);
    CHECK(text_of(*clockwise.confirm()) == text_of(*original));
}

TEST_CASE("a tile's doubt goes with it when the grid turns", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    draft.turn(Turn::clockwise);
    // The doubted A was at (0, 1); clockwise it is at (1, 1).
    CHECK(draft.tile({1, 1}).letters == "A");
    CHECK(draft.tile({1, 1}).doubtful);
    CHECK(draft.doubtful().size() == 3);
}

TEST_CASE("a grid corrected for size keeps the tiles that are on both", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.resize(3, 3).has_value());
    CHECK(draft.rows() == 3);
    CHECK(draft.columns() == 3);
    CHECK(draft.tile({0, 1}).letters == "A");
    CHECK(draft.tile({0, 1}).doubtful);
    CHECK(draft.tile({2, 0}).origin == TileOrigin::empty);
    CHECK_FALSE(draft.tile({2, 0}).doubtful);

    REQUIRE(draft.resize(1, 2).has_value());
    CHECK(draft.rows() == 1);
    CHECK(draft.columns() == 2);
    CHECK(draft.tile({0, 0}).letters == "C");
    CHECK(draft.tile({0, 1}).letters == "A");

    CHECK(draft.resize(0, 3).error() == DraftError::empty_grid);
    CHECK(draft.resize(3, 0).error() == DraftError::empty_grid);
    CHECK(draft.resize(17, 3).error() == DraftError::too_large);
    CHECK(draft.rows() == 1);  // a refused size changes nothing
}

TEST_CASE("a board typed in starts empty, with nothing doubted", "[scan][draft]") {
    auto made = BoardDraft::empty(4, 4);
    REQUIRE(made.has_value());
    BoardDraft draft = *made;
    CHECK(draft.rows() == 4);
    CHECK(draft.doubtful().empty());
    CHECK(draft.unfilled().size() == 16);
    CHECK_FALSE(draft.complete());
    CHECK(draft.confirm().error() == DraftError::incomplete);

    const std::string_view letters = "TAPSINERLOCDMUHG";
    for (std::size_t i = 0; i < 16; ++i) {
        REQUIRE(draft.set_tile({i / 4, i % 4}, letters.substr(i, 1)).has_value());
    }
    CHECK(draft.tile({0, 0}).origin == TileOrigin::typed);
    CHECK(draft.complete());
    const auto board = draft.confirm();
    REQUIRE(board.has_value());
    CHECK(text_of(*board) == "T A P S / I N E R / L O C D / M U H G");
}

TEST_CASE("a grid of no rows, or one too large to be a board, cannot be made", "[scan][draft]") {
    CHECK(BoardDraft::empty(0, 4).error() == DraftError::empty_grid);
    CHECK(BoardDraft::empty(4, 0).error() == DraftError::empty_grid);
    CHECK(BoardDraft::empty(17, 4).error() == DraftError::too_large);
    CHECK(BoardDraft::empty(16, 16).has_value());
}

TEST_CASE("each draft error says what is wrong", "[scan][draft]") {
    for (const DraftError error :
         {DraftError::empty_grid, DraftError::too_large, DraftError::bad_letters,
          DraftError::off_grid, DraftError::incomplete, DraftError::inconsistent_reading}) {
        CHECK_FALSE(to_string(error).empty());
    }
}

TEST_CASE("a reading that is not a grid is refused, and a grid is not", "[scan][draft]") {
    // Three rows of three and not a tile: set_tile on {2, 2} would have written past the
    // end of an empty vector.
    BoardReading reading;
    reading.rows = 3;
    reading.columns = 3;
    CHECK(BoardDraft::try_from_reading(reading).error() == DraftError::inconsistent_reading);

    reading.tiles.assign(8, TileReading{});  // one short
    CHECK(BoardDraft::try_from_reading(reading).error() == DraftError::inconsistent_reading);
    reading.tiles.assign(10, TileReading{});  // one over
    CHECK(BoardDraft::try_from_reading(reading).error() == DraftError::inconsistent_reading);
    reading.tiles.assign(9, TileReading{});
    REQUIRE(BoardDraft::try_from_reading(reading).has_value());

    BoardReading no_rows;
    no_rows.columns = 3;
    CHECK(BoardDraft::try_from_reading(no_rows).error() == DraftError::empty_grid);
    BoardReading no_columns;
    no_columns.rows = 3;
    no_columns.tiles.assign(3, TileReading{});
    CHECK(BoardDraft::try_from_reading(no_columns).error() == DraftError::empty_grid);

    BoardReading big;
    big.rows = 17;
    big.columns = 2;
    big.tiles.assign(34, TileReading{});
    CHECK(BoardDraft::try_from_reading(big).error() == DraftError::too_large);
    // A size that would wrap the product of rows and columns is too large, not a small grid.
    BoardReading wrapped;
    wrapped.rows = (std::size_t{1} << 63) + 1;
    wrapped.columns = 2;
    wrapped.tiles.assign(2, TileReading{});
    CHECK(BoardDraft::try_from_reading(wrapped).error() == DraftError::too_large);

    BoardReading largest;
    largest.rows = Board::max_dimension;
    largest.columns = Board::max_dimension;
    largest.tiles.assign(Board::max_dimension * Board::max_dimension, TileReading{});
    CHECK(BoardDraft::try_from_reading(largest).has_value());
}

TEST_CASE("the unchecked call gives the same draft for a grid and a safe blank for a non-grid",
          "[scan][draft]") {
    const BoardDraft same = BoardDraft::from_reading(reading_of_two_by_three());
    const BoardDraft checked = draft_of_two_by_three();
    CHECK(same.rows() == checked.rows());
    CHECK(same.columns() == checked.columns());
    CHECK(same.doubtful() == checked.doubtful());
    CHECK(same.unfilled() == checked.unfilled());

    BoardReading hollow;
    hollow.rows = 3;
    hollow.columns = 3;
    BoardDraft blank = BoardDraft::from_reading(hollow);
    REQUIRE(blank.rows() == 3);
    REQUIRE(blank.columns() == 3);
    CHECK(blank.unfilled().size() == 9);
    CHECK(blank.doubtful().size() == 9);
    // Every position is on the grid and has a tile behind it.
    CHECK(blank.set_tile({2, 2}, "A").has_value());
    CHECK(blank.tile({2, 2}).letters == "A");
    blank.turn(Turn::clockwise);
    CHECK(blank.unfilled().size() == 8);

    BoardReading nothing;  // no rows, no columns: held to a grid of one
    const BoardDraft one = BoardDraft::from_reading(nothing);
    CHECK(one.rows() == 1);
    CHECK(one.columns() == 1);
    BoardReading huge;
    huge.rows = 1000;
    huge.columns = 1000;
    const BoardDraft capped = BoardDraft::from_reading(huge);
    CHECK(capped.rows() == Board::max_dimension);
    CHECK(capped.columns() == Board::max_dimension);
}

TEST_CASE("a correction is upper-cased by ASCII and nothing else", "[scan][draft]") {
    BoardDraft draft = draft_of_two_by_three();
    REQUIRE(draft.set_tile({0, 0}, "qu").has_value());
    CHECK(draft.tile({0, 0}).letters == "QU");
    // Bytes that are not letters of the alphabet, whatever a locale makes of them.
    CHECK(draft.set_tile({0, 0}, "\xE9").error() == DraftError::bad_letters);
    CHECK(draft.set_tile({0, 0}, "a1").error() == DraftError::bad_letters);
    CHECK(draft.set_tile({0, 0}, "[").error() == DraftError::bad_letters);
    CHECK(draft.set_tile({0, 0}, "{").error() == DraftError::bad_letters);
    CHECK(draft.tile({0, 0}).letters == "QU");  // a refusal changes nothing
}
