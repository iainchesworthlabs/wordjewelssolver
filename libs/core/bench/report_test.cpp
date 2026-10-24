// SPDX-License-Identifier: GPL-3.0-or-later
// What every performance metric measures right now, beside its budget - the
// tool for setting or moving one (docs/testing.md, "Performance tests"). These
// cases assert nothing and are hidden, so no test run selects them:
//
//   wjs_perf_tests "[.perf-report]"          everything
//   wjs_perf_tests "[.perf-report-timing]"   only the wall-time metrics
//
// The timing half is only worth reading from an optimised build.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "budgets.hpp"
#include "corpus.hpp"
#include "measure.hpp"
#include "metrics.hpp"
#include "perf_build.hpp"

namespace {

void print_build() {
    std::cout << std::format("build: {}  optimised {}  timing meaningful {}\n",
                             wjs::perf::kBuildType, wjs::perf::kOptimised ? 1 : 0,
                             wjs::perf::kTimingMeaningful ? 1 : 0);
}

// One line per metric: what it measures, its budget, the room between them
// (how far above the measurement the budget sits), and where it came from.
void print_metrics(const char* title, const std::vector<wjs::perf::Metric>& metrics) {
    std::cout << std::format("\n{}\n", title);
    for (const wjs::perf::Metric& metric : metrics) {
        const double headroom = metric.measured > 0.0
                                    ? 100.0 * (metric.budget - metric.measured) / metric.measured
                                    : 0.0;
        std::string where;
        if (metric.seed != wjs::perf::kNoSeed) {
            where = "seed " + std::to_string(metric.seed);
        }
        if (!metric.note.empty()) {
            where += (where.empty() ? "" : "  ") + metric.note;
        }
        std::cout << std::format("  {:<34} {:<8} {:>12.3f} / {:<12.3f} {:<14} {:>+7.0f}%  {}{}\n",
                                 metric.name, wjs::perf::to_string(metric.mode), metric.measured,
                                 metric.budget, metric.unit, headroom,
                                 metric.holds() ? "" : "OVER  ", where);
    }
}

void print_timing() {
    print_metrics("timing: dictionary (fastest run against the calibration workload)",
                  wjs::perf::load_timing_metrics());
    print_metrics("timing: solves (one solve against the calibration workload)",
                  wjs::perf::solve_timing_metrics());
    print_metrics(
        "timing: PHONICS BOARD, one deal of a session board against the calibration workload",
        wjs::perf::session_timing_metrics());
    print_metrics(
        "timing: STANDARD BOARD, one deal of a session board against the calibration workload",
        wjs::perf::standard_session_timing_metrics());

    print_metrics("timing: reading a frame (one read against the heavy calibration workload)",
                  wjs::perf::scan_timing_metrics());
    print_metrics(
        "timing: the store, the dashboard, the phase directory, a submit and a path (against the "
        "calibration workload)",
        wjs::perf::data_timing_metrics());
    // The two calibrations on their own, so that a ratio can be read back into
    // milliseconds.
    for (const wjs::perf::Weight weight : {wjs::perf::Weight::light, wjs::perf::Weight::heavy}) {
        double fastest = 1e9;
        for (int run = 0; run < 7; ++run) {
            const auto start = wjs::perf::Clock::now();
            wjs::perf::detail::g_sink = wjs::perf::calibration_workload(weight);
            fastest = std::min(fastest, wjs::perf::milliseconds_since(start));
        }
        std::cout << std::format("  calibration {:<6} fastest of 7: {:.3f} ms\n",
                                 weight == wjs::perf::Weight::light ? "light" : "heavy", fastest);
    }
}

}  // namespace

TEST_CASE("report: what every metric measures now", "[.perf-report]") {
    print_build();
    print_metrics("dictionary", wjs::perf::dictionary_metrics());
    print_metrics("random boards: most visits of a solve, by size",
                  wjs::perf::random_board_metrics());
    print_metrics("boards built to hold many words, and boards with none",
                  wjs::perf::special_board_metrics());
    print_metrics("the scanner: what reading four synthetic frames does",
                  wjs::perf::scan_metrics());
    std::cout << std::format("\ndigest of every solution over the corpus: {} (kCorpusDigest)\n",
                             wjs::perf::corpus_digest());
    print_metrics(
        "STANDARD BOARD (the default): a roll of the dice and a solve, most visits by size",
        wjs::perf::dice_board_metrics());
    std::cout << std::format(
        "\ndigest of every solution over the rolled boards: {} (kDiceDigest)\n",
        wjs::perf::dice_digest());
    print_metrics("PHONICS BOARD (a parent's switch): seeding, by size",
                  wjs::perf::seeding_metrics());
    std::cout << std::format("\ndigest of every board seeded: {} (kSeedDigest)\n",
                             wjs::perf::seeding_digest());
    print_metrics("PHONICS BOARD (a parent's switch): the game session, by size",
                  wjs::perf::session_metrics());
    std::cout << std::format(
        "\ndigest of every board a session dealt and played: {} (kSessionDigest)\n",
        wjs::perf::session_digest());
    print_metrics("STANDARD BOARD (the default): the game session, by size",
                  wjs::perf::standard_session_metrics());
    std::cout << std::format(
        "\ndigest of every board a standard session dealt and played: {} "
        "(kStandardSessionDigest)\n",
        wjs::perf::standard_session_digest());
    print_metrics("PHONICS BOARD (a parent's switch): the binder, a path of nine tiles",
                  wjs::perf::binder_metrics());
    print_metrics("the store a play leaves, by mode (twenty boards), and at its heaviest",
                  wjs::perf::store_metrics());
    print_metrics("PHONICS BOARD (a parent's switch): the dashboard built from that store",
                  wjs::perf::dashboard_metrics());
    print_metrics("PHONICS BOARD (a parent's switch): the test phase loaded from a directory",
                  wjs::perf::phase_load_metrics());
    print_metrics("a path submitted to a session, by mode", wjs::perf::submit_metrics());
    print_timing();
}

TEST_CASE("report: the wall-time metrics", "[.perf-report-timing]") {
    print_build();
    print_timing();
}
