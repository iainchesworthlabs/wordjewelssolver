// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace wjs {

enum class ScoringScheme {
    // The scoring printed in the Boggle rules: 3 or 4 letters score 1, 5 score
    // 2, 6 score 3, 7 score 5, and 8 or more score 11.
    standard_boggle,

    // The scheme of the original solver: the mean of 100 / (the percentage
    // frequency of each letter in English), so a word made of rare letters
    // scores higher than one made of common letters.
    letter_rarity,
};

// The score of a word of letters A-Z, in either case.
[[nodiscard]] int score_word(std::string_view word, ScoringScheme scheme) noexcept;

// The child-facing points of a word found in Solo Play, independent of the
// solver's two schemes. It rewards three things a child can see:
//
//   - length: 10, 20, 35, 55, 80, then 110 and 20 for each letter beyond eight;
//   - the letters themselves: the English Scrabble values (A1 ... Q10 Z10), so
//     a word with a rare letter is worth more than a plain one of the same
//     length;
//   - a long-word bonus of 10 for each letter past five.
//
// A word shorter than the game's minimum is worth nothing. The values are a
// transparent proxy for rarity, not a claim about how common the word is:
// the repository has no word-frequency list, and the screen says "rare
// letters", never "uncommon word".
[[nodiscard]] int word_points(std::string_view word) noexcept;

}  // namespace wjs
