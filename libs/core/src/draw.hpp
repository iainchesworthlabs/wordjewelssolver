// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <utility>

namespace wjs::detail {

// The only source of randomness in the library: std::mt19937, whose output is
// fixed by the standard, taken modulo. A draw is never part of a larger
// expression, so the order the draws are made in does not depend on the
// compiler, and std::uniform_int_distribution and std::shuffle, whose
// algorithms are each standard library's own, are never used. A seed is the
// same sequence, and so the same board, on every compiler.
class Draw {
public:
    explicit Draw(std::uint32_t seed) : engine_(seed) {}

    // 0 to n - 1. n must be above 0.
    [[nodiscard]] std::size_t below(std::size_t n) {
        const auto value = static_cast<std::uint32_t>(engine_());
        return static_cast<std::size_t>(value % static_cast<std::uint32_t>(n));
    }

    template <typename T>
    void shuffle(std::span<T> items) {
        for (std::size_t i = items.size(); i > 1; --i) {
            const std::size_t j = below(i);
            std::swap(items[i - 1], items[j]);
        }
    }

private:
    std::mt19937 engine_;
};

}  // namespace wjs::detail
