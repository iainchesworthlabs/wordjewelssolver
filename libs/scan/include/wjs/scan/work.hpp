// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace wjs::scan {

// What the reader has done so far on this thread, in counts that do not depend
// on the machine, the compiler or the build type: pixels looked at, die
// candidates found, templates compared, pixel buffers allocated. The same idea
// as wjs/solver_work.hpp, for the same reason: a change that reads every cell at
// four times the angles, or allocates a buffer for each row, multiplies a count
// long before a stopwatch notices, and a count names what grew.
//
// A read adds its figures as it goes (an add a stage, nothing a pixel), the
// performance tests (libs/core/bench, docs/testing.md "Performance tests") reset it,
// make a call and read it, and nothing in the application does.
struct ScanWork {
    // Calls to read_board that got as far as the pixels.
    std::uint64_t reads = 0;
    // Pixels of the frames as they were given.
    std::uint64_t frame_pixels = 0;
    // Pixels of the working image the board was looked for in.
    std::uint64_t working_pixels = 0;
    // Pixels sampled to warp the board to a square.
    std::uint64_t warp_pixels = 0;
    // Bright blobs found, and those among them that were shaped like a die.
    std::uint64_t blobs = 0;
    std::uint64_t dice_candidates = 0;
    // Cells whose glyph was read, and the patch-and-template correlations that
    // took.
    std::uint64_t cells_read = 0;
    std::uint64_t comparisons = 0;
    // Pixel buffers allocated, in all; and the most that any one read allocated.
    std::uint64_t buffers = 0;
    std::uint64_t largest_buffers = 0;
    // Pixel buffers handed out of the reader's scratch pool without an
    // allocation (libs/scan/src/scratch.hpp). A read's acquisitions are
    // buffers + buffer_reuses, so a warm read allocates nothing and its reuses
    // are the whole set.
    std::uint64_t buffer_reuses = 0;
};

namespace detail {

// One tally per thread. A function's static and not a namespace-scope variable:
// the same thing, without being a non-const global that anything can reach.
[[nodiscard]] inline ScanWork& scan_tally() noexcept {
    static thread_local ScanWork work;
    return work;
}

inline void note_buffer() noexcept {
    ++scan_tally().buffers;
}

}  // namespace detail

// This thread's running total since the last reset.
[[nodiscard]] inline ScanWork scan_work() noexcept {
    return detail::scan_tally();
}

// Starts this thread's total from zero: reset, make the call, read.
inline void reset_scan_work() noexcept {
    detail::scan_tally() = {};
}

}  // namespace wjs::scan
