// SPDX-License-Identifier: GPL-3.0-or-later
#include "measure.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "corpus.hpp"
#include "perf_build.hpp"
#include "wjs/binder_work.hpp"
#include "wjs/game_session.hpp"
#include "wjs/solver_work.hpp"

namespace wjs::perf {

// ---------------------------------------------------------------------------
// The dictionary
// ---------------------------------------------------------------------------

const wjs::Dictionary& english() {
    static const wjs::Dictionary dictionary = [] {
        auto loaded = wjs::Dictionary::load_file(std::string(kDictionaryPath));
        // Nothing in this tier can say anything without the word list, and a
        // case that dereferenced an empty result would only say it worse.
        if (!loaded) {
            std::abort();
        }
        return *std::move(loaded);
    }();
    return dictionary;
}

std::uint64_t trie_nodes(const wjs::Dictionary& dictionary) {
    std::uint64_t nodes = 0;
    std::vector<wjs::Dictionary::Cursor> pending{wjs::Dictionary::root};
    while (!pending.empty()) {
        const wjs::Dictionary::Cursor at = pending.back();
        pending.pop_back();
        ++nodes;
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            const wjs::Dictionary::Cursor next = dictionary.step(at, letter);
            if (next != wjs::Dictionary::no_node) {
                pending.push_back(next);
            }
        }
    }
    return nodes;
}

// ---------------------------------------------------------------------------
// Solver work
// ---------------------------------------------------------------------------

SolveWork measure_solve(const wjs::Board& board, const wjs::Dictionary& dictionary) {
    wjs::reset_solve_work();
    const auto found = wjs::solve(board, dictionary);
    return {.visits = wjs::solve_work().visits, .words = found.size()};
}

namespace {

// The walk, as the definition of a visit reads: a tile is visited when the
// letters stepped so far, with its own, are still the start of some word.
class Recount {
public:
    Recount(const wjs::Board& board, const wjs::Dictionary& dictionary)
        : board_(board), dictionary_(dictionary), used_(board.rows() * board.columns(), false) {}

    std::uint64_t run() {
        for (std::size_t row = 0; row < board_.rows(); ++row) {
            for (std::size_t column = 0; column < board_.columns(); ++column) {
                visit(row, column, wjs::Dictionary::root);
            }
        }
        return visits_;
    }

private:
    void visit(std::size_t row, std::size_t column, wjs::Dictionary::Cursor from) {
        wjs::Dictionary::Cursor cursor = from;
        for (const char letter : board_.tile({row, column})) {
            cursor = dictionary_.step(cursor, letter);
            if (cursor == wjs::Dictionary::no_node) {
                return;
            }
        }
        ++visits_;
        used_[row * board_.columns() + column] = true;
        for (std::size_t next_row = row == 0 ? 0 : row - 1; next_row <= row + 1; ++next_row) {
            for (std::size_t next_column = column == 0 ? 0 : column - 1; next_column <= column + 1;
                 ++next_column) {
                const bool same = next_row == row && next_column == column;
                if (same || !board_.contains({next_row, next_column}) ||
                    used_[next_row * board_.columns() + next_column]) {
                    continue;
                }
                visit(next_row, next_column, cursor);
            }
        }
        used_[row * board_.columns() + column] = false;
    }

    const wjs::Board& board_;
    const wjs::Dictionary& dictionary_;
    std::vector<bool> used_;
    std::uint64_t visits_ = 0;
};

}  // namespace

std::uint64_t recount_visits(const wjs::Board& board, const wjs::Dictionary& dictionary) {
    return Recount(board, dictionary).run();
}

// ---------------------------------------------------------------------------
// Seeding work
// ---------------------------------------------------------------------------

SeedRun measure_seed(const wjs::PhaseIndex& index, std::size_t size, std::uint32_t seed) {
    const auto options = wjs::phonics_board_options(index, size);
    // A phase that cannot make the options, or a seeding that is refused, is a
    // mistake in the corpus; there is nothing to recover to.
    if (!options) {
        std::abort();
    }
    wjs::reset_solve_work();
    auto seeded = wjs::seed_board(index, *options, wjs::RecentTargets{}, seed);
    if (!seeded) {
        std::abort();
    }
    const auto work = wjs::solve_work();
    return {.board = *std::move(seeded), .visits = work.visits, .solver_solves = work.solves};
}

// ---------------------------------------------------------------------------
// Session work
// ---------------------------------------------------------------------------

