// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scan/confirmation.hpp"

#include <algorithm>
#include <utility>

namespace wjs::scan {

std::string_view to_string(DraftError error) noexcept {
    switch (error) {
        case DraftError::empty_grid:
            return "the grid has no rows or no columns";
        case DraftError::too_large:
            return "the grid is larger than a board can be";
        case DraftError::bad_letters:
            return "a tile is not one to three letters A to Z";
        case DraftError::off_grid:
            return "the position is not on the grid";
        case DraftError::incomplete:
            return "a tile has no letters";
        case DraftError::inconsistent_reading:
            return "the reading does not have a tile for each cell of its grid";
    }
    return "unknown draft error";
}

namespace {

// The letters upper-cased, or nothing if they are not one to three of A to Z.
std::expected<std::string, DraftError> normalised(std::string_view letters) {
    if (letters.empty() || letters.size() > Board::max_tile_letters) {
        return std::unexpected(DraftError::bad_letters);
    }
    std::string out;
    for (const char c : letters) {
        // ASCII only: std::toupper follows the locale, and a letter outside A to Z
        // is refused either way.
        const char upper = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        if (upper < 'A' || upper > 'Z') {
            return std::unexpected(DraftError::bad_letters);
        }
        out.push_back(upper);
    }
    return out;
}

std::expected<void, DraftError> check_size(std::size_t rows, std::size_t columns) {
    if (rows == 0 || columns == 0) {
        return std::unexpected(DraftError::empty_grid);
    }
    if (rows > Board::max_dimension || columns > Board::max_dimension) {
        return std::unexpected(DraftError::too_large);
    }
    return {};
}

}  // namespace

BoardDraft::BoardDraft(std::size_t rows, std::size_t columns, std::vector<DraftTile> tiles,
                       bool typed)
    : rows_(rows), columns_(columns), tiles_(std::move(tiles)), typed_(typed) {}

std::expected<BoardDraft, DraftError> BoardDraft::try_from_reading(const BoardReading& reading) {
    if (const auto ok = check_size(reading.rows, reading.columns); !ok) {
        return std::unexpected(ok.error());
    }
    if (reading.tiles.size() != reading.rows * reading.columns) {
        return std::unexpected(DraftError::inconsistent_reading);
    }
    std::vector<DraftTile> tiles;
    tiles.reserve(reading.tiles.size());
    for (const TileReading& read : reading.tiles) {
        DraftTile tile;
        tile.letters = read.letters;
        tile.confidence = read.confidence;
        tile.alternatives = read.alternatives;
        if (read.letters.empty()) {
            tile.origin = TileOrigin::empty;
            tile.doubtful = true;
        } else {
            tile.origin = TileOrigin::read;
            tile.doubtful = read.doubtful;
        }
        tiles.push_back(std::move(tile));
    }
    BoardDraft draft(reading.rows, reading.columns, std::move(tiles), false);
    return draft;
}

BoardDraft BoardDraft::from_reading(const BoardReading& reading) {
    if (auto draft = try_from_reading(reading)) {
        return std::move(*draft);
    }
    const std::size_t rows = std::clamp<std::size_t>(reading.rows, 1, Board::max_dimension);
    const std::size_t columns = std::clamp<std::size_t>(reading.columns, 1, Board::max_dimension);
    std::vector<DraftTile> tiles(rows * columns);
    for (DraftTile& tile : tiles) {
        tile.doubtful = true;
    }
    return {rows, columns, std::move(tiles), false};
}

std::expected<BoardDraft, DraftError> BoardDraft::empty(std::size_t rows, std::size_t columns) {
    if (const auto ok = check_size(rows, columns); !ok) {
        return std::unexpected(ok.error());
    }
    return BoardDraft(rows, columns, std::vector<DraftTile>(rows * columns), true);
}

std::expected<void, DraftError> BoardDraft::set_tile(Position position, std::string_view letters) {
    if (!contains(position)) {
        return std::unexpected(DraftError::off_grid);
    }
    auto upper = normalised(letters);
    if (!upper) {
        return std::unexpected(upper.error());
    }
    DraftTile& tile = tiles_[position.row * columns_ + position.column];
    tile.letters = std::move(*upper);
    // In a grid that was typed there was nothing read to correct.
    tile.origin = typed_ ? TileOrigin::typed : TileOrigin::corrected;
    tile.confidence = 1.0;
    tile.doubtful = false;
    tile.alternatives = {};
    return {};
}

std::expected<void, DraftError> BoardDraft::accept_tile(Position position) {
    if (!contains(position)) {
        return std::unexpected(DraftError::off_grid);
    }
    DraftTile& tile = tiles_[position.row * columns_ + position.column];
    if (tile.letters.empty()) {
        return std::unexpected(DraftError::incomplete);
    }
    if (tile.doubtful) {
        tile.origin = TileOrigin::corrected;
        tile.confidence = 1.0;
        tile.doubtful = false;
    }
    return {};
}

std::expected<void, DraftError> BoardDraft::clear_tile(Position position) {
    if (!contains(position)) {
        return std::unexpected(DraftError::off_grid);
    }
    tiles_[position.row * columns_ + position.column] = DraftTile{};
    return {};
}

void BoardDraft::turn(Turn direction) {
    std::vector<DraftTile> turned(tiles_.size());
    const std::size_t new_rows = columns_;
    const std::size_t new_columns = rows_;
    for (std::size_t i = 0; i < new_rows; ++i) {
        for (std::size_t j = 0; j < new_columns; ++j) {
            // Clockwise: the old left column becomes the top row, read upward.
            const std::size_t old_row = direction == Turn::clockwise ? rows_ - 1 - j : j;
            const std::size_t old_column = direction == Turn::clockwise ? i : columns_ - 1 - i;
            turned[i * new_columns + j] = tiles_[old_row * columns_ + old_column];
        }
    }
    tiles_ = std::move(turned);
    rows_ = new_rows;
    columns_ = new_columns;
}

std::expected<void, DraftError> BoardDraft::resize(std::size_t rows, std::size_t columns) {
    if (const auto ok = check_size(rows, columns); !ok) {
        return std::unexpected(ok.error());
    }
    std::vector<DraftTile> resized(rows * columns);
    for (std::size_t i = 0; i < std::min(rows, rows_); ++i) {
        for (std::size_t j = 0; j < std::min(columns, columns_); ++j) {
            resized[i * columns + j] = tiles_[i * columns_ + j];
        }
    }
    tiles_ = std::move(resized);
    rows_ = rows;
    columns_ = columns;
    return {};
}

std::vector<Position> BoardDraft::doubtful() const {
    std::vector<Position> out;
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        if (tiles_[i].doubtful) {
            out.push_back({i / columns_, i % columns_});
        }
    }
    return out;
}

std::vector<Position> BoardDraft::unfilled() const {
    std::vector<Position> out;
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        if (tiles_[i].letters.empty()) {
            out.push_back({i / columns_, i % columns_});
        }
    }
    return out;
}

bool BoardDraft::complete() const {
    return std::ranges::none_of(tiles_, [](const DraftTile& t) { return t.letters.empty(); });
}

std::expected<Board, DraftError> BoardDraft::confirm() const {
    if (!complete()) {
        return std::unexpected(DraftError::incomplete);
    }
    // Board's own parser makes the board, so a draft cannot produce one that a
    // typed-in line could not.
    std::string text;
    for (std::size_t row = 0; row < rows_; ++row) {
        if (row > 0) {
            text.push_back('/');
        }
        for (std::size_t column = 0; column < columns_; ++column) {
            if (column > 0) {
                text.push_back(' ');
            }
            text += tiles_[row * columns_ + column].letters;
        }
    }
    auto board = Board::parse(text);
    if (!board) {
        return std::unexpected(board.error() == BoardError::too_large ? DraftError::too_large
                                                                      : DraftError::bad_letters);
    }
    return std::move(*board);
}

}  // namespace wjs::scan
