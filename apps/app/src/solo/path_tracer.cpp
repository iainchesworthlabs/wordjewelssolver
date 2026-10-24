// SPDX-License-Identifier: GPL-3.0-or-later
#include "path_tracer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "wjs/path.hpp"
#include "wjs/profiling.hpp"

namespace wjs::app {
namespace {

[[nodiscard]] double distance(Point a, Point b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

}  // namespace

void PathTracer::setBoard(std::size_t rows, std::size_t columns, std::vector<bool> usable) {
    rows_ = rows;
    columns_ = columns;
    usable_ = std::move(usable);
    path_.clear();
    active_ = false;
    ignored_ = false;
    moved_ = false;
    press_on_last_ = false;
}

void PathTracer::setGeometry(double tile, double gap) {
    tile_ = tile > 0 ? tile : 1;
    gap_ = gap >= 0 ? gap : 0;
}

Point PathTracer::centre(wjs::Position position) const noexcept {
    const double pitch = tile_ + gap_;
    return {static_cast<double>(position.column) * pitch + tile_ / 2,
            static_cast<double>(position.row) * pitch + tile_ / 2};
}

bool PathTracer::usable(wjs::Position position) const {
    if (position.row >= rows_ || position.column >= columns_) {
        return false;
    }
    const std::size_t index = position.row * columns_ + position.column;
    return usable_.empty() || (index < usable_.size() && usable_[index]);
}

bool PathTracer::used(wjs::Position position) const {
    return std::ranges::find(path_, position) != path_.end();
}

bool PathTracer::joins(wjs::Position position) const {
    return !path_.empty() && usable(position) && !used(position) &&
           wjs::are_neighbours(path_.back(), position);
}

std::optional<wjs::Position> PathTracer::nearest(Point at, double within) const {
    std::optional<wjs::Position> best;
    double best_distance = within * tile_;
    for (std::size_t row = 0; row < rows_; ++row) {
        for (std::size_t column = 0; column < columns_; ++column) {
            const wjs::Position position{row, column};
            if (!usable(position)) {
                continue;
            }
            const double d = distance(at, centre(position));
            if (d <= best_distance) {
                best_distance = d;
                best = position;
            }
        }
    }
    return best;
}

PathTracer::Event PathTracer::press(Point at) {
    active_ = true;
    ignored_ = false;
    moved_ = false;
    press_on_last_ = false;
    last_point_ = at;

    const auto hit = nearest(at, tolerance_.press);
    if (!hit) {
        // A press that lands on nothing puts a pending tapped path away.
        ignored_ = true;
        return clear();
    }
    if (path_.empty()) {
        path_.push_back(*hit);
        return Event::changed;
    }
    if (*hit == path_.back()) {
        press_on_last_ = true;
        return Event::none;
    }
    if (path_.size() >= 2 && *hit == path_[path_.size() - 2]) {
        path_.pop_back();
        return Event::changed;
    }
    if (joins(*hit)) {
        path_.push_back(*hit);
        return Event::changed;
    }
    // A non-neighbour, or a tile already used: ignored, not an error.
    ignored_ = true;
    return Event::none;
}

bool PathTracer::step(Point at) {
    if (path_.empty()) {
        return false;
    }
    const double hit = tolerance_.hit * tile_;
    const double stay = tolerance_.stay * tile_;

    if (path_.size() >= 2) {
        const wjs::Position previous = path_[path_.size() - 2];
        if (distance(at, centre(previous)) <= hit && distance(at, centre(path_.back())) > stay) {
            path_.pop_back();
            moved_ = true;
            return true;
        }
    }

    std::optional<wjs::Position> best;
    double best_distance = hit;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) {
                continue;
            }
            const auto& last = path_.back();
            if ((dr < 0 && last.row == 0) || (dc < 0 && last.column == 0)) {
                continue;
            }
            const wjs::Position candidate{
                static_cast<std::size_t>(static_cast<long long>(last.row) + dr),
                static_cast<std::size_t>(static_cast<long long>(last.column) + dc)};
            if (!joins(candidate)) {
                continue;
            }
            const double d = distance(at, centre(candidate));
            if (d <= best_distance) {
                best_distance = d;
                best = candidate;
            }
        }
    }
    if (!best) {
        return false;
    }
    path_.push_back(*best);
    moved_ = true;
    return true;
}

PathTracer::Event PathTracer::move(Point at) {
    if (!active_ || ignored_) {
        return Event::none;
    }
    // One zone per pointer event, not per walked step: a move is the input
    // path's per-event work (the hit tests below, and the path display the
    // change triggers in QML). It nests inside the QML seam's `ui.trace`.
    auto zone = wjs::profiling::zone<"input.trace">();
    bool changed = false;
    const double length = distance(last_point_, at);
    const double step_length = std::max(tolerance_.step * tile_, 1e-6);
    const auto steps = static_cast<std::size_t>(std::ceil(length / step_length));
    for (std::size_t i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const Point sample{last_point_.x + (at.x - last_point_.x) * t,
                           last_point_.y + (at.y - last_point_.y) * t};
        changed = step(sample) || changed;
    }
    last_point_ = at;
    return changed ? Event::changed : Event::none;
}

PathTracer::Event PathTracer::release(Point at, bool inside) {
    if (!active_) {
        return Event::none;
    }
    active_ = false;
    if (ignored_) {
        return Event::none;
    }
    if (!inside) {
        // Lifting outside the board cancels, with no penalty.
        return clear();
    }
    if (moved_) {
        // A trace ends with the finger. Back on its first tile it is a tap's
        // start and waits for the next tile.
        return path_.size() >= 2 ? Event::submit : Event::none;
    }
    // No movement: a tap. The last tile tapped again hands the path in.
    (void)at;
    return press_on_last_ ? Event::submit : Event::none;
}

PathTracer::Event PathTracer::tap(wjs::Position position) {
    if (!usable(position)) {
        return Event::none;
    }
    if (path_.empty()) {
        path_.push_back(position);
        return Event::changed;
    }
    if (position == path_.back()) {
        return Event::submit;
    }
    if (path_.size() >= 2 && position == path_[path_.size() - 2]) {
        path_.pop_back();
        return Event::changed;
    }
    if (joins(position)) {
        path_.push_back(position);
        return Event::changed;
    }
    return Event::none;
}

PathTracer::Event PathTracer::submit() {
    return path_.empty() ? Event::none : Event::submit;
}

PathTracer::Event PathTracer::removeLast() {
    if (path_.empty()) {
        return Event::none;
    }
    path_.pop_back();
    return Event::changed;
}

PathTracer::Event PathTracer::clear() {
    active_ = false;
    moved_ = false;
    press_on_last_ = false;
    if (path_.empty()) {
        return Event::none;
    }
    path_.clear();
    return Event::cancelled;
}

}  // namespace wjs::app
