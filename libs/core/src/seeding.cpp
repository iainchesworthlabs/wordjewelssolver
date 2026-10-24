// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/seeding.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <utility>

#include "ascii.hpp"
#include "draw.hpp"
#include "wjs/profiling.hpp"

namespace wjs {
namespace {

// A board is made around two or three words of the phase (phonics design,
// "Seeding", step 1).
constexpr std::size_t min_targets = 2;
constexpr std::size_t max_targets = 3;

// Search steps one planting may spend on one word before it gives the word up,
// so that a crowded board cannot stall. The count is of steps, not time, so it
// is the same on every machine.
constexpr std::size_t plant_step_budget = 20000;

using ascii::lower;
using ascii::to_upper;
using ascii::upper;

[[nodiscard]] constexpr bool is_letter(char c) noexcept {
    return c >= 'A' && c <= 'Z';
}

using detail::Draw;

// The tiles of a board, row by row. A tile that holds nothing yet is 0.
struct Grid {
    std::size_t size = 0;
    std::vector<char> cells;

    [[nodiscard]] std::size_t free_count() const {
        return static_cast<std::size_t>(std::ranges::count(cells, '\0'));
    }

    // The tiles that touch `cell`, in a fixed order.
    [[nodiscard]] std::size_t neighbours(std::size_t cell, std::array<std::size_t, 8>& out) const {
        const std::size_t row = cell / size;
        const std::size_t column = cell % size;
        std::size_t count = 0;
        for (std::size_t r = row > 0 ? row - 1 : 0; r <= row + 1 && r < size; ++r) {
            for (std::size_t c = column > 0 ? column - 1 : 0; c <= column + 1 && c < size; ++c) {
                if (r != row || c != column) {
                    out[count++] = r * size + c;
                }
            }
        }
        return count;
    }
};

// Writes a word into a grid as a path of touching tiles. A tile that already
// holds the word's letter there may be shared, and a free tile may be taken
// only while more than `reserve` free tiles remain, so the room the dead
// jewels need is left.
class Planter {
public:
    Planter(Grid& grid, Draw& draw, std::size_t reserve)
        : grid_(grid),
          draw_(draw),
          reserve_(reserve),
          free_(grid.free_count()),
          on_path_(grid.cells.size(), false) {}

    // Leaves the grid as it was and returns false if no path fits. With `apart`
    // the word may not newly touch a letter already planted (it may share one
    // where the letter agrees), so words planted together do not make words
    // between them.
    [[nodiscard]] bool plant(std::string_view word, bool apart = false) {
        word_ = word;
        apart_ = apart;
        steps_ = 0;
        std::vector<std::size_t> starts(grid_.cells.size());
        std::iota(starts.begin(), starts.end(), std::size_t{0});
        draw_.shuffle(std::span(starts));
        for (const std::size_t start : starts) {
            if (usable(start, 0) && walk(start, 0)) {
                return true;
            }
            if (steps_ > plant_step_budget) {
                break;
            }
        }
        return false;
    }

private:
    [[nodiscard]] bool usable(std::size_t cell, std::size_t at) const {
        if (on_path_[cell]) {
            return false;
        }
        const char held = grid_.cells[cell];
        if (held == '\0') {
            if (apart_ && touches_other_word(cell)) {
                return false;
            }
            return free_ > reserve_;
        }
        return held == word_[at];
    }

    // Whether a tile beside `cell` holds a letter that this word's path does not.
    [[nodiscard]] bool touches_other_word(std::size_t cell) const {
        std::array<std::size_t, 8> next{};
        const std::size_t count = grid_.neighbours(cell, next);
        for (std::size_t i = 0; i < count; ++i) {
            if (grid_.cells[next[i]] != '\0' && !on_path_[next[i]]) {
                return true;
            }
        }
        return false;
    }

