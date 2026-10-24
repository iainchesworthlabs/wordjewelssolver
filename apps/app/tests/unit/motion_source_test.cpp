// SPDX-License-Identifier: GPL-3.0-or-later
#include <QEventLoop>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>

#include "motion_source.hpp"
#include "test_support.hpp"

// The MotionSource the window binds to. On the desktops there is no
// accelerometer: the source is unavailable, starting it is harmless, no sample
// arrives, and the drag stands in (SoloScreen.qml). The processing itself is the
// pure library's case (libs/core/tests/unit/motion_test.cpp), so the cases that need a
// sensor play a recorded trace through accept(), the path the backend's readings
// take; no hardware is needed here either.

namespace {

constexpr double standard_gravity = 9.80665;
constexpr double pi = 3.14159265358979323846;

// Runs the event loop for a moment: a sample that was going to arrive from the
// sensors would be delivered by it.
void spin(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

struct Counts {
    int samples = 0;
    int shakes = 0;
    int availability = 0;
};

void count(wjs::app::MotionSource& motion, Counts& counts) {
    QObject::connect(&motion, &wjs::app::MotionSource::sample,
                     [&counts](qreal, qreal, qreal, qreal, qreal, qreal) { ++counts.samples; });
    QObject::connect(&motion, &wjs::app::MotionSource::shakeDetected,
                     [&counts] { ++counts.shakes; });
    QObject::connect(&motion, &wjs::app::MotionSource::availableChanged,
                     [&counts] { ++counts.availability; });
}

// A device lying flat, pushed along x for a second and a half: one shake (the
// library's trace, libs/core/tests/unit/motion_test.cpp).
void play_one_shake(wjs::app::MotionSource& motion, double offset = 0.0) {
    constexpr double rate = 100.0;
    for (std::size_t i = 0; i < 300; ++i) {
        const double t = static_cast<double>(i) / rate;
        double push = 0.0;
        if (t >= 0.5 && t <= 2.0) {
            push = 2.5 * std::sin(2.0 * pi * 4.0 * (t - 0.5));
        }
        wjs::DeviceMotion sample;
        sample.t = offset + t;
        sample.ax = push * standard_gravity;
        sample.az = standard_gravity;
        sample.wx = push * 0.4;
        motion.accept(sample);
    }
}

}  // namespace

TEST_CASE("a desktop motion source has no accelerometer and stops safely", "[app][motion]") {
    wjs::app::test::GuiApplication app;
    wjs::app::MotionSource motion;

    CHECK_FALSE(motion.available());
    CHECK_FALSE(motion.active());

    Counts counts;
    count(motion, counts);

    motion.setShakeEnabled(false);
    CHECK_FALSE(motion.shakeEnabled());
    motion.setActive(true);
    CHECK(motion.active());
    // The event loop is what would deliver a reading. A sensor that is there makes
    // the source available with its first sample, so "no sample" and "not
    // available" are the same fact on any machine; and with the shake off, none
    // can have rolled the box.
    spin(100);
    CHECK((counts.samples > 0) == motion.available());
    CHECK(counts.shakes == 0);
    motion.setActive(false);
    CHECK_FALSE(motion.active());
}

TEST_CASE("the first sample makes the source available, once", "[app][motion]") {
    wjs::app::test::GuiApplication app;
    wjs::app::MotionSource motion;
    Counts counts;
    count(motion, counts);
    REQUIRE_FALSE(motion.available());

    wjs::DeviceMotion sample;
    sample.t = 0.0;
    sample.az = standard_gravity;
    motion.accept(sample);
    CHECK(motion.available());
    CHECK(counts.samples == 1);
    CHECK(counts.availability == 1);

    sample.t = 0.01;
    motion.accept(sample);
    CHECK(counts.samples == 2);
    CHECK(counts.availability == 1);
}

TEST_CASE("a shake rolls the box only while Shake is on, and samples flow either way",
          "[app][motion]") {
    wjs::app::test::GuiApplication app;
    wjs::app::MotionSource motion;
    Counts counts;
    count(motion, counts);

    // On by default: one shake, reported once.
    REQUIRE(motion.shakeEnabled());
    play_one_shake(motion);
    CHECK(counts.shakes == 1);
    const int samples_with_shake_on = counts.samples;
    CHECK(samples_with_shake_on == 300);

    // Off: the same trace reaches the scene (motion also pushes the dice) and
    // rolls nothing.
    motion.setShakeEnabled(false);
    play_one_shake(motion, 10.0);
    CHECK(counts.shakes == 1);
    CHECK(counts.samples == 2 * samples_with_shake_on);
}

TEST_CASE("starting the sensors again is a new trace", "[app][motion]") {
    wjs::app::test::GuiApplication app;
    wjs::app::MotionSource motion;
    Counts counts;
    count(motion, counts);

    play_one_shake(motion);
    REQUIRE(counts.shakes == 1);

    // The same shake again, in the same stretch of time (a clock that began
    // again), is the processor's "goes back in time" and gives no second report
    // unless the trace was reset: setActive(true) is that reset.
    motion.setActive(true);
    motion.setActive(false);
    play_one_shake(motion);
    CHECK(counts.shakes == 2);
}
