// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/path.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/solver.hpp"

// The rules of a board in play: what a traced path is, what a find earns, which
// tile the help lights, and when the board is done. They are what the touch
// handler and the tray call, and nothing here draws, plays a sound or keeps a
// score between boards: the library answers a question about one board and the
// application keeps the answers (docs/design/game.md).
//
// Two board modes use the same rules with a different word set (the author,
// 2026-10-16): the standard board, a roll of dice, counts every word of the
// full dictionary; the phonics board counts the words of the active phase's
// index and nothing else. A word set is a pair of dictionaries (WordSets), so
// the rules do not know which board they are on.

namespace wjs {

// A word of fewer letters than this is not a word of the game: it is not
// counted, has no tier and is never a start tile. The solver's default minimum
// length, and the phase data's (min_phase_word_letters), are the same number.
inline constexpr std::size_t min_word_letters = 3;

// The words a board is played against. Neither dictionary is copied: both must
// outlive the WordSets.
struct WordSets {
    // Every real word. A traced word that is in here and not in `active` is a
    // real word that is not today's.
    const Dictionary& dictionary;

    // The words that count on this board. The standard board's is `dictionary`
    // itself, so there every real word counts and the middle state of
    // WordState never arises.
    const Dictionary& active;

    [[nodiscard]] static WordSets standard(const Dictionary& words) noexcept {
        return {words, words};
    }

    // The phonics board: what counts is the phase index's words, which were
    // checked against the dictionary when the phases were built.
    [[nodiscard]] static WordSets phonics(const Dictionary& language,
                                          const PhaseIndex& index) noexcept {
        return {language, index.dictionary()};
    }
};

// ---------------------------------------------------------------------------
// 1. What a traced path is

enum class WordState {
    viable,           // a word of the active set, on this board
    real_not_todays,  // a dictionary word of three letters or more, but not one that counts here
    not_a_word,       // anything else, including a path that is not a path
};

[[nodiscard]] std::string_view to_string(WordState state) noexcept;

struct Classification {
    WordState state = WordState::not_a_word;

    // The word the path spells, upper-case, for a viable path and for nothing
    // else. The other two states are answered with no word on purpose: the
    // design never shows or reads a word that is not one of today's, so the
    // library does not hand the application one to show.
    std::string word;

    // Set when the path itself was refused by validate_path (state is then
    // not_a_word). A finger cannot make such a path; a replayed or typed one
    // can.
    std::optional<PathError> fault;
};

// Classifies a traced path. The path is checked with validate_path (so a Qu
// tile adds both its letters); a word of fewer than min_word_letters letters
// is not a word; otherwise it is viable when `words.active` has it, real but
// not today's when only `words.dictionary` has it, and not a word when neither
// does.
//
// A path is viable exactly when its word is among viable_words(board, words):
// a valid path that spells an active word is a word the solver would find.
[[nodiscard]] Classification classify_path(const Board& board, std::span<const Position> path,
                                           const WordSets& words);

// Every word of `words.active` of min_word_letters letters or more that can be
// traced on the board, once each, with the first path found: the words that
// count toward clearing it. A solve, so the application calls it once for a
// board and keeps the result for start_tile_help and is_cleared.
[[nodiscard]] std::vector<FoundWord> viable_words(const Board& board, const WordSets& words);

// ---------------------------------------------------------------------------
// 2. Tiers and stars

enum class Tier {
    spark,  // 3 letters
    power,  // 4 letters
    mega,   // 5 letters or more
};

[[nodiscard]] std::string_view to_string(Tier tier) noexcept;

// A word's tier follows from its letter count and is never stored: the count of
// letters in the word, which is what the solver and validate_path report, so a
// Qu tile has already added two. A word shorter than min_word_letters has none.
[[nodiscard]] constexpr std::optional<Tier> tier_for_letters(std::size_t letters) noexcept {
    if (letters < min_word_letters) {
        return std::nullopt;
    }
    if (letters == 3) {
        return Tier::spark;
    }
    if (letters == 4) {
        return Tier::power;
    }
    return Tier::mega;
}

struct Reward {
    // Earned on every find (a word found again on another board earns them
    // again).
    int stars = 0;

