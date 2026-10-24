// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/scan/reading.hpp"

namespace wjs::scan {

// Where a tile's letters came from.
enum class TileOrigin : std::uint8_t {
    // The reader's, untouched.
    read,
    // A grown-up changed them (or settled the reader's with accept_tile).
    corrected,
    // Typed into an empty grid, where the camera was not used.
    typed,
    // Nothing yet.
    empty,
};

// One tile of the grid being checked.
struct DraftTile {
    // "A" to "Z" and up to three letters in all, upper-case; empty while the tile
    // has none.
    std::string letters;
    TileOrigin origin = TileOrigin::empty;
    // The reader's confidence from 0 to 1; 1 for a tile a person settled.
    double confidence = 0.0;
    // Ring this tile and show the `?`: the reader doubted it (or saw nothing in
    // it) and no one has settled it since.
    bool doubtful = false;
    // The reader's other thoughts, for the picker to offer first.
    std::array<Alternative, 2> alternatives{};
};

enum class DraftError {
    // A grid of no rows or no columns.
    empty_grid,
    // A grid larger than a Board can be.
    too_large,
    // Not one to three letters A to Z.
    bad_letters,
    // A position outside the grid.
    off_grid,
    // confirm() with a tile that has no letters yet, or accept_tile on one.
    incomplete,
    // A reading whose tiles are not rows * columns of them.
    inconsistent_reading,
};

[[nodiscard]] std::string_view to_string(DraftError error) noexcept;

enum class Turn : std::uint8_t { clockwise, counter_clockwise };

// The check between the reader and the solver, as data for a screen to bind to.
//
// A draft starts from a reading (or from an empty grid, for a board typed in),
// and a grown-up changes it: a tile's letters, the grid's turn, its size. The
// tiles the reader doubted are listed, for the screen to ring. When the grown-up
// says "Yes, play", `confirm()` makes the `Board`. That is the only way a reading
// becomes a Board, so the solver sees only a board a person has looked at.
//
// No Qt, no pixels, no I/O: a plain value, copyable, with every operation a
// function of the draft and its arguments.
class BoardDraft {
public:
    // The grid as read. A tile the reader saw nothing in is empty and doubtful.
    // Refused if the reading is not a grid: no rows or columns, more than a Board
    // can have, or a number of tiles other than rows * columns (a reading made by
    // wjs::scan::read_board never is any of those; one made by anything else may
    // be, and a draft over such a reading would index past its tiles).
    [[nodiscard]] static std::expected<BoardDraft, DraftError> try_from_reading(
        const BoardReading& reading);

    // Deprecated: the call for code written before `try_from_reading`, which it
    // keeps building. For a reading that is a grid it is the same draft; for one
    // that is not, it is an empty grid of the reading's size held to 1..16 on each
    // side, every tile blank and doubted, so that nothing overruns and the grown-up
    // types the board in. New code uses `try_from_reading` and handles the error.
    [[nodiscard]] static BoardDraft from_reading(const BoardReading& reading);

    // An empty grid, nothing doubted: the "Type in your board" screen.
    [[nodiscard]] static std::expected<BoardDraft, DraftError> empty(std::size_t rows,
                                                                     std::size_t columns);

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] bool contains(Position position) const noexcept {
        return position.row < rows_ && position.column < columns_;
    }

    // The tile at a position, which must be on the grid.
    [[nodiscard]] const DraftTile& tile(Position position) const noexcept {
        return tiles_[position.row * columns_ + position.column];
    }

    // A grown-up's letters for a tile: one to three letters, upper- or lower-case,
    // kept upper-case. The tile is settled (no longer doubtful).
    [[nodiscard]] std::expected<void, DraftError> set_tile(Position position,
                                                           std::string_view letters);

    // "This one is right": settles a doubted tile without changing its letters. A
    // tile with no letters cannot be accepted.
    [[nodiscard]] std::expected<void, DraftError> accept_tile(Position position);

    // Empties a tile. It is not doubtful, only missing.
    [[nodiscard]] std::expected<void, DraftError> clear_tile(Position position);

    // A quarter turn of the whole grid, which is the "Turn" control: the replica
    // upright is not how the child sees the real board.
    void turn(Turn direction);

    // The grid at another size, for when the counted size was wrong. Tiles that
    // are on both grids are kept, where they were from the top left; the others
    // are empty.
    [[nodiscard]] std::expected<void, DraftError> resize(std::size_t rows, std::size_t columns);

    // The doubted tiles, in reading order (row by row): the ones to ring.
    [[nodiscard]] std::vector<Position> doubtful() const;

    // The tiles with no letters, in reading order.
    [[nodiscard]] std::vector<Position> unfilled() const;

    // Every tile has letters.
    [[nodiscard]] bool complete() const;

    // The board, if every tile has letters. Doubted tiles do not stop it: pressing
    // "Yes, play" is the grown-up saying they looked, and the check is for them,
    // not against them. A screen that wants to ask first asks `doubtful()`.
    [[nodiscard]] std::expected<Board, DraftError> confirm() const;

private:
    BoardDraft(std::size_t rows, std::size_t columns, std::vector<DraftTile> tiles, bool typed);

    std::size_t rows_;
    std::size_t columns_;
    std::vector<DraftTile> tiles_;
    // The grid was typed in, not read: its tiles' letters are `typed`, not `corrected`.
    bool typed_;
};

}  // namespace wjs::scan
