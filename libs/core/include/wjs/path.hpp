// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "wjs/board.hpp"

namespace wjs {

enum class PathError {
    empty,          // the path holds no tiles
    off_board,      // a position is not on the board
    repeated_tile,  // a tile is used twice
    not_adjacent,   // two consecutive tiles do not touch
};

[[nodiscard]] std::string_view to_string(PathError error) noexcept;

// Whether two positions are different tiles that touch: next to each other
// across a row or a column, or diagonally. Edges do not wrap.
[[nodiscard]] constexpr bool are_neighbours(Position a, Position b) noexcept {
    const auto near = [](std::size_t x, std::size_t y) { return (x > y ? x - y : y - x) <= 1; };
    return a != b && near(a.row, b.row) && near(a.column, b.column);
}

// Checks a path a finger or a solver has traced: every position is on the
// board, no tile is used twice and each tile touches the one before it. On
// success the result is the word the tiles spell, upper-case, with a tile's
// letters all in place (so the "Qu" tile adds two).
//
// The first fault along the path is reported. For one position the order is
// off the board, then used before, then not touching the previous tile.
[[nodiscard]] std::expected<std::string, PathError> validate_path(const Board& board,
                                                                  std::span<const Position> path);

}  // namespace wjs
