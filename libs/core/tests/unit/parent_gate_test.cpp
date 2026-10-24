// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include "wjs/parent_gate.hpp"

namespace {

using std::chrono::milliseconds;
using wjs::Gate;
using wjs::GateState;
using wjs::Point;

// An iPad-sized screen in points, landscape, and the layout the gate makes for it.
constexpr double screen_width = 1024.0;
constexpr double screen_height = 768.0;

wjs::GateLayout ipad_layout() {
    return wjs::gate_layout(screen_width, screen_height, 48.0);
}

Point in_first(const wjs::GateLayout& layout) {
    return layout.first.centre;
}
Point in_second(const wjs::GateLayout& layout) {
    return layout.second.centre;
}
Point nowhere() {
    return {screen_width / 2.0, screen_height / 2.0};
}

}  // namespace

TEST_CASE("the layout puts two spots far apart on the diagonal", "[gate]") {
    const auto layout = ipad_layout();

    CHECK(layout.first.centre.x < layout.second.centre.x);
    CHECK(layout.first.centre.y < layout.second.centre.y);
    CHECK(wjs::gate_layout_is_usable(layout, screen_width, screen_height));
}

TEST_CASE("the layout is usable on every screen shape the app meets", "[gate]") {
    struct Size {
        double w, h;
    };
    for (const Size size : {Size{1024, 768}, Size{768, 1024}, Size{844, 390}, Size{390, 844},
                            Size{1366, 1024}, Size{360, 640}, Size{2560, 1440}, Size{480, 800}}) {
        const auto layout = wjs::gate_layout(size.w, size.h, 48.0);
        INFO(size.w << " x " << size.h);
        CHECK(wjs::gate_layout_is_usable(layout, size.w, size.h));
        CHECK(layout.first.radius >= 48.0);
    }
}

TEST_CASE("the spots are never smaller than the adult touch target", "[gate]") {
    // 48 dp is the floor for anything an adult touches (application.md).
    CHECK(wjs::gate_layout(300.0, 300.0, 48.0).first.radius >= 48.0);
    CHECK(wjs::gate_layout(2000.0, 1500.0, 48.0).first.radius >= 48.0);
}

TEST_CASE("a layout whose spots are close, or off the screen, is not usable", "[gate]") {
    wjs::GateLayout close = ipad_layout();
    close.second.centre = {close.first.centre.x + close.first.radius * 2.0, close.first.centre.y};
    CHECK_FALSE(wjs::gate_layout_is_usable(close, screen_width, screen_height));

    wjs::GateLayout off = ipad_layout();
    off.first.centre = {5.0, 5.0};
    CHECK_FALSE(wjs::gate_layout_is_usable(off, screen_width, screen_height));

    wjs::GateLayout empty = ipad_layout();
    empty.first.radius = 0.0;
    CHECK_FALSE(wjs::gate_layout_is_usable(empty, screen_width, screen_height));
}

TEST_CASE("a spot contains its edge and nothing past it", "[gate]") {
    const wjs::Spot spot{{100.0, 100.0}, 10.0};
    CHECK(spot.contains({100.0, 100.0}));
    CHECK(spot.contains({110.0, 100.0}));
    CHECK_FALSE(spot.contains({110.1, 100.0}));
    CHECK_FALSE(spot.contains({108.0, 108.0}));
    CHECK(spot.contains({107.0, 107.0}));
}

TEST_CASE("two fingers on the two spots for two seconds open the gate", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    CHECK(gate.state() == GateState::waiting);
    gate.touch_down(1, in_first(layout), milliseconds{0});
    CHECK(gate.state() == GateState::waiting);
    gate.touch_down(2, in_second(layout), milliseconds{100});
    CHECK(gate.state() == GateState::holding);

    gate.tick(milliseconds{2099});
    CHECK_FALSE(gate.is_open());
    gate.tick(milliseconds{2100});
    CHECK(gate.is_open());
    CHECK(gate.state() == GateState::open);
    CHECK(gate.progress(milliseconds{5000}) == 1.0);
}

