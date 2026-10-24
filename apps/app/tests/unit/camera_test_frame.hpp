// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "scene.hpp"
#include "wjs/scan/frame.hpp"
#include "wjs/scan/image.hpp"

// A frame for the camera tests: one synthetic board from the scanner's corpus
// (libs/scan/tests/synth), copied into the owned frame a backend hands out. It is
// drawn from numbers and a seed, not from a camera, and it is the same picture
// the scanner's own tests read. Test support: nothing in libs or apps links it.

namespace wjs::app::test {

[[nodiscard]] inline wjs::scan::OwnedFrame synthetic_board_frame() {
    const wjs::scan::synth::Spec spec{wjs::scan::synth::Condition::upright,
                                      wjs::scan::synth::Split::test, 4, 0};
    const wjs::scan::synth::Rendered rendered = wjs::scan::synth::render(spec);

    wjs::scan::OwnedFrame frame;
    frame.width = static_cast<std::size_t>(rendered.image.width);
    frame.height = static_cast<std::size_t>(rendered.image.height);
    frame.stride = frame.width;
    frame.format = wjs::scan::PixelFormat::gray8;
    frame.pixels = rendered.image.pixels;
    return frame;
}

}  // namespace wjs::app::test