    // Takes `cell` for letter `at` and carries on from it. On success the
    // letters stay in the grid; on failure everything this call did is undone.
    bool walk(std::size_t cell, std::size_t at) {
        ++steps_;
        const char previous = grid_.cells[cell];
        if (previous == '\0') {
            --free_;
        }
        grid_.cells[cell] = word_[at];
        on_path_[cell] = true;

        bool placed = at + 1 == word_.size();
        if (!placed && steps_ <= plant_step_budget) {
            std::array<std::size_t, 8> next{};
            const std::size_t count = grid_.neighbours(cell, next);
            draw_.shuffle(std::span(next.data(), count));
            for (std::size_t i = 0; i < count && !placed; ++i) {
                if (usable(next[i], at + 1)) {
                    placed = walk(next[i], at + 1);
                }
            }
        }

        on_path_[cell] = false;
        if (!placed) {
            grid_.cells[cell] = previous;
            if (previous == '\0') {
                ++free_;
            }
        }
        return placed;
    }

    Grid& grid_;
    Draw& draw_;
    std::size_t reserve_;
    std::size_t free_;
    std::vector<bool> on_path_;
    std::string word_;
    bool apart_ = false;
    std::size_t steps_ = 0;
};

// A word of the phase that fits the board.
struct Candidate {
    const PhaseWord* word = nullptr;
    std::string letters;  // upper-case
    bool focus = false;
};

// One board and what solving it said.
struct Attempt {
    std::vector<char> letters;                // row by row
    std::vector<std::size_t> filler;          // tiles that are neither planted nor dead
    std::vector<std::size_t> target_indices;  // into the candidates
    std::vector<Position> dead;
    std::vector<FoundWord> viable;
    std::size_t distance = 0;  // how far the count of words is outside the range
};

[[nodiscard]] std::string board_text(std::span<const char> letters, std::size_t size) {
    std::string text;
    text.reserve(letters.size() * 2);
    for (std::size_t row = 0; row < size; ++row) {
        if (row > 0) {
            text += '/';
        }
        for (std::size_t column = 0; column < size; ++column) {
            if (column > 0) {
                text += ' ';
            }
            text += letters[row * size + column];
        }
    }
    return text;
}

}  // namespace

void RecentTargets::record(const std::vector<std::string>& targets) {
    if (capacity_ == 0) {
        return;
    }
    boards_.push_back(targets);
    while (boards_.size() > capacity_) {
        boards_.pop_front();
    }
}

bool RecentTargets::contains(std::string_view word) const {
    return std::ranges::any_of(boards_, [&](const std::vector<std::string>& board) {
        return std::ranges::any_of(board, [&](const std::string& target) {
            return std::ranges::equal(target, word,
                                      [](char a, char b) { return to_upper(a) == to_upper(b); });
        });
    });
}

std::string_view to_string(SeedError error) noexcept {
    switch (error) {
        case SeedError::bad_size:
            return "the board is not between 1 and 16 tiles each way";
        case SeedError::bad_range:
            return "the range of viable words is empty or no tries are allowed";
        case SeedError::empty_index:
            return "no word of the phase fits the board";
        case SeedError::bad_dead_letter:
            return "the dead letter is not A-Z, or a word of the phase uses it";
        case SeedError::bad_filler:
            return "the filler is empty, not A-Z, or holds the dead letter";
    }
    return "unknown seeding error";
}

std::expected<SeedOptions, SeedError> phonics_board_options(const PhaseIndex& index,
                                                            std::size_t size) {
    const std::string& alphabet = index.alphabet();
    char dead_letter = '\0';
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if (!alphabet.contains(letter)) {
            dead_letter = letter;
            break;
        }
    }
    if (dead_letter == '\0') {
        // The phase uses every letter, so there is none to keep out of a word.
        return std::unexpected(SeedError::bad_dead_letter);
    }

    SeedOptions options;
    options.size = size;
    options.dead_cells = 0;
    options.dead_letter = dead_letter;
    options.guided_fill = true;
    options.max_target_letters = 3;
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if (letter != dead_letter) {
            options.filler += letter;
        }
    }
    return options;
}

