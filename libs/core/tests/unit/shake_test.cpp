// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "wjs/shake.hpp"

// The traces below are synthetic: a device lying flat (gravity along z) with a
// little noise, and sinusoidal pushes along x for a shake. They are not
// recordings from a device; the detector's starting values have to be tuned on
// one. Amplitudes are in g, and the samples are metres per second squared, as a
// platform's motion sensor reports them, unless a case says otherwise.

namespace {

constexpr double standard_gravity = 9.80665;
constexpr double pi = 3.14159265358979323846;

// A burst of pushing back and forth along x.
struct Burst {
    double from;
    double to;
    double amplitude;  // g
    double frequency;  // Hz
};

struct TraceSpec {
    double duration = 3.0;
    double rate = 60.0;   // samples a second
    double jitter = 0.0;  // seconds, plus or minus, on each timestamp
    double unit = standard_gravity;
    double noise = 0.02;  // g, on each axis
    std::vector<Burst> bursts;
};

std::vector<wjs::AccelerationSample> make_trace(const TraceSpec& spec) {
    // Deterministic noise: mt19937 taken modulo, as the library does.
    std::mt19937 rng(12345);
    const auto noise = [&](double size) {
        const double unit = static_cast<double>(rng() % 2001U) / 1000.0 - 1.0;
        return unit * size;
    };

    std::vector<wjs::AccelerationSample> samples;
    const auto count = static_cast<std::size_t>(spec.duration * spec.rate);
    for (std::size_t i = 0; i < count; ++i) {
        const double nominal = static_cast<double>(i) / spec.rate;
        const double t = nominal + (spec.jitter > 0 ? noise(spec.jitter) : 0.0);

        double push = 0;
        for (const Burst& burst : spec.bursts) {
            if (nominal >= burst.from && nominal <= burst.to) {
                push +=
                    burst.amplitude * std::sin(2.0 * pi * burst.frequency * (nominal - burst.from));
            }
        }
        wjs::AccelerationSample sample;
        sample.t = t;
        sample.ax = (push + noise(spec.noise)) * spec.unit;
        sample.ay = noise(spec.noise) * spec.unit;
        sample.az = (1.0 + noise(spec.noise)) * spec.unit;
        samples.push_back(sample);
    }
    return samples;
}

wjs::ShakeOptions options_in(double unit) {
    wjs::ShakeOptions options;
    options.gravity = unit;
    return options;
}

}  // namespace

TEST_CASE("a device lying still is not shaken", "[shake]") {
    TraceSpec spec;
    spec.duration = 5.0;

    CHECK(wjs::detect_shakes(make_trace(spec)).empty());
}

TEST_CASE("no samples and one sample hold no shake", "[shake]") {
    CHECK(wjs::detect_shakes({}).empty());

    const std::vector<wjs::AccelerationSample> one = {{0.0, 0.0, 0.0, 9.8}};
    CHECK(wjs::detect_shakes(one).empty());
}

TEST_CASE("a vigorous shake is one shake, found where it is", "[shake]") {
    TraceSpec spec;
    spec.bursts = {{1.0, 2.5, 2.5, 4.0}};

    const auto shakes = wjs::detect_shakes(make_trace(spec));

    REQUIRE(shakes.size() == 1);
    CHECK(shakes[0].start >= 1.0);
    CHECK(shakes[0].start <= 1.3);
    CHECK(shakes[0].end >= 2.2);
    CHECK(shakes[0].end <= 2.5);
    CHECK(shakes[0].jolts >= 8);
    CHECK(shakes[0].peak > 1.2);     // above the jolt threshold
    CHECK_FALSE(shakes[0].ongoing);  // half a second of stillness follows
}

TEST_CASE("the peak of a shake follows how hard it was", "[shake]") {
    // A push of 1.6 g and one of 3.2 g, the same in every other way. The peak is
    // the hardest jolt beyond gravity, so it is near the push and the harder
    // shake has the greater, whatever the threshold is.
    const auto peak_of = [](double amplitude) {
        TraceSpec spec;
        spec.bursts = {{1.0, 2.5, amplitude, 4.0}};
        const auto shakes = wjs::detect_shakes(make_trace(spec));
        REQUIRE(shakes.size() == 1);
        return shakes[0].peak;
    };
    const double gentle = peak_of(1.6);
    const double hard = peak_of(3.2);

    CHECK(gentle > 1.2);
    CHECK(gentle < 1.9);
    CHECK(hard > gentle * 1.5);
    CHECK(hard < 3.5);
}

TEST_CASE("a shake the trace ends in the middle of is ongoing", "[shake]") {
    TraceSpec spec;
    spec.duration = 2.0;
    spec.bursts = {{1.0, 3.0, 2.5, 4.0}};

    const auto shakes = wjs::detect_shakes(make_trace(spec));

    REQUIRE(shakes.size() == 1);
    CHECK(shakes[0].ongoing);
}

TEST_CASE("a shake is reported only once it has enough jolts", "[shake]") {
    TraceSpec spec;
    spec.duration = 2.0;
    // Four jolts take two cycles of a 4 hertz shake, about a quarter of a second.
    spec.bursts = {{1.0, 1.1, 2.5, 4.0}};

    CHECK(wjs::detect_shakes(make_trace(spec)).empty());

    spec.bursts = {{1.0, 1.6, 2.5, 4.0}};
    CHECK(wjs::detect_shakes(make_trace(spec)).size() == 1);
}

TEST_CASE("a gentle movement below the threshold is not a shake", "[shake]") {
    TraceSpec spec;
    spec.bursts = {{0.5, 2.5, 0.7, 3.0}};

    CHECK(wjs::detect_shakes(make_trace(spec)).empty());
}

