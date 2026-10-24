// SPDX-License-Identifier: GPL-3.0-or-later
// Wall-time checks, the part of the performance tier that is opt-in.
//
// A clock is the one measurement that can be wrong for no reason of its own:
// another process, a cold cache, a runner that is not the machine the budget
// was set on. So each figure here is a ratio to a calibration workload run in
// the same process, alternately with the task and with the same load on both,
// and each budget is several times what the ratio is; and the cases do not run
// unless WJS_PERF_TIMING is set, never in an unoptimised or instrumented build,
// and alone (ctest RUN_SERIAL), not beside other tests.
//
// What a failure means: docs/testing.md, "Performance tests".

#include <catch2/catch_test_macros.hpp>

#include "check.hpp"
#include "metrics.hpp"

TEST_CASE("loading the dictionary takes no more than a multiple of the calibration workload",
          "[perf][timing][dictionary]") {
    wjs::perf::skip_unless_timing_wanted();
    wjs::perf::check_within_budget(wjs::perf::load_timing_metrics());
}

TEST_CASE("solving takes no more than a multiple of the calibration workload",
          "[perf][timing][solver]") {
    wjs::perf::skip_unless_timing_wanted();
    wjs::perf::check_within_budget(wjs::perf::solve_timing_metrics());
}

TEST_CASE("dealing a board takes no more than a multiple of the calibration workload",
          "[perf][timing][session][phonics]") {
    wjs::perf::skip_unless_timing_wanted();
    wjs::perf::check_within_budget(wjs::perf::session_timing_metrics());
}

TEST_CASE("dealing a standard board takes no more than a multiple of the calibration workload",
          "[perf][timing][session][standard]") {
    wjs::perf::skip_unless_timing_wanted();
    wjs::perf::check_within_budget(wjs::perf::standard_session_timing_metrics());
}

TEST_CASE("reading a frame takes no more than a multiple of the calibration workload",
          "[perf][timing][scan]") {
    wjs::perf::skip_unless_timing_wanted();
    wjs::perf::check_within_budget(wjs::perf::scan_timing_metrics());
}

TEST_CASE(
    "the store, the dashboard, the phase directory and a submit take no more than a "
    "multiple of the calibration workload",
    "[perf][timing][store][dashboard][phase][submit]") {
    wjs::perf::skip_unless_timing_wanted();
    wjs::perf::check_within_budget(wjs::perf::data_timing_metrics());
}
