// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "wjs/shake.hpp"

// The device's motion, from its raw samples to the two things the game does
// with it: the numbers the board scene pushes its dice with, and the shakes
// the box answers. Pure: the platform's sensors are the application's, and this
// is what a recorded trace and a live stream are both put through, so it is
// tested without hardware.
//
// The accelerometer's reading is in metres per second squared, gravity
// included, in the device's own axes. The scene wants it in g; a device lying
// face up is (0, 0, 1) there. The gyroscope is already in radians a second and
// is passed through. The shake detector takes the raw accelerometer reading
// (wjs/detect_shakes), so a slow turn is not a shake and a rattle is.

namespace wjs {

// One sample as a platform's motion sensors give it.
struct DeviceMotion {
    double t = 0;   // seconds, from any fixed origin, later each sample
    double ax = 0;  // metres per second squared, gravity included
    double ay = 0;
    double az = 0;
    double wx = 0;  // radians a second
    double wy = 0;
    double wz = 0;
};

// What the scene is given, and whether this sample completed a new shake.
struct MotionOutput {
    double ax = 0;  // g, gravity included
    double ay = 0;
    double az = 0;
    double wx = 0;  // radians a second
    double wy = 0;
    double wz = 0;
    bool shake = false;
};

// A live processor: every sample in, the scene's numbers out, and `shake` true
// on the one sample that first completes a shake. The window of recent samples
// the detector reads is kept inside, so a caller does not have to hold one.
class MotionProcessor {
public:
    explicit MotionProcessor(ShakeOptions options = {});

    // `sample` is not finite, or its time is not later than the one before:
    // the scene gets no numbers and no shake. The detector already skips such
    // samples; this keeps a bad value out of the dice too.
    [[nodiscard]] MotionOutput push(const DeviceMotion& sample);

    // Nothing is held: for a sensor that was stopped and started again.
    void reset();

    [[nodiscard]] const ShakeOptions& options() const noexcept { return options_; }

private:
    // How much of the trace detect_shakes reads on each sample. Long enough
    // for the detector's gravity filter to have settled and for a shake to
    // have its jolts; short enough that the work per sample stays small.
    static constexpr double window_seconds = 1.5;

    ShakeOptions options_;
    std::vector<AccelerationSample> window_;
    // The last shake reported, as its first and last jolts. A sliding window
    // loses the earliest samples of a long shake, and detect_shakes then
    // reports the same shake with a later start: the end it already reached
    // is what tells the two apart from a new shake, which cannot begin within
    // max_gap of the old one's end and be a different shake.
    std::optional<double> last_reported_start_;
    std::optional<double> last_reported_end_;
    std::optional<double> last_time_;
};

}  // namespace wjs
