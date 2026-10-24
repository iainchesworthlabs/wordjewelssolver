// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace wjs {

struct Position {
    std::size_t row = 0;
    std::size_t column = 0;

    friend constexpr bool operator==(const Position&, const Position&) = default;
};

enum class BoardError {
    empty,      // the text holds no tiles
    ragged,     // the rows are not all the same length
    bad_tile,   // a tile is not one to three letters A-Z
    too_large,  // more rows or columns than Board::max_dimension
};

[[nodiscard]] std::string_view to_string(BoardError error) noexcept;

// A rectangular grid of tiles. A tile carries one to three letters, usually
// one; the "Qu" face of a Boggle die is the two-letter tile "QU". Letters are
// stored upper-case.
class Board {
public:
    static constexpr std::size_t max_dimension = 16;
    static constexpr std::size_t max_tile_letters = 3;

    // Rows are separated by newlines or '/'. Within a row, tiles are separated
    // by whitespace or commas; a row with no separators is read one letter per
    // tile, so "TAPE" is four tiles and "Qu I T" is three.
    [[nodiscard]] static std::expected<Board, BoardError> parse(std::string_view text);

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }

    [[nodiscard]] bool contains(Position position) const noexcept;

    // The upper-case letters on a tile. The position must be on the board.
    [[nodiscard]] const std::string& tile(Position position) const noexcept;

private:
    Board(std::size_t rows, std::size_t columns, std::vector<std::string> tiles);

    std::size_t rows_;
    std::size_t columns_;
    std::vector<std::string> tiles_;
};

}  // namespace wjs
