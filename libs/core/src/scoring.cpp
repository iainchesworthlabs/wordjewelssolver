// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scoring.hpp"

#include <array>
#include <cstddef>

namespace wjs {
namespace {

// How often each letter turns up in English text, in percent (the table the
// original solver used), indexed A..Z.
constexpr std::array<double, 26> letter_frequency_percent = {
    8.12,   // A
    1.49,   // B
    2.71,   // C
    4.32,   // D
    12.02,  // E
    2.30,   // F
    2.03,   // G
    5.92,   // H
    7.31,   // I
    0.10,   // J
    0.69,   // K
    3.98,   // L
    2.61,   // M
    6.95,   // N
    7.68,   // O
    1.82,   // P
    0.11,   // Q
    6.02,   // R
    6.28,   // S
    9.10,   // T
    2.88,   // U
    1.11,   // V
    2.09,   // W
    0.17,   // X
    2.11,   // Y
    0.07,   // Z
};

[[nodiscard]] constexpr int standard_boggle_score(std::size_t length) noexcept {
    if (length < 3) {
        return 0;
    }
    if (length <= 4) {
        return 1;
    }
    switch (length) {
        case 5:
            return 2;
        case 6:
            return 3;
        case 7:
            return 5;
        default:
            return 11;
    }
}

[[nodiscard]] int letter_rarity_score(std::string_view word) noexcept {
    if (word.empty()) {
        return 0;
    }
    double total = 0.0;
    for (const char c : word) {
        std::size_t index = 26;
        if (c >= 'A' && c <= 'Z') {
            index = static_cast<std::size_t>(c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            index = static_cast<std::size_t>(c - 'a');
        }
        // A character that is not a letter adds nothing but still counts in
        // the mean, as it did in the original.
        if (index < letter_frequency_percent.size()) {
            total += 100.0 / letter_frequency_percent[index];
        }
    }
    return static_cast<int>(total / static_cast<double>(word.size()));
}

// The English Scrabble tile values, indexed A..Z. They are a public table and
// a simple, explainable proxy for how rare a letter is.
constexpr std::array<int, 26> letter_value = {
    1, 3, 3, 2, 1, 4, 2, 4, 1, 8, 5, 1, 3, 1, 1, 3, 10, 1, 1, 1, 1, 4, 4, 8, 4, 10,
};

[[nodiscard]] int points_for_length(std::size_t length) noexcept {
    switch (length) {
        case 3:
            return 10;
        case 4:
            return 20;
        case 5:
            return 35;
        case 6:
            return 55;
        case 7:
            return 80;
        case 8:
            return 110;
        default:
            return 110 + static_cast<int>(length - 8) * 20;
    }
}

}  // namespace

int score_word(std::string_view word, ScoringScheme scheme) noexcept {
    switch (scheme) {
        case ScoringScheme::standard_boggle:
            return standard_boggle_score(word.size());
        case ScoringScheme::letter_rarity:
            return letter_rarity_score(word);
    }
    return 0;
}

int word_points(std::string_view word) noexcept {
    if (word.size() < 3) {
        return 0;
    }
    int total = points_for_length(word.size());
    for (const char c : word) {
        if (c >= 'A' && c <= 'Z') {
            total += letter_value[static_cast<std::size_t>(c - 'A')];
        } else if (c >= 'a' && c <= 'z') {
            total += letter_value[static_cast<std::size_t>(c - 'a')];
        }
    }
    if (word.size() > 5) {
        total += static_cast<int>(word.size() - 5) * 10;
    }
    return total;
}

}  // namespace wjs