TEST_CASE("walking is not a shake", "[shake]") {
    TraceSpec spec;
    spec.duration = 6.0;
    spec.bursts = {{0.0, 6.0, 0.5, 2.0}};

    CHECK(wjs::detect_shakes(make_trace(spec)).empty());
}

TEST_CASE("one knock is not a shake", "[shake]") {
    auto samples = make_trace(TraceSpec{});
    // A single hard push along x at about t = 1.
    samples[60].ax += 4.0 * standard_gravity;
    samples[61].ax += 3.0 * standard_gravity;

    CHECK(wjs::detect_shakes(samples).empty());
}

TEST_CASE("turning the device over is not a shake", "[shake]") {
    auto samples = make_trace(TraceSpec{});
    // Face up, then face down from t = 1: gravity reverses along z at once.
    for (std::size_t i = 60; i < samples.size(); ++i) {
        samples[i].az = -samples[i].az;
    }

    CHECK(wjs::detect_shakes(samples).empty());
}

TEST_CASE("tilting the device slowly is not a shake", "[shake]") {
    auto samples = make_trace(TraceSpec{});
    // Gravity swings through a quarter turn about x over the first second and a
    // half and stays there.
    for (auto& sample : samples) {
        const double angle = (pi / 2.0) * std::min(sample.t / 1.5, 1.0);
        const double along = standard_gravity;
        sample.ay = along * std::sin(angle);
        sample.az = along * std::cos(angle);
    }

    CHECK(wjs::detect_shakes(samples).empty());
}

TEST_CASE("two shakes with a still moment between them are two", "[shake]") {
    TraceSpec spec;
    spec.duration = 5.0;
    spec.bursts = {{0.5, 1.5, 2.5, 4.0}, {3.0, 4.0, 2.5, 4.0}};

    const auto shakes = wjs::detect_shakes(make_trace(spec));

    REQUIRE(shakes.size() == 2);
    CHECK(shakes[0].end < shakes[1].start);
    CHECK(shakes[0].start < 1.0);
    CHECK(shakes[1].start >= 3.0);
}

TEST_CASE("a short pause inside a shake does not end it and a long one does", "[shake]") {
    TraceSpec spec;
    spec.duration = 4.0;

    SECTION("a pause of a tenth of a second between the pushes") {
        spec.bursts = {{0.5, 1.5, 2.5, 4.0}, {1.6, 2.6, 2.5, 4.0}};
        CHECK(wjs::detect_shakes(make_trace(spec)).size() == 1);
    }

    SECTION("a pause of most of a second") {
        spec.bursts = {{0.5, 1.5, 2.5, 4.0}, {2.3, 3.3, 2.5, 4.0}};
        CHECK(wjs::detect_shakes(make_trace(spec)).size() == 2);
    }
}

TEST_CASE("the sample rate and a little timing jitter make no difference", "[shake]") {
    for (const double rate : {30.0, 60.0, 100.0, 200.0}) {
        for (const double jitter : {0.0, 0.002}) {
            TraceSpec spec;
            spec.rate = rate;
            spec.jitter = jitter;
            spec.bursts = {{1.0, 2.5, 2.5, 4.0}};

            INFO("rate " << rate << " jitter " << jitter);
            CHECK(wjs::detect_shakes(make_trace(spec)).size() == 1);
        }
    }
}

TEST_CASE("the unit of the samples is the one the options name", "[shake]") {
    TraceSpec spec;
    spec.unit = 1.0;  // samples in g
    spec.bursts = {{1.0, 2.5, 2.5, 4.0}};
    const auto samples = make_trace(spec);

    SECTION("told the samples are in g") {
        CHECK(wjs::detect_shakes(samples, options_in(1.0)).size() == 1);
    }

    SECTION("read as metres per second squared the same push is too small") {
        CHECK(wjs::detect_shakes(samples, options_in(standard_gravity)).empty());
    }
}

TEST_CASE("a threshold can be raised to ask for a harder shake", "[shake]") {
    TraceSpec spec;
    spec.bursts = {{1.0, 2.5, 2.0, 4.0}};
    const auto samples = make_trace(spec);

    auto options = options_in(standard_gravity);
    CHECK(wjs::detect_shakes(samples, options).size() == 1);

    options.jolt_threshold = 2.5;
    CHECK(wjs::detect_shakes(samples, options).empty());
}

TEST_CASE("samples that are out of order or not numbers are skipped", "[shake]") {
    TraceSpec spec;
    spec.bursts = {{1.0, 2.5, 2.5, 4.0}};
    auto samples = make_trace(spec);

    // Damage the middle of the shake: a repeated time, a time that goes back,
    // and values that are not numbers.
    samples[100].t = samples[99].t;
    samples[110].t = samples[100].t - 5.0;
    samples[120].ax = std::numeric_limits<double>::quiet_NaN();
    samples[130].t = std::numeric_limits<double>::infinity();

    const auto shakes = wjs::detect_shakes(samples);

    REQUIRE(shakes.size() == 1);
    CHECK(shakes[0].jolts >= 8);
}

TEST_CASE("the same trace gives the same shakes", "[shake]") {
    TraceSpec spec;
    spec.bursts = {{1.0, 2.5, 2.5, 4.0}};
    const auto samples = make_trace(spec);

    const auto first = wjs::detect_shakes(samples);
    const auto second = wjs::detect_shakes(samples);

    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].start == second[i].start);
        CHECK(first[i].end == second[i].end);
        CHECK(first[i].jolts == second[i].jolts);
    }
}
