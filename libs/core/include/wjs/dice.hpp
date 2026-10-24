// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wjs/board.hpp"

// The dice of the standard board: a set of dice, each with faces, and a roll
// that puts them on a board. A face is a tile of one to three letters (the
// "Qu" face of a Boggle die is the two-letter tile QU).
//
// A set is a text file, one die a line, faces separated by spaces or commas,
// `#` lines being the header (data/dice/<n>x<n>.txt):
//
//     # dice for a 4x4 board ...
//     # source-sha256: <hash of the word list the counts came from>
//     E A R I T N
//     S O L Q C D
//     ...
//
// The library holds no set of its own. The project's sets are made by
// tools/data/make_dice.ps1 from the dictionary's letter counts.

namespace wjs {

enum class DiceError {
    empty,        // no dice
    bad_die,      // a die has more than max_faces faces (a line with none is skipped)
    bad_face,     // a face is not one to three letters A-Z
    not_square,   // the number of dice is not a square, so no n by n board
    wrong_count,  // asked for a board of a size the set has not the dice for
    unreadable,   // the file could not be read
};

[[nodiscard]] std::string_view to_string(DiceError error) noexcept;

class DiceSet {
public:
    static constexpr std::size_t max_faces = 12;

    // Faces are stored upper-case.
    [[nodiscard]] static std::expected<DiceSet, DiceError> parse(std::string_view text);
    [[nodiscard]] static std::expected<DiceSet, DiceError> load(std::istream& in);
    [[nodiscard]] static std::expected<DiceSet, DiceError> load_file(
        const std::filesystem::path& path);

    // How many dice.
    [[nodiscard]] std::size_t size() const noexcept { return dice_.size(); }

    // The faces of die `index`, which must be below size().
    [[nodiscard]] const std::vector<std::string>& die(std::size_t index) const {
        return dice_[index];
    }

    // The side of the square board these dice make, or zero when their number
    // is not a square.
    [[nodiscard]] std::size_t side() const noexcept;

private:
    explicit DiceSet(std::vector<std::vector<std::string>> dice) : dice_(std::move(dice)) {}

    std::vector<std::vector<std::string>> dice_;
};

// Which die lies on a tile of a rolled board, and which of its faces is up.
struct Placement {
    std::size_t die = 0;   // an index into the set: DiceSet::die(die)
    std::size_t face = 0;  // an index into that die's faces: the one that is up

    friend constexpr bool operator==(const Placement&, const Placement&) = default;
};

// A rolled board and where it came from. `placements` has one entry for each
// tile, row-major, and `die(placements[t].die)[placements[t].face]` is the
// letters of tile t. Every die of the set is used once. The faces that are not
// up are the die's other faces, and nothing in the game reads them: they are
// there for a scene that shows a die tumbling (docs/design/dice-and-faces.md).
struct RolledBoard {
    Board board;
    std::vector<Placement> placements;
};

// Shakes the dice onto a square board: they are put in a random order, a die to
// a tile, and each shows one of its faces. A pure function of the set and the
// seed: std::mt19937 taken modulo and a shuffle of the library's own, so a seed
// is the same board and the same placements on every compiler.
[[nodiscard]] std::expected<RolledBoard, DiceError> roll_with_placements(const DiceSet& dice,
                                                                         std::uint32_t seed);

// The board of roll_with_placements, for a caller that does not draw the dice:
// for a seed, the same board as roll_with_placements(dice, seed)->board.
[[nodiscard]] std::expected<Board, DiceError> roll(const DiceSet& dice, std::uint32_t seed);

}  // namespace wjs
