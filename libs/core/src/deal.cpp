// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/deal.hpp"

#include <random>
#include <utility>

#include "wjs/profiling.hpp"

namespace wjs {

std::string_view to_string(DealError error) noexcept {
    switch (error) {
        case DealError::bad_options:
            return "bad_options";
        case DealError::bad_board:
            return "bad_board";
        case DealError::no_board:
            return "no_board";
    }
    return "unknown";
}

std::uint32_t next_deal_seed(std::uint32_t seed) {
    std::mt19937 engine(seed);
    return static_cast<std::uint32_t>(engine());
}

std::expected<DealtBoard, DealError> deal_playable(const WordSets& words,
                                                   const BoardMaker& make_board, std::uint32_t seed,
                                                   const DealOptions& options) {
    auto zone = profiling::zone<"board.deal_playable">();

    if (options.min_viable == 0 || options.max_rolls == 0 || !make_board) {
        return std::unexpected(DealError::bad_options);
    }

    for (std::size_t roll_number = 1; roll_number <= options.max_rolls; ++roll_number) {
        auto board = make_board(seed);
        if (!board) {
            return std::unexpected(DealError::bad_board);
        }
        auto viable = viable_words(*board, words);
        if (viable.size() >= options.min_viable) {
            zone.Value(roll_number);
            return DealtBoard{.board = *std::move(board),
                              .viable = std::move(viable),
                              .rolls = roll_number,
                              .seed = seed};
        }
        seed = next_deal_seed(seed);
    }
    zone.Value(options.max_rolls);
    return std::unexpected(DealError::no_board);
}

std::expected<DealtBoard, DealError> deal_standard_board(const DiceSet& dice, const WordSets& words,
                                                         std::uint32_t seed,
                                                         const DealOptions& options) {
    const BoardMaker rolled = [&dice](std::uint32_t s) {
        return roll(dice, s).transform_error([](DiceError) { return DealError::bad_board; });
    };
    return deal_playable(words, rolled, seed, options);
}

}  // namespace wjs
