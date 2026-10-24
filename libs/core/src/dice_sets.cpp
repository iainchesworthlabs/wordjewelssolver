// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/dice_sets.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "wjs/profiling.hpp"

namespace wjs {

std::expected<DiceSets, DiceError> DiceSets::make(std::vector<DiceSet> sets) {
    constexpr std::size_t count = largest_side - smallest_side + 1;
    if (sets.size() != count) {
        return std::unexpected(DiceError::wrong_count);
    }
    for (const DiceSet& set : sets) {
        const std::size_t side = set.side();
        if (side == 0) {
            return std::unexpected(DiceError::not_square);
        }
        if (side < smallest_side || side > largest_side) {
            return std::unexpected(DiceError::wrong_count);
        }
    }
    std::ranges::sort(sets, {}, &DiceSet::side);
    // Sorted, a set for each side from the smallest to the largest is the one
    // whose side is its place plus the smallest; a side twice leaves another out.
    for (std::size_t place = 0; place < count; ++place) {
        if (sets[place].side() != smallest_side + place) {
            return std::unexpected(DiceError::wrong_count);
        }
    }
    return DiceSets(std::move(sets));
}

std::expected<DiceSets, DiceError> DiceSets::load_directory(
    const std::filesystem::path& directory) {
    auto zone = profiling::zone<"dice.load_sets">();

    std::vector<DiceSet> sets;
    for (std::size_t side = smallest_side; side <= largest_side; ++side) {
        const std::string name = std::to_string(side) + "x" + std::to_string(side) + ".txt";
        auto set = DiceSet::load_file(directory / name);
        if (!set) {
            return std::unexpected(set.error());
        }
        // 4x4.txt holds the dice of a board of four: files swapped between two
        // names would otherwise pass for a complete set.
        if (set->side() == 0) {
            return std::unexpected(DiceError::not_square);
        }
        if (set->side() != side) {
            return std::unexpected(DiceError::wrong_count);
        }
        sets.push_back(*std::move(set));
    }
    zone.Value(sets.size());
    return make(std::move(sets));
}

const DiceSet* DiceSets::find(std::size_t side) const noexcept {
    if (side < smallest_side || side > largest_side) {
        return nullptr;
    }
    return &sets_[side - smallest_side];
}

}  // namespace wjs
