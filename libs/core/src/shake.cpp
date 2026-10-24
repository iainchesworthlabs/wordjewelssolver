// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/shake.hpp"

#include <algorithm>
#include <cmath>

#include "wjs/profiling.hpp"

namespace wjs {
namespace {

struct Vector {
    double x = 0;
    double y = 0;
    double z = 0;
};

[[nodiscard]] double dot(const Vector& a, const Vector& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] double length(const Vector& v) noexcept {
    return std::sqrt(dot(v, v));
}

// The jolts of one run, as they are found.
struct Chain {
    std::size_t jolts = 0;
    double start = 0;
    double last = 0;
    double peak = 0;
    Vector direction;  // of the last jolt
};

}  // namespace

std::vector<Shake> detect_shakes(std::span<const AccelerationSample> samples,
                                 const ShakeOptions& options) {
    auto zone = profiling::zone<"shake.detect">();

    std::vector<Shake> shakes;

    const double threshold = options.jolt_threshold * options.gravity;
    const double constant = options.gravity_time_constant;

    Chain chain;
    Vector gravity;
    double previous_time = 0;
    double last_time = 0;
    bool started = false;

    const auto finish = [&](bool ongoing) {
        if (chain.jolts >= options.min_jolts && chain.jolts > 0) {
            shakes.push_back(
                Shake{chain.start, chain.last, chain.jolts, chain.peak / options.gravity, ongoing});
        }
    };

    for (const AccelerationSample& sample : samples) {
        if (!std::isfinite(sample.t) || !std::isfinite(sample.ax) || !std::isfinite(sample.ay) ||
            !std::isfinite(sample.az)) {
            continue;
        }
        const Vector now{sample.ax, sample.ay, sample.az};
        if (!started) {
            started = true;
            gravity = now;
            previous_time = sample.t;
            last_time = sample.t;
            continue;
        }
        if (sample.t <= previous_time) {
            continue;
        }

        // What is left once the steady pull is taken away, using the estimate
        // from before this sample so a push is not averaged into its own
        // baseline.
        const Vector push{now.x - gravity.x, now.y - gravity.y, now.z - gravity.z};
        const double elapsed = sample.t - previous_time;
        const double keep = constant > 0 ? std::exp(-elapsed / constant) : 0.0;
        gravity.x = keep * gravity.x + (1.0 - keep) * now.x;
        gravity.y = keep * gravity.y + (1.0 - keep) * now.y;
        gravity.z = keep * gravity.z + (1.0 - keep) * now.z;
        previous_time = sample.t;
        last_time = sample.t;

        const double strength = length(push);
        if (strength < threshold) {
            continue;
        }

        // The hardest sample of the jolt just counted, while it lasts.
        if (chain.jolts > 0 && sample.t - chain.last < options.refractory) {
            chain.peak = std::max(chain.peak, strength);
            continue;
        }

        if (chain.jolts > 0 && sample.t - chain.last <= options.max_gap) {
            // The same push carried on in the same direction is not a new jolt;
            // a shake goes back and forth.
            if (dot(push, chain.direction) >= 0) {
                continue;
            }
            ++chain.jolts;
        } else {
            finish(false);
            chain = Chain{};
            chain.jolts = 1;
            chain.start = sample.t;
        }
        chain.last = sample.t;
        chain.direction = push;
        chain.peak = std::max(chain.peak, strength);
    }

    finish(started && last_time - chain.last <= options.max_gap);
    zone.Value(shakes.size());
    return shakes;
}

}  // namespace wjs
