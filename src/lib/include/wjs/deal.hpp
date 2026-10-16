#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dice.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/solver.hpp"

// The deal of the standard board: a roll of the dice that is kept only if it
// holds a word. The board is decided and checked before the shake (the author,
// 2026-10-16), so a child never lands on a board with nothing to find. With the
// project's dice and the full dictionary 3.8% of 3x3 rolls and 0.15% of 4x4
// rolls hold no word of three letters or more, and wjs::roll does not look.
//
// The phonics board needs no such check: seed_board plants its targets, so its
// board always holds them.

namespace wjs {

enum class DealError {
    bad_options,  // min_viable or max_rolls is zero
    bad_dice,     // the dice make no board (DiceSet::side() is zero or above 16)
    no_board,     // max_rolls rolls were made and none held min_viable words
};

[[nodiscard]] std::string_view to_string(DealError error) noexcept;

struct DealOptions {
    // How many words of the active set (three letters or more) the board must
    // hold. Must be at least 1: a board of none is what the deal exists to
    // refuse.
    std::size_t min_viable = 1;

    // The most rolls made before giving up. Each costs one solve. Must be at
    // least 1. A 3x3 roll is empty about one time in 26, so twenty rolls in a
    // row are empty about once in 10^28.
    std::size_t max_rolls = 20;
};

struct DealtBoard {
    Board board;

    // Every viable word on the board as viable_words reports them: what
    // start_tile_help and is_cleared take, so the application does not solve
    // again. At least options.min_viable of them.
    std::vector<FoundWord> viable;

    // Rolls made: 1 to max_rolls.
    std::size_t rolls = 0;

    // The seed of the roll that made this board, so wjs::roll(dice, seed) is
    // `board` again.
    std::uint32_t seed = 0;
};

// The seed of the roll after the one made with `seed`: the first output of
// std::mt19937 seeded with it, which the standard fixes, so it is the same on
// every compiler.
[[nodiscard]] std::uint32_t next_deal_seed(std::uint32_t seed);

// Rolls with `seed`; if the board holds fewer than min_viable words of
// `words.active`, rolls with next_deal_seed of that, and so on up to max_rolls.
// A pure function of the dice, the words, the options and the seed: the same
// board on every compiler, whatever has been dealt before.
[[nodiscard]] std::expected<DealtBoard, DealError> deal_standard_board(
    const DiceSet& dice, const WordSets& words, std::uint32_t seed,
    const DealOptions& options = {});

}  // namespace wjs