TEST_CASE("the order of the fingers does not matter", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    gate.touch_down(7, in_second(layout), milliseconds{0});
    gate.touch_down(3, in_first(layout), milliseconds{0});
    gate.tick(milliseconds{2000});

    CHECK(gate.is_open());
}

TEST_CASE("one second is not enough", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    gate.tick(milliseconds{1000});
    gate.touch_up(1, milliseconds{1000});
    gate.touch_up(2, milliseconds{1000});
    gate.tick(milliseconds{9000});

    CHECK_FALSE(gate.is_open());
    CHECK(gate.state() == GateState::waiting);
}

TEST_CASE("the hold is measured when a finger lifts as well as when a clock ticks", "[gate]") {
    // No tick came between the two touches going down and one lifting after
    // three seconds: the hold had been long enough, and the gate says so.
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    gate.touch_up(2, milliseconds{3000});

    CHECK(gate.is_open());
}

TEST_CASE("progress fills in step with the hold", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    CHECK(gate.progress(milliseconds{0}) == 0.0);

    gate.touch_down(1, in_first(layout), milliseconds{500});
    gate.touch_down(2, in_second(layout), milliseconds{500});

    CHECK(gate.progress(milliseconds{500}) == 0.0);
    CHECK(gate.progress(milliseconds{1500}) == 0.5);
    CHECK(gate.progress(milliseconds{2500}) == 1.0);
    CHECK(gate.progress(milliseconds{9000}) == 1.0);
    CHECK(gate.progress(milliseconds{0}) == 0.0);  // a clock that went backwards is not progress
}

TEST_CASE("one finger on one spot, however long, never opens it", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.tick(milliseconds{60000});

    CHECK_FALSE(gate.is_open());
    CHECK(gate.progress(milliseconds{60000}) == 0.0);
}

TEST_CASE("two fingers on the same spot never open it", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_first(layout), milliseconds{0});
    gate.tick(milliseconds{60000});

    CHECK_FALSE(gate.is_open());
}

TEST_CASE("a finger outside both spots holds nothing", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, nowhere(), milliseconds{0});
    gate.tick(milliseconds{60000});

    CHECK_FALSE(gate.is_open());
}

TEST_CASE("a third touch, a palm for instance, starts the hold over and keeps it from opening",
          "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    gate.touch_down(3, nowhere(), milliseconds{1500});
    CHECK(gate.state() == GateState::waiting);
    gate.tick(milliseconds{5000});
    CHECK_FALSE(gate.is_open());

    // The palm lifts: the hold begins again from that moment.
    gate.touch_up(3, milliseconds{5000});
    CHECK(gate.state() == GateState::holding);
    gate.tick(milliseconds{6900});
    CHECK_FALSE(gate.is_open());
    gate.tick(milliseconds{7000});
    CHECK(gate.is_open());
}

TEST_CASE("a finger that slides out of its spot starts the hold over", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    gate.touch_move(2, nowhere(), milliseconds{1900});
    CHECK(gate.state() == GateState::waiting);
    gate.touch_move(2, in_second(layout), milliseconds{2000});
    CHECK(gate.state() == GateState::holding);

    gate.tick(milliseconds{3900});
    CHECK_FALSE(gate.is_open());
    gate.tick(milliseconds{4000});
    CHECK(gate.is_open());
}

TEST_CASE("a small wobble inside the spot keeps the hold", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    for (int t = 100; t < 2000; t += 100) {
        gate.touch_move(1, {layout.first.centre.x + 5.0, layout.first.centre.y - 4.0},
                        milliseconds{t});
        gate.touch_move(2, {layout.second.centre.x - 6.0, layout.second.centre.y + 3.0},
                        milliseconds{t});
    }
    gate.tick(milliseconds{2000});

    CHECK(gate.is_open());
}

TEST_CASE("a finger that slides from one spot into the other holds neither", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    gate.touch_move(1, in_second(layout), milliseconds{500});

    CHECK(gate.state() == GateState::waiting);
    gate.tick(milliseconds{30000});
    CHECK_FALSE(gate.is_open());
}

