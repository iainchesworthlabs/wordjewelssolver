// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/phase_data.hpp"

namespace wjs {

// Binds a traced path into grapheme blocks, as the finger moves: tiles that
// make one sound together are drawn as one block (a shared bar, a common edge).
// Dice carry single letters (and "Qu"), never a digraph, so a block is a run of
// consecutive tiles whose letters spell a multi-letter grapheme of the active
// phase.
//
// The match is greedy and longest first: at each tile, the longest grapheme
// that the letters from there spell and that ends at a tile boundary is taken,
// and the next block starts after it. So with "ai" and "air" in the phase, A I R
// is one block; with "ai" and "ir", A I R is "ai" and then R. A tile that
// starts no grapheme is a block of its own.
//
// The binder knows no grapheme of its own; it is given the phase's.
class GraphemeBinder {
public:
    // One run of tiles of the path.
    struct Block {
        std::size_t first = 0;  // the index in the path of its first tile
        std::size_t tiles = 1;  // how many tiles
        // The grapheme's letters, lower-case, when tiles is above one; empty for
        // a tile on its own.
        std::string grapheme;

        // Two or more tiles that make one sound.
        [[nodiscard]] bool linked() const noexcept { return tiles > 1; }
    };

    // The multi-letter graphemes of `graphemes` are kept; a single letter is a
    // tile on its own already, so it is not a block, and a split digraph is not
    // found in flight, so it is left out.
    explicit GraphemeBinder(std::span<const Grapheme> graphemes);
    explicit GraphemeBinder(const PhaseIndex& index);

    // The blocks of a path, given the letters of its tiles in order (one to
    // three letters each, either case). They cover the path, in order, each
    // tile in exactly one block.
    [[nodiscard]] std::vector<Block> bind(std::span<const std::string> tiles) const;

    // The same, reading the tiles of `path` from `board`. Every position must be
    // on the board.
    [[nodiscard]] std::vector<Block> bind(const Board& board, std::span<const Position> path) const;

private:
    // Lower-case, longest first, then in letter order so that ties are the same
    // everywhere.
    std::vector<std::string> graphemes_;
};

}  // namespace wjs
