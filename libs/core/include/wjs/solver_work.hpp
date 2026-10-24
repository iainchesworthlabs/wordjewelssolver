// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace wjs {

// What the solver's searches have cost so far on this thread, counted in
// visits: the tile steps a search took that stayed inside the dictionary's
// trie, which is the one figure that does not depend on the machine, the
// compiler or the build type. A change that loses the trie pruning multiplies
// it by orders of magnitude long before a stopwatch would notice.
//
// A solve counts its visits as it walks (one add to a member of the search)
// and writes them here once, as it ends, so the thread-local costs one add per
// solve and nothing per visit. The performance tests (libs/core/bench,
// docs/testing.md "Performance tests") reset it, make a call and read it;
// nothing in the application does.
struct SolveWork {
    // Calls to solve() on this thread since the last reset.
    std::uint64_t solves = 0;
    // Visits of all of them.
    std::uint64_t visits = 0;
    // The most visits any one of them made.
    std::uint64_t largest = 0;
};

namespace detail {

// One tally per thread. A function's static and not a namespace-scope variable:
// the same thing, without being a non-const global that anything can reach.
[[nodiscard]] inline SolveWork& solve_tally() noexcept {
    static thread_local SolveWork work;
    return work;
}

inline void note_solve(std::uint64_t visits) noexcept {
    SolveWork& work = solve_tally();
    ++work.solves;
    work.visits += visits;
    work.largest = std::max(work.largest, visits);
}

}  // namespace detail

// This thread's running total since the last reset.
[[nodiscard]] inline SolveWork solve_work() noexcept {
    return detail::solve_tally();
}

// Starts this thread's total from zero: reset, make the call, read. (Not a
// before-and-after subtraction, because `largest` is a maximum.)
inline void reset_solve_work() noexcept {
    detail::solve_tally() = {};
}

}  // namespace wjs
