// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <utility>
#include <vector>

#include "wjs/dice.hpp"

// The dice of every size the standard board is played at: one DiceSet for each
// side from 3 to 6, as data/dice holds them (3x3.txt to 6x6.txt). The game
// session picks the set of the size the child chose on the box.
//
// The library holds no set of its own: a DiceSets is made from sets the
// application read (make) or from a directory of the project's files
// (load_directory).

namespace wjs {

class DiceSets {
public:
    static constexpr std::size_t smallest_side = 3;
    static constexpr std::size_t largest_side = 6;

    // One set for each side from smallest_side to largest_side, in any order. A
    // set of another side, a side twice or a side missing is
    // DiceError::wrong_count; a set whose dice are not a square number is
    // DiceError::not_square.
    [[nodiscard]] static std::expected<DiceSets, DiceError> make(std::vector<DiceSet> sets);

    // `<n>x<n>.txt` of `directory` for each side, read with DiceSet::load_file;
    // the first error stops it. Each file must hold the dice of its own side: a
    // file whose dice make another board is DiceError::wrong_count (not_square
    // when their number is no square at all).
    [[nodiscard]] static std::expected<DiceSets, DiceError> load_directory(
        const std::filesystem::path& directory);

    // The set for a board of `side` by `side`; null for a side the standard
    // board is not played at.
    [[nodiscard]] const DiceSet* find(std::size_t side) const noexcept;

private:
    // Indexed by side - smallest_side.
    explicit DiceSets(std::vector<DiceSet> sets) : sets_(std::move(sets)) {}

    std::vector<DiceSet> sets_;
};

}  // namespace wjs
