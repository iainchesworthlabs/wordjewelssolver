// SPDX-License-Identifier: GPL-3.0-or-later
#include <QList>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <limits>

#include "dice_sim.hpp"

// DiceSim is the dice simulation as QML calls it: Q_INVOKABLE methods whose
// arguments are whatever a script computed. Nothing it is handed may hang it, throw
// out of it (an exception out of an invokable ends the program), or leave it with a
// world that cannot move again; the checks of the world itself are in
// apps/app/tests/prototype/dice_world_test.cpp, and these are the same promises as the QML
// object keeps them.

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

[[nodiscard]] bool all_finite(const QList<double>& numbers) {
    for (const double number : numbers) {
        if (!std::isfinite(number)) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("a script cannot make the dice's box impossible", "[scene][input]") {
    DiceSim sim;
    struct Box {
        int rows;
        int columns;
        double pitch;
        double slack;
        double lid;
    };
    const Box boxes[] = {
        {0, 0, 1.08, 0.6, 2.4},
        {-1, -1, 1.08, 0.6, 2.4},
        {2'000'000'000, 2'000'000'000, 1.08, 0.6, 2.4},
        {4, 4, kNaN, kNaN, kNaN},
        {4, 4, kInfinity, -kInfinity, kInfinity},
    };
    for (const Box& box : boxes) {
        INFO("rows " << box.rows << ", columns " << box.columns);
        // The call is the check: a throw out of it is a failure of the test.
        sim.reset(box.rows, box.columns, box.pitch, box.slack, box.lid, 1);
        const QList<double> poses = sim.poses();
        CHECK(poses.size() >= 7);
        CHECK(poses.size() % 7 == 0);
        CHECK(all_finite(poses));
        sim.advance(0.05);
        CHECK(all_finite(sim.poses()));
    }
}

TEST_CASE("a script cannot hang or freeze the dice with the time it passes", "[scene][input]") {
    DiceSim sim;
    sim.reset(2, 2, 1.08, 0.6, 2.42, 1);
    sim.advance(0.05);
    const int steps = sim.steps();
    const QString digest = sim.digest();

    sim.advance(kInfinity);
    sim.advance(-kInfinity);
    sim.advance(kNaN);
    sim.advance(-3.0);
    CHECK(sim.steps() == steps);
    CHECK(sim.digest() == digest);

    // Not frozen by it: time still passes.
    sim.advance(0.05);
    CHECK(sim.steps() > steps);
    CHECK(all_finite(sim.poses()));

    // A stall of an hour is a second of play, not four million steps.
    sim.advance(3600.0);
    CHECK(sim.steps() < steps + 2'500);
}

TEST_CASE("a script cannot poison the dice with a reading", "[scene][input]") {
    DiceSim sim;
    sim.reset(3, 3, 1.08, 0.6, 2.42, 7);
    sim.setReading(kNaN, kInfinity, -kInfinity, kNaN, kNaN, 1.0e300, kInfinity, kNaN, 0.0);
    sim.advance(0.5);
    CHECK(all_finite(sim.poses()));
}