TEST_CASE("a touch that lifts and presses again starts the hold over", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});
    gate.touch_up(2, milliseconds{1800});
    gate.touch_down(2, in_second(layout), milliseconds{1900});

    gate.tick(milliseconds{3800});
    CHECK_FALSE(gate.is_open());
    gate.tick(milliseconds{3900});
    CHECK(gate.is_open());
}

TEST_CASE("a touch the gate never saw moving or lifting is ignored", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    gate.touch_move(99, in_first(layout), milliseconds{0});
    gate.touch_up(99, milliseconds{10});

    CHECK(gate.touches() == 0);
    CHECK(gate.state() == GateState::waiting);
}

TEST_CASE("a touch that goes down twice is one touch", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(1, in_second(layout), milliseconds{0});

    CHECK(gate.touches() == 1);
}

TEST_CASE("an open gate stays open until it is locked again", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});
    gate.tick(milliseconds{2000});
    REQUIRE(gate.is_open());

    gate.touch_up(1, milliseconds{2100});
    gate.touch_up(2, milliseconds{2100});
    CHECK(gate.is_open());

    gate.relock();
    CHECK_FALSE(gate.is_open());
    CHECK(gate.state() == GateState::waiting);
    CHECK(gate.touches() == 0);
}

TEST_CASE("locking again forgets a hold that was going", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    gate.relock();
    gate.tick(milliseconds{9000});

    CHECK_FALSE(gate.is_open());
}

TEST_CASE("assistive technology opens it without a hold", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};

    gate.unlock_for_assistive_technology();

    CHECK(gate.is_open());
    CHECK(gate.state() == GateState::open);
}

TEST_CASE("nothing a touch does opens it without the hold", "[gate]") {
    // The assistive action is the only shortcut: a tap on a spot, a tap on both,
    // a double tap, none of them is a hold.
    const auto layout = ipad_layout();
    Gate gate{layout};
    for (int round = 0; round < 50; ++round) {
        const milliseconds t{round * 300};
        gate.touch_down(1, in_first(layout), t);
        gate.touch_down(2, in_second(layout), t);
        gate.touch_up(1, t + milliseconds{120});
        gate.touch_up(2, t + milliseconds{120});
    }
    CHECK_FALSE(gate.is_open());
}

TEST_CASE("a new layout starts the hold over", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});
    gate.tick(milliseconds{1500});

    gate.set_layout(wjs::gate_layout(768.0, 1024.0, 48.0));

    CHECK(gate.state() == GateState::waiting);
    gate.tick(milliseconds{5000});
    CHECK_FALSE(gate.is_open());
}

TEST_CASE("a hold time of the caller's choosing is respected", "[gate]") {
    const auto layout = ipad_layout();
    Gate gate{layout, milliseconds{500}};
    gate.touch_down(1, in_first(layout), milliseconds{0});
    gate.touch_down(2, in_second(layout), milliseconds{0});

    gate.tick(milliseconds{499});
    CHECK_FALSE(gate.is_open());
    gate.tick(milliseconds{500});
    CHECK(gate.is_open());
}

TEST_CASE("the hold the design asks for is two seconds", "[gate]") {
    CHECK(wjs::gate_hold_time == milliseconds{2000});
}