namespace {

// One board of a session, dealt and played through: the deal, every word of the
// board traced (with the help asked for before every other one), a word again, a
// touch, the snapshots, and the restore. `active` is the word list that counts
// on the board: the phase's index on the phonics board, the dictionary on the
// standard. The process ends if the session cannot be made or refuses the deal.
SessionRun run_session(const wjs::SessionData& data, const wjs::SessionOptions& options,
                       const wjs::Dictionary& active) {
    auto created = wjs::GameSession::create(data, options);
    if (!created) {
        std::abort();
    }
    wjs::GameSession session = *std::move(created);

    SessionRun run;
    run.mode = options.mode;

    // The deal: the shake and nothing else.
    wjs::reset_solve_work();
    if (!session.shake()) {
        std::abort();
    }
    run.deal_visits = wjs::solve_work().visits;
    run.deal_solves = wjs::solve_work().solves;
    run.deal = *session.last_deal();

    // The words and their paths come from the solver, outside the counted part:
    // the play is what the screens do, and they do not solve.
    if (!session.still() || !session.landed()) {
        std::abort();
    }
    const std::vector<wjs::FoundWord> words = wjs::solve(*session.board(), active);
    run.words = words.size();

    wjs::reset_solve_work();
    wjs::Stamp when = 0;
    for (std::size_t which = 0; which < words.size(); ++which) {
        if (which % 2 == 1) {
            static_cast<void>(session.request_help());
        }
        run.kinds.push_back(session.trace(words[which].path, ++when).kind);
        if (which == 0) {
            // A word again, and a touch of one tile: neither is a find.
            run.kinds.push_back(session.trace(words[0].path, ++when).kind);
            const std::vector<wjs::Position> touch{words[0].path.front()};
            run.kinds.push_back(session.trace(touch, ++when).kind);
            run.after_first_find = session.snapshot();
            (void)session.record();
        }
    }
    if (session.state() != wjs::SessionState::cleared || !session.rest()) {
        std::abort();
    }
    run.at_rest = session.snapshot();
    run.play_visits = wjs::solve_work().visits;
    run.play_solves = wjs::solve_work().solves;

    // Bringing the board back: one solve.
    wjs::reset_solve_work();
    const auto restored = wjs::GameSession::restore(run.after_first_find, data, options);
    if (!restored) {
        std::abort();
    }
    run.restore_visits = wjs::solve_work().visits;
    run.restore_solves = wjs::solve_work().solves;
    return run;
}

[[nodiscard]] wjs::SessionOptions options_of(wjs::BoardMode mode, std::size_t size,
                                             std::uint32_t seed) {
    const auto board_size = wjs::board_size_for(size);
    if (!board_size) {
        std::abort();
    }
    wjs::SessionOptions options;
    options.mode = mode;
    options.size = *board_size;
    options.seed = seed;
    return options;
}

}  // namespace

SessionRun measure_session(const wjs::PhaseIndex& index, std::size_t size, std::uint32_t seed) {
    return run_session({.dictionary = english(), .phase = &index},
                       options_of(wjs::BoardMode::phonics, size, seed), index.dictionary());
}

SessionRun measure_standard_session(std::size_t size, std::uint32_t seed) {
    return run_session({.dictionary = english(), .dice = &dice_sets()},
                       options_of(wjs::BoardMode::standard, size, seed), english());
}

std::size_t measure_session_deal(const wjs::PhaseIndex& index, std::size_t size,
                                 std::uint32_t seed) {
    auto created = wjs::GameSession::create({.dictionary = english(), .phase = &index},
                                            options_of(wjs::BoardMode::phonics, size, seed));
    if (!created || !created->shake()) {
        std::abort();
    }
    return created->last_deal()->viable;
}

std::size_t measure_standard_session_deal(std::size_t size, std::uint32_t seed) {
    auto created = wjs::GameSession::create({.dictionary = english(), .dice = &dice_sets()},
                                            options_of(wjs::BoardMode::standard, size, seed));
    if (!created || !created->shake()) {
        std::abort();
    }
    return created->last_deal()->viable;
}

// ---------------------------------------------------------------------------
// Binder work
// ---------------------------------------------------------------------------

BindRun measure_bind(const wjs::GraphemeBinder& binder, std::span<const std::string> tiles) {
    wjs::reset_bind_work();
    BindRun run;
    run.blocks = binder.bind(tiles);
    run.comparisons = wjs::bind_work().comparisons;
    return run;
}

