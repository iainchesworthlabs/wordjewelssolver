// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

// The parental gate (docs/design/dashboard.md, "The gate"; question 49). It
// keeps a child out of the settings and the data by accident. It is not
// security, and nothing here pretends to be.
//
// **Two fingers on two marked spots, held for two seconds.** A five-year-old
// presses things, but rarely with two fingers on two places a hand apart for
// two seconds, and a palm, a whole hand or a third finger spoils it. A parent
// needs both hands and no reading. What this holds down, as logic with no
// screen and no clock of its own:
//
//   - the two spots are far apart and diagonal (gate_layout), so that one
//     finger cannot be on both and a palm falls between them;
//   - exactly two touches, one in each spot, for the whole time: a touch that
//     leaves its spot, a lifted finger or a third touch starts the hold over;
//   - the time is whatever the caller says it is. The gate has no timer: it is
//     told when each touch moves and when time has passed, and it answers from
//     those. The screen asks again when its own clock ticks, so the ring fills
//     smoothly, but a stopped clock cannot open it and a late one only opens
//     it late;
//   - a screen-reader user, who cannot hold two spots blind, is given the same
//     screen through unlock_for_assistive_technology(), called from the
//     accessibility action of the screen's control and from nowhere else. A
//     touch that is a tap never calls it.
//
// Which age cannot pass it is **not established** by anything in the
// repository: the gate is to be tried with children (dashboard.md).

namespace wjs {

struct Point {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Point&, const Point&) = default;
};

struct Spot {
    Point centre;
    double radius = 0.0;

    [[nodiscard]] bool contains(Point p) const noexcept;

    friend bool operator==(const Spot&, const Spot&) = default;
};

struct GateLayout {
    Spot first;
    Spot second;

    friend bool operator==(const GateLayout&, const GateLayout&) = default;
};

// How long both spots are held.
inline constexpr std::chrono::milliseconds gate_hold_time{2000};

// The most touches the gate follows. A hand on the glass is more than this, and
// more than two touches never open the gate.
inline constexpr std::size_t gate_max_touches = 32;

// Two spots on a screen of this size: on the diagonal, a quarter of the way in
// from each side, so that they are half the width apart and the height between
// them is a quarter of it. The radius is `min_radius` (the caller's 48 dp, in
// the screen's own units) or a ninth of the shorter side, whichever is larger.
[[nodiscard]] GateLayout gate_layout(double width, double height, double min_radius) noexcept;

// Whether a layout is one the gate will work with: both spots wholly on the
// screen, and their centres at least four radii apart (further than a hand
// that is flat on the glass reaches from one to the other).
[[nodiscard]] bool gate_layout_is_usable(const GateLayout& layout, double width,
                                         double height) noexcept;

enum class GateState {
    waiting,  // not both spots held
    holding,  // both held; progress() says how far
    open,     // held for the time, or opened for assistive technology
};

class Gate {
public:
    using Time = std::chrono::milliseconds;

    explicit Gate(GateLayout layout, Time hold = gate_hold_time);

    // A touch begins, moves or ends. `id` is the system's id for the touch, the
    // same through its life. A touch the gate does not know of moving or ending
    // is ignored.
    void touch_down(int id, Point at, Time now);
    void touch_move(int id, Point at, Time now);
    void touch_up(int id, Time now);

    // Time has passed with nothing else happening.
    void tick(Time now);

    // The accessibility action of the gate's control: opens it. A hold cannot
    // be done by a person who cannot see the spots (WCAG 2.5.1 gives a way
    // without a path or a hold).
    void unlock_for_assistive_technology() noexcept;

    // Closes it and forgets every touch: when the parent area is left.
    void relock() noexcept;

    // The screen changed size or turned: new spots, and the hold starts over.
    void set_layout(GateLayout layout) noexcept;

    [[nodiscard]] GateState state() const noexcept;
    [[nodiscard]] bool is_open() const noexcept { return open_; }
    [[nodiscard]] const GateLayout& layout() const noexcept { return layout_; }

    // How much of the hold has gone, from 0 to 1, as of `now`. 1 once open.
    [[nodiscard]] double progress(Time now) const noexcept;

    // How many touches are down, and whether both spots hold exactly one.
    [[nodiscard]] std::size_t touches() const noexcept { return touches_.size(); }
    [[nodiscard]] bool both_spots_held() const noexcept;

private:
    struct Touch {
        int id = 0;
        Point at;
    };

    void refresh(Time now) noexcept;

    GateLayout layout_;
    Time hold_;
    std::vector<Touch> touches_;
    std::optional<Time> held_since_;
    bool open_ = false;
};

}  // namespace wjs
