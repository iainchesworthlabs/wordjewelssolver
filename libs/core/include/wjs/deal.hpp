// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dice.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/solver.hpp"

// The deal of a board that must hold a word: a board is made, solved, and made
// again from the next seed until it holds one. The board is decided and checked
// before the shake (the author, 2026-10-16), so a child never lands on a board
// with nothing to find. With the project's dice and the full dictionary 3.8% of
// 3x3 rolls and 0.15% of 4x4 rolls hold no word of three letters or more, and
// wjs::roll does not look.
//
// The retry rule is the same whoever makes the board, so it takes the maker as a
// parameter and knows nothing of dice. The standard board's deal,
// deal_standard_board, is this with a maker that calls wjs::roll(dice, s) and
// maps its DiceError to DealError::bad_board. The phonics board needs none of
// it: seed_board plants its targets.

namespace wjs {

enum class DealError {
    bad_options,  // min_viable or max_rolls is zero
    bad_board,    // the maker could not make a board
    no_board,     // max_rolls boards were made and none held min_viable words
};

[[nodiscard]] std::string_view to_string(DealError error) noexcept;

struct DealOptions {
    // How many words of the active set (three letters or more) the board must
    // hold. Must be at least 1: a board of none is what the deal exists to
    // refuse.
    std::size_t min_viable = 1;

    // The most boards made before giving up. Each costs one solve. Must be at
    // least 1. A 3x3 roll is empty about one time in 26, so twenty in a row are
    // empty about once in 10^28.
    std::size_t max_rolls = 20;
};

struct DealtBoard {
    Board board;

    // Every viable word on the board as viable_words reports them: what
    // start_tile_help and is_cleared take, so the application does not solve
    // again. At least options.min_viable of them.
    std::vector<FoundWord> viable;

    // Boards made: 1 to max_rolls.
    std::size_t rolls = 0;

    // The seed the maker was given for this board.
    std::uint32_t seed = 0;
};

// Makes a board from a seed. Nothing is thrown: a seed it cannot make a board
// from is an error, which ends the deal as DealError::bad_board.
using BoardMaker = std::function<std::expected<Board, DealError>(std::uint32_t seed)>;

// The seed after `seed`: the first output of std::mt19937 seeded with it, which
// the standard fixes, so it is the same on every compiler.
[[nodiscard]] std::uint32_t next_deal_seed(std::uint32_t seed);

// Makes a board with `seed`; if it holds fewer than min_viable words of
// `words.active`, makes one with next_deal_seed of that, and so on up to
// max_rolls. A pure function of the maker, the words, the options and the seed,
// when the maker is one of its seed.
[[nodiscard]] std::expected<DealtBoard, DealError> deal_playable(const WordSets& words,
                                                                 const BoardMaker& make_board,
                                                                 std::uint32_t seed,
                                                                 const DealOptions& options = {});

// The standard board: deal_playable over wjs::roll(dice, s). Dice that make no
// board (DiceSet::side() zero or above 16) are DealError::bad_board.
[[nodiscard]] std::expected<DealtBoard, DealError> deal_standard_board(
    const DiceSet& dice, const WordSets& words, std::uint32_t seed,
    const DealOptions& options = {});

}  // namespace wjs