std::expected<SeededBoard, SeedError> seed_board(const PhaseIndex& index,
                                                 const SeedOptions& options,
                                                 const RecentTargets& recent, std::uint32_t seed) {
    auto zone = profiling::zone<"board.seed">();

    // --- the options -----------------------------------------------------------
    if (options.size == 0 || options.size > Board::max_dimension) {
        return std::unexpected(SeedError::bad_size);
    }
    if (options.max_viable == 0 || options.min_viable > options.max_viable ||
        options.max_tries == 0) {
        return std::unexpected(SeedError::bad_range);
    }

    const std::string& alphabet = index.alphabet();
    const char dead_letter = to_upper(options.dead_letter);
    // A guided fill may make a tile dead when no letter fits, so it needs the
    // dead letter whether or not dead jewels were asked for.
    const bool needs_dead_letter = options.dead_cells > 0 || options.guided_fill;
    if (needs_dead_letter) {
        if (!is_letter(dead_letter) || alphabet.contains(dead_letter)) {
            return std::unexpected(SeedError::bad_dead_letter);
        }
    }

    const std::string filler = options.filler.empty() ? alphabet : upper(options.filler);
    if (filler.empty() || !std::ranges::all_of(filler, is_letter) ||
        (needs_dead_letter && filler.contains(dead_letter))) {
        return std::unexpected(SeedError::bad_filler);
    }

    // --- the words that can be planted -----------------------------------------
    const std::size_t cell_count = options.size * options.size;

    // A focus sound is ticked by the letters of its grapheme, in lower case as
    // Settings::normalized writes them: that is what the parent's list shows
    // (focus_sound_choices) and what the settings hold. All the sounds a
    // grapheme has are ticked together, as they are on the dashboard.
    std::vector<std::string> focus_letters;
    focus_letters.reserve(options.focus_sounds.size());
    for (const std::string& tick : options.focus_sounds) {
        focus_letters.push_back(lower(tick));
    }

    std::vector<Candidate> candidates;
    std::size_t shortest = cell_count;
    for (const PhaseWord& word : index.words()) {
        if (word.word.size() > cell_count ||
            (options.max_target_letters > 0 && word.word.size() > options.max_target_letters)) {
            continue;
        }
        Candidate candidate;
        candidate.word = &word;
        candidate.letters = upper(word.word);
        candidate.focus = std::ranges::any_of(word.graphemes, [&](const WordGrapheme& grapheme) {
            return std::ranges::find(focus_letters, grapheme.letters) != focus_letters.end();
        });
        shortest = std::min(shortest, word.word.size());
        candidates.push_back(std::move(candidate));
    }
    if (candidates.empty()) {
        return std::unexpected(SeedError::empty_index);
    }

    // No more dead jewels than leave room for the shortest word.
    const std::size_t dead_target = std::min(options.dead_cells, cell_count - shortest);

    SolveOptions solve_options;
    solve_options.min_length = min_phase_word_letters;

    Draw draw(seed);
    std::size_t tries = 0;
    std::size_t solves = 0;
    std::optional<Attempt> best;

    // The words of the phase on a board of these letters.
    const auto count_words = [&](std::span<const char> letters) -> std::size_t {
        const auto parsed = Board::parse(board_text(letters, options.size));
        ++solves;
        return parsed ? solve(*parsed, index.dictionary(), solve_options).size() : 0;
    };

    // The letters a guided fill chooses between, each once.
    std::string distinct = filler;
    std::ranges::sort(distinct);
    distinct.erase(std::ranges::unique(distinct).begin(), distinct.end());

    const auto verify = [&](Attempt& attempt) -> bool {
        auto verify_zone = profiling::zone<"board.verify">();
        const auto board = Board::parse(board_text(attempt.letters, options.size));
        // The letters are A-Z in a square of at most 16: it parses.
        attempt.viable =
            board ? solve(*board, index.dictionary(), solve_options) : std::vector<FoundWord>{};
        ++tries;
        ++solves;

        const std::size_t count = attempt.viable.size();
        verify_zone.Value(count);
        std::size_t distance = 0;
        if (count < options.min_viable) {
            distance = options.min_viable - count;
        } else if (count > options.max_viable) {
            distance = count - options.max_viable;
        }
        attempt.distance = distance;
        // A board that lost a target is worse than any that kept them all.
        const bool kept = std::ranges::all_of(attempt.target_indices, [&](std::size_t target) {
            return std::ranges::any_of(attempt.viable, [&](const FoundWord& found) {
                return found.word == candidates[target].letters;
            });
        });
        if (!kept) {
            attempt.distance += cell_count * cell_count;
        }

        if (!best || attempt.distance < best->distance) {
            best = attempt;
        }
        return attempt.distance == 0;
    };

    // --- make boards until one lands in range, or the tries are spent -----------
    bool accepted = false;
    while (!accepted && tries < options.max_tries) {
        // Choose and plant the targets. The pools, in the order they are drawn
        // from: words with a focus sound that are not recent, those that are,
        // then the same two for words with none.
        std::array<std::vector<std::size_t>, 4> pools;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const bool is_recent = recent.contains(candidates[i].word->word);
            const std::size_t which = (candidates[i].focus ? 0U : 2U) + (is_recent ? 1U : 0U);
            pools[which].push_back(i);
        }

        Grid grid;
        grid.size = options.size;
        grid.cells.assign(cell_count, '\0');
        Planter planter(grid, draw, dead_target);

        // A guided fill only ever adds words, so enough are planted at the
        // start that the count cannot fall short.
        const std::size_t base = min_targets + draw.below(max_targets - min_targets + 1);
        const std::size_t wanted =
            std::min({options.guided_fill ? std::max(base, options.min_viable) : base,
                      options.max_viable, candidates.size()});

        Attempt attempt;
        while (attempt.target_indices.size() < wanted) {
            // Once the focus is being honoured it stays honoured: a third
            // word is taken from the focus pools or not at all. The first two
            // are taken from any pool, in order.
            const bool focus_only =
                !options.focus_sounds.empty() && attempt.target_indices.size() >= min_targets &&
                std::ranges::all_of(attempt.target_indices,
                                    [&](std::size_t target) { return candidates[target].focus; });
            // libstdc++'s std::array iterator is a pointer and MSVC's is a
            // class; auto is the portable spelling.
            // NOLINTNEXTLINE(readability-qualified-auto)
            const auto last = pools.begin() + (focus_only ? 2 : 4);
            // NOLINTNEXTLINE(readability-qualified-auto)
            auto from = std::find_if(pools.begin(), last, [](const auto& p) { return !p.empty(); });
            if (from == last) {
                break;
            }
            const std::size_t at = draw.below(from->size());
            const std::size_t chosen = (*from)[at];
            from->erase(from->begin() + static_cast<std::ptrdiff_t>(at));
            // A guided fill cannot take words away, so the planting is kept apart
            // where it can be, and made to touch only if there is no other way.
            const bool planted =
                (options.guided_fill && planter.plant(candidates[chosen].letters, true)) ||
                planter.plant(candidates[chosen].letters);
            if (planted) {
                attempt.target_indices.push_back(chosen);
            }
        }
        if (attempt.target_indices.empty()) {
            // Cannot happen on an empty grid with a word that fits, but a board
            // with nothing planted is not one to offer.
            break;
        }

        // Dead jewels among the tiles the words left, then the filler.
        std::vector<std::size_t> open;
        for (std::size_t i = 0; i < cell_count; ++i) {
            if (grid.cells[i] == '\0') {
                open.push_back(i);
            }
        }
        draw.shuffle(std::span(open));
        const std::size_t dead_count = std::min(dead_target, open.size());
        std::vector<std::size_t> dead_cells(open.begin(),
                                            open.begin() + static_cast<std::ptrdiff_t>(dead_count));
        std::ranges::sort(dead_cells);
        attempt.filler.assign(open.begin() + static_cast<std::ptrdiff_t>(dead_count), open.end());
        std::ranges::sort(attempt.filler);

        for (const std::size_t cell : dead_cells) {
            grid.cells[cell] = dead_letter;
            attempt.dead.push_back(Position{cell / options.size, cell % options.size});
        }
        const auto in_row_order = [](const Position& a, const Position& b) {
            return a.row != b.row ? a.row < b.row : a.column < b.column;
        };
        if (options.guided_fill) {
            // Every tile starts as a dead jewel, so the count is that of the words
            // planted, and each is given, in a random order, a random letter from
            // those that keep the count within a ceiling, or stays dead.
            std::vector<std::size_t> order = attempt.filler;
            draw.shuffle(std::span(order));
            for (const std::size_t cell : order) {
                grid.cells[cell] = dead_letter;
            }
            // The words planted may hold too many words between themselves, and
            // no fill can take words away. That planting is given up on the cost
            // of one solve, not of a fill.
            const std::size_t baseline = count_words(grid.cells);
            if (baseline > options.max_viable) {
                for (const std::size_t cell : order) {
                    attempt.dead.push_back(Position{cell / options.size, cell % options.size});
                }
                std::ranges::sort(attempt.dead, in_row_order);
                attempt.letters = grid.cells;
                accepted = verify(attempt);
                continue;
            }
            // Fill up to a count chosen between the words already there and the most
            // wanted, so the boards are not all at the top of the range.
            const std::size_t floor = std::max(baseline, options.min_viable);
            const std::size_t ceiling = floor + draw.below(options.max_viable - floor + 1);
            for (const std::size_t cell : order) {
                std::vector<char> fits;
                for (const char letter : distinct) {
                    grid.cells[cell] = letter;
                    if (count_words(grid.cells) <= ceiling) {
                        fits.push_back(letter);
                    }
                }
                // A letter of the phase is preferred; one it does not use (which
                // adds no word, so always fits) only when no letter of the phase
                // does, and a dead jewel only if there is none of those either.
                std::vector<char> taught;
                for (const char letter : fits) {
                    if (alphabet.contains(letter)) {
                        taught.push_back(letter);
                    }
                }
                const std::vector<char>& chosen_from = taught.empty() ? fits : taught;
                if (chosen_from.empty()) {
                    grid.cells[cell] = dead_letter;
                    attempt.dead.push_back(Position{cell / options.size, cell % options.size});
                } else {
                    grid.cells[cell] = chosen_from[draw.below(chosen_from.size())];
                }
            }
            std::ranges::sort(attempt.dead, in_row_order);
        } else {
            for (const std::size_t cell : attempt.filler) {
                grid.cells[cell] = filler[draw.below(filler.size())];
            }
        }
        attempt.letters = grid.cells;

        accepted = verify(attempt);

        // Then change one filler tile at a time, which moves the count by a
        // word or two and costs one solve, before starting over.
        for (std::size_t change = 0;
             !accepted && !options.guided_fill && change < options.mutations_per_plant &&
             tries < options.max_tries && !attempt.filler.empty();
             ++change) {
            const std::size_t cell = attempt.filler[draw.below(attempt.filler.size())];
            char letter = filler[draw.below(filler.size())];
            for (std::size_t retry = 0; retry < 8 && letter == attempt.letters[cell]; ++retry) {
                letter = filler[draw.below(filler.size())];
            }
            attempt.letters[cell] = letter;
            accepted = verify(attempt);
        }
    }

    if (!best) {
        return std::unexpected(SeedError::empty_index);
    }

    auto board = Board::parse(board_text(best->letters, options.size));
    if (!board) {
        return std::unexpected(SeedError::bad_size);
    }

    SeededBoard result{*std::move(board),
                       {},
                       std::move(best->viable),
                       std::move(best->dead),
                       tries,
                       false,
                       false,
                       solves};
    std::size_t with_focus = 0;
    for (const std::size_t target : best->target_indices) {
        result.targets.push_back(candidates[target].word->word);
        if (candidates[target].focus) {
            ++with_focus;
        }
    }
    result.fallback = best->distance != 0;
    result.focus_unmet = !options.focus_sounds.empty() && with_focus < result.targets.size();
    zone.Value(tries);
    return result;
}

}  // namespace wjs
