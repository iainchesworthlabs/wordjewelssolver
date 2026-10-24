// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/solver.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

#include "wjs/profiling.hpp"
#include "wjs/solver_work.hpp"

namespace wjs {
namespace {

struct Offset {
    std::ptrdiff_t row;
    std::ptrdiff_t column;
};

constexpr std::array<Offset, 8> neighbours = {{
    {-1, -1},
    {-1, 0},
    {-1, 1},
    {0, -1},
    {0, 1},
    {1, -1},
    {1, 0},
    {1, 1},
}};

// The most tiles a board can hold (Board::max_dimension each way).
constexpr std::size_t max_tiles = Board::max_dimension * Board::max_dimension;

// A depth-first walk of the board that carries a position in the dictionary's
// trie alongside the path, so a branch that no word can continue is dropped as
// soon as it is known, rather than after the letters have been collected.
//
// The hot path (extend, one call a visit) allocates nothing: the visit bitmask,
// the path and the neighbour lists are fixed arrays sized to the largest board,
// and the per-tile letter lookup is a pointer table built once a solve. Before,
// a visit appended to two vectors (a reallocation on growth) and indexed a
// std::vector<bool>, and the neighbour loop did per-visit row/column
// arithmetic with bounds checks; the tables move that work to the constructor,
// which runs once per solve instead of once per visit. The walk itself is the
// same: neighbours are tried in the same order, so the visits, the words and
// the first path of each word are exactly what they were (the performance
// tier's digests and visit budgets pin that).
class Search {
public:
    Search(const Board& board, const Dictionary& dictionary, const SolveOptions& options)
        : board_(board), dictionary_(dictionary), options_(options) {
        const std::size_t cells = board_.rows() * board_.columns();
        for (std::size_t index = 0; index < cells; ++index) {
            tiles_[index] = &board_.tile({index / board_.columns(), index % board_.columns()});
            std::size_t count = 0;
            for (const Offset offset : neighbours) {
                const auto row = static_cast<std::ptrdiff_t>(index / board_.columns()) + offset.row;
                const auto column =
                    static_cast<std::ptrdiff_t>(index % board_.columns()) + offset.column;
                if (row < 0 || column < 0) {
                    continue;
                }
                const auto next_row = static_cast<std::size_t>(row);
                const auto next_column = static_cast<std::size_t>(column);
                if (next_row >= board_.rows() || next_column >= board_.columns()) {
                    continue;
                }
                neighbours_[index * 8 + count] =
                    static_cast<std::uint16_t>(next_row * board_.columns() + next_column);
                ++count;
            }
            neighbour_count_[index] = static_cast<std::uint8_t>(count);
        }
    }

    // Tile steps that stayed inside the trie, after run(): the figure
    // solver_work.hpp tallies.
    [[nodiscard]] std::uint64_t visits() const noexcept { return visits_; }

    [[nodiscard]] std::vector<FoundWord> run() {
        for (std::size_t row = 0; row < board_.rows(); ++row) {
            for (std::size_t column = 0; column < board_.columns(); ++column) {
                const auto index = static_cast<std::uint16_t>(row * board_.columns() + column);
                extend(index, Dictionary::root);
            }
        }

        std::vector<FoundWord> result;
        result.reserve(found_.size());
        for (auto& entry : found_) {
            result.push_back(std::move(entry.second));
        }
        std::ranges::sort(result, [](const FoundWord& lhs, const FoundWord& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            if (lhs.word.size() != rhs.word.size()) {
                return lhs.word.size() > rhs.word.size();
            }
            return lhs.word < rhs.word;
        });
        return result;
    }

private:
    void extend(std::uint16_t index, Dictionary::Cursor from) {
        const std::string& tile = *tiles_[index];

        Dictionary::Cursor cursor = from;
        for (const char letter : tile) {
            cursor = dictionary_.step(cursor, letter);
            if (cursor == Dictionary::no_node) {
                return;
            }
        }

        ++visits_;

        visited_.set(index);
        word_ += tile;
        path_[depth_] = index;
        ++depth_;

        if (dictionary_.is_word(cursor) && word_.size() >= options_.min_length) {
            record();
        }

        for (std::size_t i = 0; i < neighbour_count_[index]; ++i) {
            const std::uint16_t next = neighbours_[static_cast<std::size_t>(index) * 8 + i];
            if (!visited_.test(next)) {
                extend(next, cursor);
            }
        }

        --depth_;
        word_.resize(word_.size() - tile.size());
        visited_.reset(index);
    }

    void record() {
        if (found_.contains(word_)) {
            return;
        }
        std::vector<Position> path;
        path.reserve(depth_);
        for (std::size_t i = 0; i < depth_; ++i) {
            path.emplace_back(path_[i] / board_.columns(), path_[i] % board_.columns());
        }
        found_.emplace(word_,
                       FoundWord{word_, std::move(path), score_word(word_, options_.scoring)});
    }

    const Board& board_;
    const Dictionary& dictionary_;
    const SolveOptions& options_;

    std::uint64_t visits_ = 0;
    std::bitset<max_tiles> visited_;
    std::string word_;
    std::uint16_t depth_ = 0;
    std::array<std::uint16_t, max_tiles> path_{};
    std::array<std::uint16_t, max_tiles * 8> neighbours_{};
    std::array<std::uint8_t, max_tiles> neighbour_count_{};
    std::array<const std::string*, max_tiles> tiles_{};
    std::unordered_map<std::string, FoundWord> found_;
};

}  // namespace

std::vector<FoundWord> solve(const Board& board, const Dictionary& dictionary,
                             const SolveOptions& options) {
    auto zone = profiling::zone<"solver.solve">();

    Search search(board, dictionary, options);
    std::vector<FoundWord> found = search.run();

    detail::note_solve(search.visits());
    zone.Value(found.size());
    profiling::plot("solver.visits", search.visits());
    return found;
}

}  // namespace wjs
