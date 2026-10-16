#include "wjs/deal.hpp"

#include <random>
#include <utility>

namespace wjs {

std::string_view to_string(DealError error) noexcept {
    switch (error) {
        case DealError::bad_options:
            return "bad_options";
        case DealError::bad_dice:
            return "bad_dice";
        case DealError::no_board:
            return "no_board";
    }
    return "unknown";
}

std::uint32_t next_deal_seed(std::uint32_t seed) {
    std::mt19937 engine(seed);
    return static_cast<std::uint32_t>(engine());
}

std::expected<DealtBoard, DealError> deal_standard_board(const DiceSet& dice, const WordSets& words,
                                                         std::uint32_t seed,
                                                         const DealOptions& options) {
    if (options.min_viable == 0 || options.max_rolls == 0) {
        return std::unexpected(DealError::bad_options);
    }

    for (std::size_t roll_number = 1; roll_number <= options.max_rolls; ++roll_number) {
        auto board = roll(dice, seed);
        if (!board) {
            return std::unexpected(DealError::bad_dice);
        }
        auto viable = viable_words(*board, words);
        if (viable.size() >= options.min_viable) {
            return DealtBoard{.board = *std::move(board),
                              .viable = std::move(viable),
                              .rolls = roll_number,
                              .seed = seed};
        }
        seed = next_deal_seed(seed);
    }
    return std::unexpected(DealError::no_board);
}

}  // namespace wjs
