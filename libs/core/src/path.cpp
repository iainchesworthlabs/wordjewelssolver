// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/path.hpp"

#include <vector>

#include "wjs/profiling.hpp"

namespace wjs {

std::string_view to_string(PathError error) noexcept {
    switch (error) {
        case PathError::empty:
            return "the path has no tiles";
        case PathError::off_board:
            return "a tile of the path is not on the board";
        case PathError::repeated_tile:
            return "the path uses a tile twice";
        case PathError::not_adjacent:
            return "two tiles of the path do not touch";
    }
    return "unknown path error";
}

std::expected<std::string, PathError> validate_path(const Board& board,
                                                    std::span<const Position> path) {
    auto zone = profiling::zone<"path.validate">();
    zone.Value(path.size());

    if (path.empty()) {
        return std::unexpected(PathError::empty);
    }

    std::vector<bool> used(board.rows() * board.columns(), false);
    std::string word;

    for (std::size_t i = 0; i < path.size(); ++i) {
        const Position at = path[i];
        if (!board.contains(at)) {
            return std::unexpected(PathError::off_board);
        }
        const std::size_t index = at.row * board.columns() + at.column;
        if (used[index]) {
            return std::unexpected(PathError::repeated_tile);
        }
        if (i > 0 && !are_neighbours(path[i - 1], at)) {
            return std::unexpected(PathError::not_adjacent);
        }
        used[index] = true;
        word += board.tile(at);
    }
    return word;
}

}  // namespace wjs
