// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/parent_gate.hpp"

#include <algorithm>
#include <cstddef>

#include "wjs/profiling.hpp"

namespace wjs {

bool Spot::contains(Point p) const noexcept {
    const double dx = p.x - centre.x;
    const double dy = p.y - centre.y;
    return dx * dx + dy * dy <= radius * radius;
}

GateLayout gate_layout(double width, double height, double min_radius) noexcept {
    const double radius = std::max(min_radius, std::min(width, height) / 9.0);
    return {{{width * 0.25, height * 0.375}, radius}, {{width * 0.75, height * 0.625}, radius}};
}

bool gate_layout_is_usable(const GateLayout& layout, double width, double height) noexcept {
    const auto on_screen = [width, height](const Spot& spot) {
        return spot.radius > 0.0 && spot.centre.x - spot.radius >= 0.0 &&
               spot.centre.y - spot.radius >= 0.0 && spot.centre.x + spot.radius <= width &&
               spot.centre.y + spot.radius <= height;
    };
    if (!on_screen(layout.first) || !on_screen(layout.second)) {
        return false;
    }
    const double dx = layout.first.centre.x - layout.second.centre.x;
    const double dy = layout.first.centre.y - layout.second.centre.y;
    const double apart = 4.0 * std::max(layout.first.radius, layout.second.radius);
    return dx * dx + dy * dy >= apart * apart;
}

Gate::Gate(GateLayout layout, Time hold) : layout_(layout), hold_(hold) {}

bool Gate::both_spots_held() const noexcept {
    if (touches_.size() != 2) {
        return false;
    }
    const Point a = touches_[0].at;
    const Point b = touches_[1].at;
    return (layout_.first.contains(a) && layout_.second.contains(b)) ||
           (layout_.first.contains(b) && layout_.second.contains(a));
}

void Gate::refresh(Time now) noexcept {
    auto zone = profiling::zone<"gate.refresh">();

    if (open_) {
        return;
    }
    if (!both_spots_held()) {
        held_since_.reset();
        return;
    }
    if (!held_since_) {
        held_since_ = now;
        return;
    }
    // A clock that went backwards is not a hold that has been going on.
    if (now < *held_since_) {
        held_since_ = now;
        return;
    }
    if (now - *held_since_ >= hold_) {
        open_ = true;
    }
}

void Gate::touch_down(int id, Point at, Time now) {
    refresh(now);  // the hold so far, with the touches it was made with
    const auto known =
        std::find_if(touches_.begin(), touches_.end(), [id](const Touch& t) { return t.id == id; });
    if (known != touches_.end()) {
        known->at = at;
    } else if (touches_.size() < gate_max_touches) {
        touches_.push_back({id, at});
    }
    refresh(now);
}

void Gate::touch_move(int id, Point at, Time now) {
    refresh(now);
    const auto known =
        std::find_if(touches_.begin(), touches_.end(), [id](const Touch& t) { return t.id == id; });
    if (known != touches_.end()) {
        known->at = at;
    }
    refresh(now);
}

void Gate::touch_up(int id, Time now) {
    refresh(now);
    std::erase_if(touches_, [id](const Touch& t) { return t.id == id; });
    refresh(now);
}

void Gate::tick(Time now) {
    refresh(now);
}

void Gate::unlock_for_assistive_technology() noexcept {
    open_ = true;
}

void Gate::relock() noexcept {
    open_ = false;
    touches_.clear();
    held_since_.reset();
}

void Gate::set_layout(GateLayout layout) noexcept {
    layout_ = layout;
    held_since_.reset();
}

GateState Gate::state() const noexcept {
    if (open_) {
        return GateState::open;
    }
    return held_since_ ? GateState::holding : GateState::waiting;
}

double Gate::progress(Time now) const noexcept {
    if (open_) {
        return 1.0;
    }
    if (!held_since_ || hold_.count() <= 0) {
        return 0.0;
    }
    const double elapsed = static_cast<double>((now - *held_since_).count());
    return std::clamp(elapsed / static_cast<double>(hold_.count()), 0.0, 1.0);
}

}  // namespace wjs