std::uint64_t recount_comparisons(std::span<const wjs::Grapheme> graphemes,
                                  std::span<const wjs::GraphemeBinder::Block> blocks) {
    // What the binder holds, from its own description (grapheme_binder.hpp):
    // the multi-letter graphemes that are not split, longest first, then in
    // letter order, each once. The letters of a Grapheme are lower-case as
    // written.
    std::vector<std::string> held;
    for (const wjs::Grapheme& grapheme : graphemes) {
        if (grapheme.letters.size() > 1 && !grapheme.split()) {
            held.push_back(grapheme.letters);
        }
    }
    std::ranges::sort(held, [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    held.erase(std::ranges::unique(held).begin(), held.end());

    std::uint64_t comparisons = 0;
    for (const wjs::GraphemeBinder::Block& block : blocks) {
        if (!block.linked()) {
            comparisons += held.size();
            continue;
        }
        const auto at = std::ranges::find(held, block.grapheme);
        comparisons += static_cast<std::uint64_t>(at - held.begin()) + 1;
    }
    return comparisons;
}

void Digest::byte(std::uint8_t value) noexcept {
    state_ ^= value;
    state_ *= 1099511628211ULL;
}

void Digest::number(std::uint64_t value) noexcept {
    // Eight bytes, low first: the same on every machine whatever its byte order.
    for (int shift = 0; shift < 64; shift += 8) {
        byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void Digest::text(const std::string& value) {
    for (const char letter : value) {
        byte(static_cast<std::uint8_t>(letter));
    }
    byte(0);
}

void Digest::add(const std::vector<wjs::FoundWord>& found) {
    number(found.size());
    for (const wjs::FoundWord& word : found) {
        text(word.word);
        number(static_cast<std::uint64_t>(word.score));
        number(word.path.size());
        for (const wjs::Position position : word.path) {
            number(position.row);
            number(position.column);
        }
    }
}

void Digest::add(const wjs::SeededBoard& seeded) {
    number(seeded.board.rows());
    number(seeded.board.columns());
    for (std::size_t row = 0; row < seeded.board.rows(); ++row) {
        for (std::size_t column = 0; column < seeded.board.columns(); ++column) {
            text(seeded.board.tile({row, column}));
        }
    }
    number(seeded.targets.size());
    for (const std::string& target : seeded.targets) {
        text(target);
    }
    number(seeded.dead.size());
    for (const wjs::Position position : seeded.dead) {
        number(position.row);
        number(position.column);
    }
    number(seeded.tries);
    number(seeded.solves);
    number(seeded.fallback ? 1U : 0U);
    add(seeded.viable);
}

void Digest::add(const wjs::SessionSnapshot& snapshot) {
    number(static_cast<std::uint64_t>(snapshot.state));
    number(wjs::side(snapshot.size));
    number(snapshot.seed);
    number(snapshot.board ? 1U : 0U);
    if (snapshot.board) {
        const wjs::BoardRecord& record = *snapshot.board;
        text(record.phase);
        text(record.tiles);
        number(record.viable.size());
        for (const std::string& word : record.viable) {
            text(word);
        }
        number(record.finds.size());
        for (const wjs::Find& find : record.finds) {
            text(find.word);
            number(find.assisted ? 1U : 0U);
            number(static_cast<std::uint64_t>(find.when));
        }
        // What only the standard board's record says. Added for it alone, so that
        // the phonics digest is what it was before the mode existed.
        if (record.mode == wjs::BoardMode::standard) {
            number(record.viable_total);
        }
    }
    number(snapshot.lit ? 1U : 0U);
    if (snapshot.lit) {
        text(*snapshot.lit);
    }
}

void Digest::add(const SessionRun& run) {
    if (run.mode == wjs::BoardMode::standard) {
        number(run.deal.rolls);  // the standard deal's own count, in the place of the seedings
    }
    number(run.deal.seedings);
    number(run.deal.tries);
    number(run.deal.solves);
    number(run.deal.viable);
    number(run.deal.in_range ? 1U : 0U);
    number(run.deal.seed);
    number(run.words);
    number(run.kinds.size());
    for (const wjs::TraceKind kind : run.kinds) {
        number(static_cast<std::uint64_t>(kind));
    }
    add(run.after_first_find);
    add(run.at_rest);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

bool optimised_build() {
    return kOptimised;
}

bool timing_meaningful() {
    return kTimingMeaningful;
}

bool timing_requested() {
    const char* value = std::getenv("WJS_PERF_TIMING");
    if (value == nullptr) {
        return false;
    }
    const std::string_view text = value;
    return !text.empty() && text != "0";
}

std::uint64_t calibration_workload(Weight weight) {
    // A xorshift stream feeds a hash set (inserts of mostly new keys, the shape
    // of a trie's growth) and an arena of 48-byte records appended and read
    // back (its nodes' shape). Light is about ten milliseconds and a few
    // megabytes, which a cache holds; heavy is about ten times that and tens
    // of megabytes, which loading a dictionary's trie is, and which a cache
    // does not.
    const std::size_t steps = weight == Weight::light ? 100'000 : 400'000;
    struct Record {
        std::array<std::uint8_t, 48> bytes;
    };

    std::uint64_t x = 0x9E3779B97F4A7C15ULL;
    std::uint64_t sum = 0;
    std::unordered_set<std::uint64_t> seen;
    std::vector<Record> arena;
    for (std::size_t i = 0; i < steps; ++i) {
        x ^= x << 13U;
        x ^= x >> 7U;
        x ^= x << 17U;
        const std::uint64_t key = x & 0xFFFFFFFULL;
        if (seen.insert(key).second) {
            Record record{};
            record.bytes.fill(static_cast<std::uint8_t>(key));
            arena.push_back(record);
        }
        const Record& back = arena.at((x >> 20U) % arena.size());
        sum += back.bytes.at((x >> 40U) % back.bytes.size());
    }
    return sum + seen.size() + arena.size();
}

namespace detail {

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values.at(middle);
    }
    return (values.at(middle - 1) + values.at(middle)) / 2.0;
}

}  // namespace detail

}  // namespace wjs::perf
