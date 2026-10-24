// SPDX-License-Identifier: GPL-3.0-or-later
// The dice simulation (apps/app/src/scene/sim) and the face of a die that
// is to be up: the board says which face of each die is up, and the guide turns
// the die to it. What has to be true: a face asked for is the face that is up
// when the die is home, whichever way the die lay; every die still comes home
// and is still when it is given a face (its own, since a die with faces is a
// particular die); the replay is the same replay; and a die that is not yet
// being guided moves as it would with no face asked for.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "dice_world.hpp"

namespace {

using namespace wjs::dice;

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// A hand shaking a box, as dice_world_test.cpp has it: strokes of 3 to 5 Hz up
// to 1.2 g sideways and 3 g up and down, then put down.
Reading shaking(double t, std::uint32_t seed) {
    const auto phase = [seed](std::uint32_t k) {
        return kTwoPi * std::fmod(0.6180339887 * (seed * 7 + k * 13 + 1), 1.0);
    };
    const double burst = t < 1.0 ? 1.0 : std::max(0.0, 1.0 - (t - 1.0) / 0.12);
    const double stroke = std::sin(kTwoPi * 3.7 * t + phase(0));
    const double cross = std::sin(kTwoPi * 3.1 * t + phase(1));
    Reading r;
    r.force = Vec3{1.2 * burst * stroke, 1.2 * burst * cross,
                   1.0 + 2.6 * burst * std::sin(kTwoPi * 3.7 * t + phase(2))};
    r.spin = Vec3{1.4 * burst * cross, -1.4 * burst * stroke, 0.3 * burst * stroke};
    r.spinChange = Vec3{-1.4 * burst * kTwoPi * 3.1 * std::sin(kTwoPi * 3.1 * t + phase(1)),
                        1.4 * burst * kTwoPi * 3.7 * std::sin(kTwoPi * 3.7 * t + phase(0)), 0.0};
    return r;
}

// The faces asked for: a different one for each die, from a small generator.
std::vector<int> targets(int count, std::uint32_t seed) {
    std::vector<int> faces;
    std::uint32_t x = seed * 2654435761U + 1U;
    for (int i = 0; i < count; ++i) {
        x = x * 1664525U + 1013904223U;
        faces.push_back(static_cast<int>((x >> 16) % 6U));
    }
    return faces;
}

// A play as the scene does it, up to the lid: the hand shakes for a second and
// puts the box down.
void shake(World& world, std::uint32_t seed) {
    for (double t = 0.0; t < 0.9; t += 0.01) {
        world.setReading(shaking(t, seed));
        world.advance(0.01);
    }
    world.setReading(Reading{});
    world.advance(0.15);
    world.setLid(false);
}

// The guide, with each die to its own cell, until every die is down or the time
// is up; the seconds it took.
double guideHome(World& world, double limit) {
    std::vector<double> delays;
    for (int i = 0; i < world.count(); ++i) {
        delays.push_back(0.03 * ((i * 7) % world.count()));
    }
    world.guide(delays);
    double elapsed = 0.0;
    while (!world.allDone() && elapsed < limit) {
        world.advance(0.01);
        elapsed += 0.01;
    }
    return elapsed;
}

bool squareToTheBox(Quat q) {
    for (const Vec3& axis : axesOf(q)) {
        const double biggest = std::max({std::abs(axis.x), std::abs(axis.y), std::abs(axis.z)});
        if (std::abs(biggest - 1.0) > 1e-9) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("a die is squared with the face asked for up, whichever way it lies", "[dice][faces]") {
    // Twenty-four ways to lie, and a few that are not square.
    std::uint32_t x = 12345U;
    const auto next = [&x]() {
        x = x * 1664525U + 1013904223U;
        return static_cast<double>(x >> 8) / 16777216.0;
    };
    for (int sample = 0; sample < 200; ++sample) {
        const Quat q = normalized(Quat{next() - 0.5, next() - 0.5, next() - 0.5, next() - 0.5});
        for (int face = 0; face < kFaceCount; ++face) {
            const Quat square = squareTo(q, face);
            INFO("sample " << sample << " face " << face);
            CHECK(squareToTheBox(square));
            CHECK(uprightOf(square).face == face);
        }
        // With no face asked for, it is the face that was nearest to up.
        CHECK(uprightOf(squareTo(q)).face == uprightOf(q).face);
    }
}

TEST_CASE("with a face to be up, every die comes home on it, and still", "[dice][faces]") {
    int runs = 0;
    int slow = 0;  // the runs that were not home in three seconds
    for (const int side : {3, 4, 6}) {
        for (const std::uint32_t seed : {1U, 2U, 3U, 4U, 5U, 6U}) {
            Box box;
            box.rows = side;
            box.columns = side;
            World world(box, Tuning{}, seed);
            const std::vector<int> faces = targets(world.count(), seed);
            world.setTargets(faces);
            shake(world, seed);
            const double elapsed = guideHome(world, 4.0);

            INFO("side " << side << " seed " << seed << " took " << elapsed << " s");
            // For every trajectory: home within the four seconds the guide is given.
            CHECK(world.allDone());
            ++runs;
            slow += elapsed < 3.0 ? 0 : 1;
            int wrong = 0;
            double far = 0.0;
            for (int i = 0; i < world.count(); ++i) {
                const Die& d = world.dice()[static_cast<std::size_t>(i)];
                wrong += uprightOf(d.q).face == faces[static_cast<std::size_t>(i)] ? 0 : 1;
                far = std::max(far, length(d.x - world.cellCentre(i)));
                CHECK(squareToTheBox(d.q));
                CHECK(length(d.v) == 0.0);
            }
            // Each die is on its own cell, and on the face it was asked for.
            CHECK(wrong == 0);
            CHECK(far < 1e-9);
            CHECK(world.deepestOverlap() < 0.02);
        }
    }
    // And promptly: three seconds is the time the guide is meant to keep to. One run
    // in ten may be later without the guide being wrong, since the dice's paths are
    // each compiler's own; a guide that is slow is slow for most of them.
    CHECK(slow * 10 <= runs);
}

TEST_CASE("finish puts every die home on its face", "[dice][faces]") {
    Box box;
    World world(box, Tuning{}, 9);
    const std::vector<int> faces = targets(world.count(), 9);
    world.setTargets(faces);
    for (int i = 0; i < 80; ++i) {
        world.setReading(shaking(i * 0.01, 9));
        world.advance(0.01);
    }
    world.finish();

    CHECK(world.allDone());
    for (int i = 0; i < world.count(); ++i) {
        const Die& d = world.dice()[static_cast<std::size_t>(i)];
        CHECK(uprightOf(d.q).face == faces[static_cast<std::size_t>(i)]);
        CHECK(squareToTheBox(d.q));
        CHECK(length(d.x - world.cellCentre(i)) < 1e-9);
    }
}

TEST_CASE("the faces change nothing until the guide starts, and a replay is the same replay",
          "[dice][faces]") {
    const auto play = [](bool withFaces, std::uint32_t seed, bool guided) {
        World world(Box{}, Tuning{}, seed);
        if (withFaces) {
            world.setTargets(targets(world.count(), seed + 100));
        }
        shake(world, seed);
        if (guided) {
            guideHome(world, 4.0);
        }
        return world.digest();
    };
    // Before the guide the dice cannot tell: the faces are asked for later.
    CHECK(play(true, 3, false) == play(false, 3, false));
    CHECK(play(true, 4, false) == play(false, 4, false));
    // After it they are told apart, and each is the same twice.
    CHECK(play(true, 3, true) == play(true, 3, true));
    CHECK(play(true, 3, true) != play(false, 3, true));
}
