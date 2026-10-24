// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/grapheme_binder.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>

#include "ascii.hpp"
#include "wjs/binder_work.hpp"
#include "wjs/profiling.hpp"

namespace wjs {
namespace {

using ascii::lower;
using ascii::to_lower;

}  // namespace

GraphemeBinder::GraphemeBinder(std::span<const Grapheme> graphemes) {
    for (const Grapheme& grapheme : graphemes) {
        // A split digraph cannot be found in flight: it is not known until its
        // second half is traced, and the design draws it from the word's split.
        if (grapheme.letters.size() > 1 && !grapheme.split()) {
            graphemes_.push_back(lower(grapheme.letters));
        }
    }
    std::ranges::sort(graphemes_, [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    graphemes_.erase(std::ranges::unique(graphemes_).begin(), graphemes_.end());
}

GraphemeBinder::GraphemeBinder(const PhaseIndex& index)
    : GraphemeBinder(std::span<const Grapheme>(index.graphemes())) {}

std::vector<GraphemeBinder::Block> GraphemeBinder::bind(std::span<const std::string> tiles) const {
    auto zone = profiling::zone<"phonics.bind">();

    // The letters of the whole path, and where each tile's letters start in it.
    // The lower-case letters are appended straight into `text`: lower() made a
    // string for every tile of every call, and bind() runs as the finger moves.
    std::string text;
    std::vector<std::size_t> starts;
    starts.reserve(tiles.size() + 1);
    for (const std::string& tile : tiles) {
        starts.push_back(text.size());
        for (const char letter : tile) {
            text.push_back(to_lower(letter));
        }
    }
    starts.push_back(text.size());

    std::vector<Block> blocks;
    std::uint64_t comparisons = 0;
    std::size_t at = 0;
    while (at < tiles.size()) {
        Block block;
        block.first = at;

        for (const std::string& grapheme : graphemes_) {
            ++comparisons;
            if (text.compare(starts[at], grapheme.size(), grapheme) != 0) {
                continue;
            }
            // It has to end where a tile does: a block is whole tiles.
            const std::size_t end = starts[at] + grapheme.size();
            const auto boundary = std::ranges::find(
                starts.begin() + static_cast<std::ptrdiff_t>(at) + 1, starts.end(), end);
            if (boundary == starts.end()) {
                continue;
            }
            const auto tile_count = static_cast<std::size_t>(boundary - starts.begin()) - at;
            if (tile_count > 1) {
                block.tiles = tile_count;
                block.grapheme = grapheme;
                break;
            }
        }

        blocks.push_back(std::move(block));
        at += blocks.back().tiles;
    }

    detail::note_bind(comparisons);
    zone.Value(comparisons);
    return blocks;
}

std::vector<GraphemeBinder::Block> GraphemeBinder::bind(const Board& board,
                                                        std::span<const Position> path) const {
    std::vector<std::string> tiles;
    tiles.reserve(path.size());
    for (const Position at : path) {
        tiles.push_back(board.tile(at));
    }
    return bind(tiles);
}

}  // namespace wjs
