// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/dice.hpp"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <span>
#include <sstream>
#include <utility>

#include "ascii.hpp"
#include "draw.hpp"
#include "wjs/profiling.hpp"

namespace wjs {
namespace {

constexpr std::string_view byte_order_mark = "\xEF\xBB\xBF";

using ascii::to_upper;

[[nodiscard]] bool is_separator(char c) noexcept {
    return c == ' ' || c == '\t' || c == ',' || c == '\r';
}

}  // namespace

std::string_view to_string(DiceError error) noexcept {
    switch (error) {
        case DiceError::empty:
            return "the set has no dice";
        case DiceError::bad_die:
            return "a die has too many faces";
        case DiceError::bad_face:
            return "a face is not one to three letters";
        case DiceError::not_square:
            return "the number of dice is not a square";
        case DiceError::wrong_count:
            return "the set has not the dice for a board of that size";
        case DiceError::unreadable:
            return "the dice file could not be read";
    }
    return "unknown dice error";
}

std::expected<DiceSet, DiceError> DiceSet::parse(std::string_view text) {
    std::vector<std::vector<std::string>> dice;
    std::size_t start = 0;
    bool first_line = true;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        start = end + 1;

        if (first_line && line.starts_with(byte_order_mark)) {
            line.remove_prefix(byte_order_mark.size());
        }
        first_line = false;

        std::vector<std::string> faces;
        std::size_t at = 0;
        bool comment = false;
        while (at < line.size()) {
            if (is_separator(line[at])) {
                ++at;
                continue;
            }
            if (faces.empty() && line[at] == '#') {
                comment = true;
                break;
            }
            std::size_t stop = at;
            while (stop < line.size() && !is_separator(line[stop])) {
                ++stop;
            }
            const std::string_view token = line.substr(at, stop - at);
            if (token.size() > Board::max_tile_letters) {
                return std::unexpected(DiceError::bad_face);
            }
            std::string face;
            for (const char c : token) {
                const char upper = to_upper(c);
                if (upper < 'A' || upper > 'Z') {
                    return std::unexpected(DiceError::bad_face);
                }
                face.push_back(upper);
            }
            faces.push_back(std::move(face));
            at = stop;
        }
        if (comment || faces.empty()) {
            continue;
        }
        if (faces.size() > max_faces) {
            return std::unexpected(DiceError::bad_die);
        }
        dice.push_back(std::move(faces));
    }

    if (dice.empty()) {
        return std::unexpected(DiceError::empty);
    }
    return DiceSet(std::move(dice));
}

std::expected<DiceSet, DiceError> DiceSet::load(std::istream& in) {
    std::ostringstream text;
    text << in.rdbuf();
    if (in.bad()) {
        return std::unexpected(DiceError::unreadable);
    }
    return parse(text.str());
}

std::expected<DiceSet, DiceError> DiceSet::load_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(DiceError::unreadable);
    }
    return load(file);
}

std::size_t DiceSet::side() const noexcept {
    std::size_t side = 0;
    while (side * side < dice_.size()) {
        ++side;
    }
    return side * side == dice_.size() ? side : 0;
}

std::expected<RolledBoard, DiceError> roll_with_placements(const DiceSet& dice,
                                                           std::uint32_t seed) {
    // The one zone of a roll: roll() is this and the board taken out of it.
    auto zone = profiling::zone<"board.roll">();
    zone.Value(dice.size());

    const std::size_t side = dice.side();
    if (side == 0) {
        return std::unexpected(DiceError::not_square);
    }
    if (side > Board::max_dimension) {
        return std::unexpected(DiceError::wrong_count);
    }

    // The draws, in this order and no other: the shuffle, then a face for each
    // tile row-major. A pinned seed is a pinned board and pinned placements.
    detail::Draw draw(seed);
    std::vector<std::size_t> order(dice.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    draw.shuffle(std::span(order));

    // The tile at (row, column) shows die order[row * side + column].
    std::vector<Placement> placements;
    placements.reserve(dice.size());
    std::string text;
    for (std::size_t row = 0; row < side; ++row) {
        if (row > 0) {
            text += '/';
        }
        for (std::size_t column = 0; column < side; ++column) {
            if (column > 0) {
                text += ' ';
            }
            const std::size_t die = order[row * side + column];
            const auto& faces = dice.die(die);
            const std::size_t face = draw.below(faces.size());
            text += faces[face];
            placements.push_back({die, face});
        }
    }

    auto board = Board::parse(text);
    if (!board) {
        return std::unexpected(DiceError::bad_face);
    }
    return RolledBoard{*std::move(board), std::move(placements)};
}

std::expected<Board, DiceError> roll(const DiceSet& dice, std::uint32_t seed) {
    auto rolled = roll_with_placements(dice, seed);
    if (!rolled) {
        return std::unexpected(rolled.error());
    }
    return std::move(rolled->board);
}

}  // namespace wjs
