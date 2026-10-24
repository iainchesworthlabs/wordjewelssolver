// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace wjs {

// What the grapheme binder's calls have cost so far on this thread, counted in
// comparisons: the times a grapheme was held against the letters at the start
// of a block. A tile that starts no grapheme costs one comparison for every
// grapheme the binder holds; a tile that starts one costs as many as come
// before it in the binder's order (longest first), and one more for itself. It
// is the one figure that does not depend on the machine, the compiler or the
// build type, as the solver's visits are (solver_work.hpp).
//
// A bind counts its comparisons in a local and writes them here once, as it
// ends, so the thread-local costs one add per bind and nothing per comparison.
// The performance tests (libs/core/bench, docs/testing.md "Performance tests") reset
// it, make a call and read it; nothing in the application does.
struct BindWork {
    // Calls to bind() on this thread since the last reset (the overload that
    // takes a board is one call, not two).
    std::uint64_t binds = 0;
    // Comparisons of all of them.
    std::uint64_t comparisons = 0;
    // The most comparisons any one of them made.
    std::uint64_t largest = 0;
};

namespace detail {

// One tally per thread. A function's static and not a namespace-scope variable,
// as in solver_work.hpp.
[[nodiscard]] inline BindWork& bind_tally() noexcept {
    static thread_local BindWork work;
    return work;
}

inline void note_bind(std::uint64_t comparisons) noexcept {
    BindWork& work = bind_tally();
    ++work.binds;
    work.comparisons += comparisons;
    work.largest = std::max(work.largest, comparisons);
}

}  // namespace detail

// This thread's running total since the last reset.
[[nodiscard]] inline BindWork bind_work() noexcept {
    return detail::bind_tally();
}

// Starts this thread's total from zero: reset, make the call, read. (Not a
// before-and-after subtraction, because `largest` is a maximum.)
inline void reset_bind_work() noexcept {
    detail::bind_tally() = {};
}

}  // namespace wjs
