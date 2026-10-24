// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What a performance case does with its metrics, and how it stands aside in a
// build where it has nothing to say.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "measure.hpp"
#include "metrics.hpp"
#include "perf_build.hpp"

namespace wjs::perf {

// Ends the case as skipped (not failed) in a build that is not optimised: the
// corpus there is much longer, and the counts it would check are the same ones
// an optimised build checks.
//
// The test is a call, not `if constexpr (kOptimised)`: SKIP ends the case, and
// where the compiler can see that it always does, every statement after the
// call is unreachable code, which is a warning and so an error.
inline void skip_unless_optimised() {
    if (!optimised_build()) {
        SKIP("the performance tier runs in an optimised build, and this is "
             << kBuildType << "; its work counts are the same in every build, so the "
             << "optimised legs hold them (docs/testing.md, \"Performance tests\")");
    }
}

// Ends the case as skipped where a stopwatch cannot be read as a statement
// about the program, and where nobody asked for one.
inline void skip_unless_timing_wanted() {
    if (!timing_meaningful()) {
        SKIP("a clock reading is not about the program in this build ("
             << kBuildType << (kInstrumented ? ", with coverage or Tracy compiled in" : "")
             << "); wall-time checks need an optimised, uninstrumented build");
    }
    if (!timing_requested()) {
        SKIP(
            "wall-time checks are opt-in: set WJS_PERF_TIMING=1, or run the *-perf test "
            "preset (docs/testing.md, \"Performance tests\")");
    }
}

// Every metric within its budget. A failure names the metric, the seed that
// gave a maximum, and both numbers.
inline void check_within_budget(const std::vector<Metric>& metrics) {
    for (const Metric& metric : metrics) {
        INFO("metric " << metric.name << " (" << metric.unit << ", " << to_string(metric.mode)
                       << " board)"
                       << (metric.bound == Bound::exactly ? ", must be exactly the budget" : "")
                       << (metric.seed == kNoSeed
                               ? std::string()
                               : ", worst corpus seed " + std::to_string(metric.seed))
                       << (metric.note.empty() ? std::string() : "; " + metric.note)
                       << "; measured " << metric.measured << ", budget " << metric.budget);
        CHECK(metric.holds());
    }
}

}  // namespace wjs::perf
