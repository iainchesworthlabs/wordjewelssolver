// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/motion.hpp"

#include <algorithm>
#include <cmath>

#include "wjs/profiling.hpp"

namespace wjs {

MotionProcessor::MotionProcessor(ShakeOptions options) : options_(options) {}

void MotionProcessor::reset() {
    window_.clear();
    last_reported_start_.reset();
    last_reported_end_.reset();
    last_time_.reset();
}

MotionOutput MotionProcessor::push(const DeviceMotion& sample) {
    auto zone = profiling::zone<"motion.push">();

    MotionOutput output;
    if (!std::isfinite(sample.t) || !std::isfinite(sample.ax) || !std::isfinite(sample.ay) ||
        !std::isfinite(sample.az) || !std::isfinite(sample.wx) || !std::isfinite(sample.wy) ||
        !std::isfinite(sample.wz)) {
        return output;
    }
    if (last_time_ && sample.t <= *last_time_) {
        // The sensors' clock went backwards, or two readings share a time: the
        // detector cannot use it and neither can the scene's history.
        return output;
    }
    last_time_ = sample.t;

    output.ax = sample.ax / options_.gravity;
    output.ay = sample.ay / options_.gravity;
    output.az = sample.az / options_.gravity;
    output.wx = sample.wx;
    output.wy = sample.wy;
    output.wz = sample.wz;

    window_.push_back(AccelerationSample{sample.t, sample.ax, sample.ay, sample.az});
    const double horizon = sample.t - std::max(window_seconds, options_.max_gap + 1.0);
    // The samples are kept contiguous (detect_shakes takes a span), so the old
    // ones are dropped in one erase and not one at a time.
    window_.erase(window_.begin(),
                  std::ranges::find_if(window_, [horizon](const AccelerationSample& kept) {
                      return kept.t >= horizon;
                  }));

    // detect_shakes reads the whole window each time and reports a shake once
    // it has its jolts. An ongoing shake keeps the start it had until its
    // earliest sample leaves the window; from then on it looks like a later
    // shake. A really new shake cannot start within max_gap of the previous
    // one's end - detect_shakes would have joined them - so the end is what
    // separates the two.
    for (const Shake& shake : detect_shakes(window_, options_)) {
        // The start and the end are set together; both are asked so that the
        // reads below are plainly of values.
        if (!last_reported_start_ || !last_reported_end_) {
            last_reported_start_ = shake.start;
            last_reported_end_ = shake.end;
            output.shake = true;
            continue;
        }
        if (shake.start <= *last_reported_start_) {
            continue;
        }
        if (shake.start > *last_reported_end_ + options_.max_gap) {
            last_reported_start_ = shake.start;
            last_reported_end_ = shake.end;
            output.shake = true;
        } else {
            // The same shake, its start pruned: keep the end it has reached.
            last_reported_end_ = shake.end;
        }
    }
    return output;
}

}  // namespace wjs
