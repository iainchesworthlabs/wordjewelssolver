// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/board.hpp"

#include <utility>

#include "ascii.hpp"
#include "wjs/profiling.hpp"

namespace wjs {
namespace {

constexpr std::string_view tile_separators = " \t,";

[[nodiscard]] constexpr bool is_row_break(char c) noexcept {
    return c == '\n' || c == '\r' || c == '/';
}

using ascii::to_upper;

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    return ascii::trim(text, tile_separators);
}

// The non-blank rows of the text.
[[nodiscard]] std::vector<std::string_view> split_rows(std::string_view text) {
    std::vector<std::string_view> rows;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || is_row_break(text[i])) {
            const auto row = trim(text.substr(start, i - start));
            if (!row.empty()) {
                rows.push_back(row);
            }
            start = i + 1;
        }
    }
    return rows;
}

// One tile from its raw text, or nothing if it is not one to three letters.
[[nodiscard]] bool append_tile(std::string_view raw, std::vector<std::string>& tiles) {
    if (raw.empty() || raw.size() > Board::max_tile_letters) {
        return false;
    }
    std::string tile;
    tile.reserve(raw.size());
    for (const char c : raw) {
        const char upper = to_upper(c);
        if (upper < 'A' || upper > 'Z') {
            return false;
        }
        tile.push_back(upper);
    }
    tiles.push_back(std::move(tile));
    return true;
}

// The tiles of one trimmed, non-empty row.
[[nodiscard]] std::expected<std::vector<std::string>, BoardError> read_row(std::string_view row) {
    std::vector<std::string> tiles;

    if (row.find_first_of(tile_separators) == std::string_view::npos) {
        for (const char c : row) {
            if (!append_tile(std::string_view(&c, 1), tiles)) {
                return std::unexpected(BoardError::bad_tile);
            }
        }
        return tiles;
    }

    std::size_t start = 0;
    while (start < row.size()) {
        const auto end = row.find_first_of(tile_separators, start);
        const auto stop = end == std::string_view::npos ? row.size() : end;
        if (stop > start && !append_tile(row.substr(start, stop - start), tiles)) {
            return std::unexpected(BoardError::bad_tile);
        }
        start = stop + 1;
    }
    return tiles;
}

}  // namespace

std::string_view to_string(BoardError error) noexcept {
    switch (error) {
        case BoardError::empty:
            return "the board has no tiles";
        case BoardError::ragged:
            return "the rows of the board are not all the same length";
        case BoardError::bad_tile:
            return "a tile is not one to three letters";
        case BoardError::too_large:
            return "the board is larger than 16 by 16";
    }
    return "unknown board error";
}

Board::Board(std::size_t rows, std::size_t columns, std::vector<std::string> tiles)
    : rows_(rows), columns_(columns), tiles_(std::move(tiles)) {}

std::expected<Board, BoardError> Board::parse(std::string_view text) {
    auto zone = profiling::zone<"board.parse">();

    std::vector<std::string> tiles;
    std::size_t columns = 0;
    std::size_t rows = 0;
    bool ragged = false;

    for (const auto row_text : split_rows(text)) {
        auto row = read_row(row_text);
        if (!row) {
            return std::unexpected(row.error());
        }
        if (rows == 0) {
            columns = row->size();
        } else if (row->size() != columns) {
            ragged = true;
        }
        ++rows;
        for (auto& tile : *row) {
            tiles.push_back(std::move(tile));
        }
    }

    if (rows == 0) {
        return std::unexpected(BoardError::empty);
    }
    if (ragged) {
        return std::unexpected(BoardError::ragged);
    }
    if (rows > max_dimension || columns > max_dimension) {
        return std::unexpected(BoardError::too_large);
    }
    zone.Value(tiles.size());
    return Board(rows, columns, std::move(tiles));
}

bool Board::contains(Position position) const noexcept {
    return position.row < rows_ && position.column < columns_;
}

const std::string& Board::tile(Position position) const noexcept {
    return tiles_[position.row * columns_ + position.column];
}

}  // namespace wjs
