// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "wjs/motion.hpp"

// The device's motion processing (wjs::MotionProcessor): the accelerometer's
// metres per second squared become the scene's g, the gyroscope passes through,
// and the same samples go through wjs::detect_shakes so only a real shake is
// reported - and reported once.
//
// The traces are synthetic, as libs/core/tests/unit/shake_test.cpp's are: a device lying
// flat with gravity along z and sinusoidal pushes along x. They are not
// recordings; the detector's starting values still have to be tuned on a
// tablet. No hardware is needed, which is the point: on the desktops there is
// no accelerometer to test with.

namespace {

constexpr double standard_gravity = 9.80665;
constexpr double pi = 3.14159265358979323846;

struct Burst {
    double from = 0;       // seconds
    double to = 0;         // seconds
    double amplitude = 0;  // g
    double frequency = 0;  // hertz
};

// A device lying flat: gravity along z, and an optional push along x.
std::vector<wjs::DeviceMotion> make_trace(double duration, double rate,
                                          const std::vector<Burst>& bursts = {}) {
    std::vector<wjs::DeviceMotion> samples;
    const auto count = static_cast<std::size_t>(duration * rate);
    samples.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / rate;
        double push = 0;
        for (const Burst& burst : bursts) {
            if (t >= burst.from && t <= burst.to) {
                push += burst.amplitude * std::sin(2.0 * pi * burst.frequency * (t - burst.from));
            }
        }
        wjs::DeviceMotion sample;
        sample.t = t;
        sample.ax = push * standard_gravity;
        sample.ay = 0;
        sample.az = standard_gravity;
        // A hand that shakes the tablet also turns it a little.
        sample.wx = push * 0.4;
        sample.wy = 0;
        sample.wz = 0;
        samples.push_back(sample);
    }
    return samples;
}

}  // namespace

TEST_CASE("the scene is given g and the gyroscope's radians a second", "[motion][processor]") {
    wjs::MotionProcessor processor;
    wjs::DeviceMotion sample;
    sample.t = 0.5;
    sample.ax = standard_gravity;
    sample.ay = -standard_gravity / 2.0;
    sample.az = standard_gravity;
    sample.wx = 0.25;
    sample.wy = -1.5;
    sample.wz = 3.0;

    const wjs::MotionOutput output = processor.push(sample);

    CHECK(output.ax == 1.0);
    CHECK(output.ay == -0.5);
    CHECK(output.az == 1.0);
    CHECK(output.wx == 0.25);
    CHECK(output.wy == -1.5);
    CHECK(output.wz == 3.0);
    CHECK_FALSE(output.shake);
}

TEST_CASE("a sample that is not finite, or goes back in time, is not used", "[motion][processor]") {
    wjs::MotionProcessor processor;
    wjs::DeviceMotion good;
    good.t = 1.0;
    good.az = standard_gravity;
    const wjs::MotionOutput first = processor.push(good);
    CHECK(first.az == 1.0);

    wjs::DeviceMotion nan = good;
    nan.t = 2.0;
    nan.ax = std::numeric_limits<double>::quiet_NaN();
    const wjs::MotionOutput from_nan = processor.push(nan);
    CHECK(from_nan.ax == 0.0);
    CHECK(from_nan.az == 0.0);
    CHECK_FALSE(from_nan.shake);

    wjs::DeviceMotion earlier = good;
    earlier.t = 0.5;
    earlier.ax = 2.0 * standard_gravity;
    const wjs::MotionOutput from_earlier = processor.push(earlier);
    CHECK(from_earlier.ax == 0.0);
    CHECK_FALSE(from_earlier.shake);

    // The next good sample is still processed, with the scene's numbers.
    wjs::DeviceMotion next = good;
    next.t = 1.01;
    const wjs::MotionOutput from_next = processor.push(next);
    CHECK(from_next.az == 1.0);
}

TEST_CASE("a device lying still is not shaken", "[motion][processor]") {
    wjs::MotionProcessor processor;
    for (const wjs::DeviceMotion& sample : make_trace(3.0, 100.0)) {
        CHECK_FALSE(processor.push(sample).shake);
    }
}

TEST_CASE("a vigorous shake is reported once, on the sample that completes it",
          "[motion][processor]") {
    wjs::MotionProcessor processor;
    const auto trace = make_trace(3.0, 100.0, {{0.5, 2.0, 2.5, 4.0}});

    int reports = 0;
    std::size_t report_at = 0;
    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (processor.push(trace[i]).shake) {
            ++reports;
            report_at = i;
        }
    }

    CHECK(reports == 1);
    CHECK(trace[report_at].t >= 0.5);
    CHECK(trace[report_at].t <= 1.0);
}

TEST_CASE("two shakes are two events, and a reset forgets the first", "[motion][processor]") {
    wjs::MotionProcessor processor;
    int reports = 0;
    for (const wjs::DeviceMotion& sample :
         make_trace(4.0, 100.0, {{0.4, 1.4, 2.5, 4.0}, {2.0, 3.0, 2.5, 4.0}})) {
        if (processor.push(sample).shake) {
            ++reports;
        }
    }
    CHECK(reports == 2);

    processor.reset();
    CHECK_FALSE(processor.push(wjs::DeviceMotion{}).shake);
    reports = 0;
    for (const wjs::DeviceMotion& sample : make_trace(2.0, 100.0, {{0.5, 1.5, 2.5, 4.0}})) {
        if (processor.push(sample).shake) {
            ++reports;
        }
    }
    CHECK(reports == 1);
}
