// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/game_rules.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

#include "ascii.hpp"
#include "wjs/profiling.hpp"

namespace wjs {
namespace {

// The three places that say "three letters" are one number.
static_assert(min_word_letters == min_phase_word_letters);

using ascii::upper;

// The squared straight-line distance from the top-left tile to this one,
// measured in tiles. Squared so that it stays an integer and a comparison is
// exact.
[[nodiscard]] constexpr std::size_t distance_from_top_left(Position tile) noexcept {
    return tile.row * tile.row + tile.column * tile.column;
}

// Whether `lhs` is the better word to light: shorter, then nearer the top left,
// then (so that the order is total) the upper tile, the left tile and the word
// that sorts first. Both words have a path.
[[nodiscard]] bool helps_before(const FoundWord& lhs, const FoundWord& rhs) noexcept {
    const Position from_lhs = lhs.path.front();
    const Position from_rhs = rhs.path.front();
    const std::size_t near_lhs = distance_from_top_left(from_lhs);
    const std::size_t near_rhs = distance_from_top_left(from_rhs);
    const std::size_t length_lhs = lhs.word.size();
    const std::size_t length_rhs = rhs.word.size();
    return std::tie(length_lhs, near_lhs, from_lhs.row, from_lhs.column, lhs.word) <
           std::tie(length_rhs, near_rhs, from_rhs.row, from_rhs.column, rhs.word);
}

}  // namespace

std::string_view to_string(WordState state) noexcept {
    switch (state) {
        case WordState::viable:
            return "viable";
        case WordState::real_not_todays:
            return "a real word, not one of today's";
        case WordState::not_a_word:
            return "not a word";
    }
    return "unknown word state";
}

std::string_view to_string(Tier tier) noexcept {
    switch (tier) {
        case Tier::spark:
            return "Spark";
        case Tier::power:
            return "Power";
        case Tier::mega:
            return "Mega";
    }
    return "unknown tier";
}

Classification classify_path(const Board& board, std::span<const Position> path,
                             const WordSets& words) {
    auto zone = profiling::zone<"rules.classify">();

    auto spelled = validate_path(board, path);
    if (!spelled) {
        return {.state = WordState::not_a_word, .word = {}, .fault = spelled.error()};
    }
    if (spelled->size() < min_word_letters) {
        return {};
    }
    if (words.active.contains(*spelled)) {
        return {.state = WordState::viable, .word = std::move(*spelled), .fault = std::nullopt};
    }
    if (words.dictionary.contains(*spelled)) {
        return {.state = WordState::real_not_todays, .word = {}, .fault = std::nullopt};
    }
    return {};
}

std::vector<FoundWord> viable_words(const Board& board, const WordSets& words) {
    SolveOptions options;
    options.min_length = min_word_letters;
    return solve(board, words.active, options);
}

bool FoundWords::insert(std::string_view word) {
    if (word.empty()) {
        return false;
    }
    std::string key = upper(word);
    if (!seen_.insert(key).second) {
        return false;
    }
    order_.push_back(std::move(key));
    return true;
}

std::size_t FoundWords::CaseBlindHash::operator()(std::string_view word) const noexcept {
    // FNV-1a over the upper-case letters, so every case of a word hashes alike.
    std::size_t hash = 14695981039346656037ULL;
    for (const char c : word) {
        hash ^= static_cast<unsigned char>(ascii::to_upper(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool FoundWords::CaseBlindEqual::operator()(std::string_view lhs,
                                            std::string_view rhs) const noexcept {
    return std::ranges::equal(
        lhs, rhs, [](char a, char b) { return ascii::to_upper(a) == ascii::to_upper(b); });
}

bool FoundWords::contains(std::string_view word) const noexcept {
    return seen_.contains(word);
}

std::optional<StartTileHelp> start_tile_help(std::span<const FoundWord> viable,
                                             const FoundWords& found) {
    const FoundWord* best = nullptr;
    for (const FoundWord& candidate : viable) {
        if (candidate.path.empty() || found.contains(candidate.word)) {
            continue;
        }
        if (best == nullptr || helps_before(candidate, *best)) {
            best = &candidate;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return StartTileHelp{.word = best->word, .tile = best->path.front()};
}

bool is_cleared(std::span<const FoundWord> viable, const FoundWords& found) {
    return std::ranges::all_of(
        viable, [&found](const FoundWord& word) { return found.contains(word.word); });
}

std::size_t words_starting_with(std::span<const FoundWord> viable, const FoundWords& found,
                                std::span<const Position> prefix) noexcept {
    if (prefix.empty()) {
        return 0;
    }
    std::size_t count = 0;
    for (const FoundWord& word : viable) {
        if (found.contains(word.word) || word.path.size() < prefix.size()) {
            continue;
        }
        const bool begins = std::equal(
            prefix.begin(), prefix.end(), word.path.begin(),
            [](Position a, Position b) { return a.row == b.row && a.column == b.column; });
        if (begins) {
            ++count;
        }
    }
    return count;
}

}  // namespace wjs
