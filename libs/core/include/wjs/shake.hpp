// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <vector>

// Shake detection: a pure function from accelerometer samples to the shakes in
// them. The library reads no sensor; the application reads the platform's
// accelerometer and passes the samples in, so the same code runs on a recorded
// trace in a test and on the live stream on a device.
//
// A shake is a run of jolts, each a push the other way from the one before,
// close together in time. A jolt is the device's acceleration, with the steady
// pull of gravity taken away, going over a threshold. Gravity is estimated by a
// low-pass filter, so slowly turning the device is not a jolt and neither is
// laying it down.
//
// The defaults are starting values for a child shaking a tablet. They have not
// been tuned on a device, which is where they have to be settled
// (docs/design/board.md: "starting values to tune on a device").

namespace wjs {

struct AccelerationSample {
    double t = 0;  // seconds, from any fixed origin
    // The device's acceleration along its own axes, gravity included, in the
    // unit ShakeOptions::gravity names.
    double ax = 0;
    double ay = 0;
    double az = 0;
};

struct ShakeOptions {
    // The size of one g in the samples' unit: 9.80665 for metres per second
    // squared, which is what the platforms' motion sensors report, or 1 for
    // samples already in g.
    double gravity = 9.80665;

    // A push this many g beyond gravity is a jolt.
    double jolt_threshold = 1.2;

    // The time constant of the filter that follows gravity, in seconds. Short
    // enough that turning the device over is one jolt and then nothing, long
    // enough to pass the 2 to 6 hertz of a shake.
    double gravity_time_constant = 0.1;

    // The shortest time between one jolt and the next, in seconds, so the
    // ringing of one push is not several.
    double refractory = 0.08;

    // Jolts further apart than this, in seconds, are not one shake.
    double max_gap = 0.4;

    // Jolts that make a shake.
    std::size_t min_jolts = 4;
};

struct Shake {
    double start = 0;       // the time of its first jolt
    double end = 0;         // the time of its last jolt
    std::size_t jolts = 0;  // how many
    double peak = 0;        // the hardest jolt, in g beyond gravity
    bool ongoing = false;   // the trace ended within max_gap of `end`: it may go on
};

// The shakes in a trace, in time order. Samples are taken in the order given; a
// sample whose time is not later than the one before, or that holds a value
// that is not finite, is skipped. A shake is reported once it has min_jolts
// jolts, and `ongoing` says whether the trace ends while it may still be going,
// which is what a live caller asks of a window of the latest samples.
[[nodiscard]] std::vector<Shake> detect_shakes(std::span<const AccelerationSample> samples,
                                               const ShakeOptions& options = {});

}  // namespace wjs
