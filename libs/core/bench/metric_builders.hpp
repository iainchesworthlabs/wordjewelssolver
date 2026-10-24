// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How a metric is made, for the files that make them (metrics.cpp,
// data_metrics.cpp): the three kinds of figure the tier holds, each stating the
// board mode it is for.

#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "budgets.hpp"
#include "measure.hpp"
#include "metrics.hpp"

namespace wjs::perf {

// Every metric states the board mode it is for, with no default to fall back
// on: a figure for one mode says nothing about the other.
[[nodiscard]] inline Metric exact(std::string name, BoardMode mode, std::uint64_t measured,
                                  std::uint64_t budget, std::string unit) {
    Metric metric;
    metric.name = std::move(name);
    metric.mode = mode;
    metric.measured = static_cast<double>(measured);
    metric.budget = static_cast<double>(budget);
    metric.bound = Bound::exactly;
    metric.unit = std::move(unit);
    return metric;
}

[[nodiscard]] inline Metric at_most(std::string name, BoardMode mode, std::uint64_t measured,
                                    std::uint64_t budget, std::string unit,
                                    unsigned seed = kNoSeed) {
    Metric metric;
    metric.name = std::move(name);
    metric.mode = mode;
    metric.measured = static_cast<double>(measured);
    metric.budget = static_cast<double>(budget);
    metric.seed = seed;
    metric.unit = std::move(unit);
    return metric;
}

[[nodiscard]] inline Metric ratio(std::string name, BoardMode mode, const RelativeTime& time,
                                  const budget::TimeBudget& allowed) {
    Metric metric;
    metric.name = std::move(name);
    metric.mode = mode;
    metric.measured = time.ratio;
    metric.budget = allowed.ratio;
    metric.unit = "x calibration";
    metric.note =
        std::format("fastest {:.3f} ms against {:.3f} ms (medians {:.3f} / {:.3f})", time.task_ms,
                    time.calibration_ms, time.task_median_ms, time.calibration_median_ms);
    return metric;
}

}  // namespace wjs::perf