// A property test against an oracle written separately from the gate. A
// simulated child touches the screen at random: several fingers, in the spots
// and out of them, for short and long times. The oracle walks the same events
// and measures the longest stretch in which exactly two touches were down, one
// in each spot; the gate has to be open at the end exactly when that stretch
// reached two seconds. So nothing but a real hold opens it, and a real hold
// always does.
TEST_CASE("the gate opens exactly when two spots were held for the whole time", "[gate]") {
    const auto layout = ipad_layout();

    struct Event {
        enum Kind { down, move, up } kind;
        int id;
        Point at;
        std::int64_t ms;
    };

    int opened = 0;
    int refused = 0;
    for (std::uint32_t seed = 1; seed <= 400; ++seed) {
        std::mt19937 random(seed);
        // std::mt19937's output is the same on every standard library, and a
        // remainder is arithmetic, so a seed is the same run everywhere; the
        // std::uniform_int_distribution would not be (its algorithm is the
        // library's own).
        const auto pick = [&random](int low, int high) {
            return low + static_cast<int>(random() % static_cast<std::uint32_t>(high - low + 1));
        };
        const auto random_point = [&]() -> Point {
            switch (pick(0, 3)) {
                case 0:
                    return in_first(layout);
                case 1:
                    return in_second(layout);
                default:
                    return {static_cast<double>(pick(0, 1024)), static_cast<double>(pick(0, 768))};
            }
        };

        // Build a run of events: touches come and go, move, and time passes.
        std::vector<Event> events;
        std::vector<int> down;
        std::int64_t now = 0;
        const bool deliberate = pick(0, 3) == 0;  // a quarter of the runs are a real hold
        int next_id = 1;
        if (deliberate) {
            events.push_back({Event::down, next_id, in_first(layout), now});
            down.push_back(next_id++);
            events.push_back({Event::down, next_id, in_second(layout), now});
            down.push_back(next_id++);
            now += pick(1500, 2600);
        }
        for (int step = 0; step < 60; ++step) {
            now += pick(0, 700);
            const int action = pick(0, 9);
            if (action < 4 && down.size() < 5) {
                events.push_back({Event::down, next_id, random_point(), now});
                down.push_back(next_id++);
            } else if (action < 7 && !down.empty()) {
                events.push_back(
                    {Event::move,
                     down[static_cast<std::size_t>(pick(0, static_cast<int>(down.size()) - 1))],
                     random_point(), now});
            } else if (!down.empty()) {
                const auto at =
                    static_cast<std::size_t>(pick(0, static_cast<int>(down.size()) - 1));
                events.push_back({Event::up, down[at], {}, now});
                down.erase(down.begin() + static_cast<std::ptrdiff_t>(at));
            }
        }
        now += pick(0, 3000);

        // The gate.
        Gate gate{layout};
        // The oracle.
        struct Held {
            int id;
            Point at;
        };
        std::vector<Held> held;
        const auto both = [&]() {
            return held.size() == 2 &&
                   ((layout.first.contains(held[0].at) && layout.second.contains(held[1].at)) ||
                    (layout.first.contains(held[1].at) && layout.second.contains(held[0].at)));
        };
        bool was_both = false;
        std::int64_t since = 0;
        std::int64_t longest = 0;
        const auto settle = [&](std::int64_t t) {
            if (was_both) {
                longest = std::max(longest, t - since);
            }
        };

        for (const Event& e : events) {
            // The oracle sees the stretch up to this event with the touches as
            // they were; then the touches change.
            settle(e.ms);
            switch (e.kind) {
                case Event::down:
                    gate.touch_down(e.id, e.at, milliseconds{e.ms});
                    held.push_back({e.id, e.at});
                    break;
                case Event::move:
                    gate.touch_move(e.id, e.at, milliseconds{e.ms});
                    for (Held& h : held) {
                        if (h.id == e.id) {
                            h.at = e.at;
                        }
                    }
                    break;
                case Event::up:
                    gate.touch_up(e.id, milliseconds{e.ms});
                    std::erase_if(held, [&e](const Held& h) { return h.id == e.id; });
                    break;
            }
            const bool now_both = both();
            if (now_both && !was_both) {
                since = e.ms;
            }
            was_both = now_both;
        }
        settle(now);
        gate.tick(milliseconds{now});

        INFO("seed " << seed << ", longest hold " << longest << " ms");
        const bool oracle_open = longest >= 2000;
        REQUIRE(gate.is_open() == oracle_open);
        oracle_open ? ++opened : ++refused;
    }

    // The runs are not all one answer: the test would pass trivially if they were.
    CHECK(opened > 20);
    CHECK(refused > 20);
}