    // The tier carries a word sticker, given the first time the word is found
    // on this device. Whether this is the first time is the application's to
    // know; the library keeps nothing between boards.
    bool sticker = false;
};

// Spark: one star. Power: two stars and a sticker. Mega: five stars (its
// accessory belongs to a later release and is not modelled).
[[nodiscard]] constexpr Reward reward_for(Tier tier) noexcept {
    switch (tier) {
        case Tier::spark:
            return {.stars = 1, .sticker = false};
        case Tier::power:
            return {.stars = 2, .sticker = true};
        case Tier::mega:
            return {.stars = 5, .sticker = false};
    }
    return {};
}

// ---------------------------------------------------------------------------
// The words found on a board

// The words a child has found on one board, each once, in the order found.
// Case does not matter. Start a new one for each board.
class FoundWords {
public:
    // Records a word. True when it was new; false, and nothing changes, when it
    // had been found already or is empty. The caller awards stars on true.
    bool insert(std::string_view word);

    // Looks the word up as it is, in any case: no copy is made, so this is the
    // call to make for every candidate of a board.
    [[nodiscard]] bool contains(std::string_view word) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }
    [[nodiscard]] bool empty() const noexcept { return order_.empty(); }

    // Upper-case, in the order found: the tray's jewels.
    [[nodiscard]] const std::vector<std::string>& in_order() const noexcept { return order_; }

private:
    // The set holds the words upper-case and is searched with the word in any
    // case, so a lookup neither copies nor upper-cases the word.
    struct CaseBlindHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view word) const noexcept;
    };
    struct CaseBlindEqual {
        using is_transparent = void;
        [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept;
    };

    std::vector<std::string> order_;
    std::unordered_set<std::string, CaseBlindHash, CaseBlindEqual> seen_;
};

// ---------------------------------------------------------------------------
// 3. The start-tile help

struct StartTileHelp {
    std::string word;  // the word it is for, upper-case
    Position tile;     // the tile that starts it: the one to light
};

// The start tile of one viable word not yet found, as the design chooses it:
//
//   - the shortest word, counted in letters (so QUIT, three tiles, is four);
//   - of those, the one whose first tile is nearest the top-left tile, by the
//     straight-line distance between tile centres (compared as the sum of the
//     squares, so it is exact), the upper tile winning a tie and then the left
//     one;
//   - of those, the word that sorts first.
//
// The answer depends only on the words left, never on the order they are given
// in or on how often it is asked, so the same press gives the same answer. And
// finding a word other than the answer cannot change it, because the answer is
// the least of the words left in a fixed order: it stays until that word is
// found. When it is found the same call names the next, and there is no
// answer (nullopt) once no viable word is left.
//
// `viable` is viable_words(...) for the board. The tile is the first tile of
// the word's path as the solver found it, the first start tile in reading order
// that can trace the word, so the word can be traced from it. A word with no
// path cannot be pointed at and is passed over.
[[nodiscard]] std::optional<StartTileHelp> start_tile_help(std::span<const FoundWord> viable,
                                                           const FoundWords& found);

// How many of the board's words are still to find and begin with `prefix`
// (the tile the help lit, or its first two tiles): what the child is told
// after a find made from the lit start - whether the start still leads
// anywhere. A count, never a word (the design never hands the word itself to
// the screen). An empty prefix counts nothing.
[[nodiscard]] std::size_t words_starting_with(std::span<const FoundWord> viable,
                                              const FoundWords& found,
                                              std::span<const Position> prefix) noexcept;

// ---------------------------------------------------------------------------
// 4. Whether the board is cleared

// No viable word is left unfound. Words in `found` that are not viable are
// ignored. A board with no viable word at all is cleared in this literal sense,
// and the deal is meant never to make one: the board is decided and checked
// before the shake (the author, 2026-10-16), so this is the answer for a board
// that should not have been dealt, not a case the application plays.
[[nodiscard]] bool is_cleared(std::span<const FoundWord> viable, const FoundWords& found);

}  // namespace wjs
