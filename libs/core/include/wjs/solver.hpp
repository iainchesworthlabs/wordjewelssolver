// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/scoring.hpp"

namespace wjs {

struct SolveOptions {
    // Words shorter than this are not reported. Counted in letters, so the
    // "Qu" tile is two.
    std::size_t min_length = 3;

    ScoringScheme scoring = ScoringScheme::standard_boggle;
};

struct FoundWord {
    std::string word;
    // The tiles that spell it, in order. A tile is used at most once.
    std::vector<Position> path;
    int score = 0;
};

// Every dictionary word that can be traced through neighbouring tiles
// (horizontally, vertically or diagonally) without using a tile twice. Each
// word appears once, with the first path found for it, ordered by score
// (highest first), then length (longest first), then alphabetically.
[[nodiscard]] std::vector<FoundWord> solve(const Board& board, const Dictionary& dictionary,
                                           const SolveOptions& options = {});

}  // namespace wjs
