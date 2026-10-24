// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Frames drawn once per test run and shared by the cases that want one: drawing a
// frame is most of what a scanner test costs, and in Debug with the sanitizers it
// is a good deal.

#include <expected>
#include <map>

#include "scene.hpp"
#include "wjs/scan/reader.hpp"

namespace wjs::scan::fixtures {

// The frame for a spec, drawn on first use.
[[nodiscard]] inline const synth::Rendered& frame(const synth::Spec& spec) {
    static std::map<unsigned, synth::Rendered> drawn;
    const unsigned seed = synth::seed_of(spec);
    auto found = drawn.find(seed);
    if (found == drawn.end()) {
        found = drawn.emplace(seed, synth::render(spec)).first;
    }
    return found->second;
}

// A frame of the tuning split: a condition, a size, and which of its boards.
[[nodiscard]] inline const synth::Rendered& tuning_frame(synth::Condition condition,
                                                         std::size_t size, unsigned index) {
    synth::Spec spec;
    spec.condition = condition;
    spec.split = synth::Split::tuning;
    spec.size = size;
    spec.index = index;
    return frame(spec);
}

// The reader's answer for a frame, with the default options, got on first use.
[[nodiscard]] inline const std::expected<BoardReading, ReadError>& reading_of(
    const synth::Spec& spec) {
    static std::map<unsigned, std::expected<BoardReading, ReadError>> read;
    const unsigned seed = synth::seed_of(spec);
    auto found = read.find(seed);
    if (found == read.end()) {
        found = read.emplace(seed, read_board(frame(spec).image.view())).first;
    }
    return found->second;
}

}  // namespace wjs::scan::fixtures
