// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "wjs/board.hpp"

// Trace and tap in order, both always on (docs/design/game.md, "Tracing and
// tapping"): the touch half of making a word. It turns pointer positions into
// a path of touching tiles and says when the path is finished. It knows no
// word and no sound; what the path spells is the game's to say
// (wjs::validate_path and the session), and what it looks like is QML's.
//
//   Trace  press a tile and slide across neighbours, diagonals included; lift
//          the finger to hand the path in. Sliding back onto the previous tile
//          takes the last tile off. Lifting outside the board cancels, with no
//          penalty. A tile cannot be used twice, a non-neighbour is ignored.
//   Tap    tap a tile, then each neighbour in turn; hand the path in by
//          tapping the last tile again, or with the Done jewel (submit()).
//   Both   a trace may continue a tapped path and the reverse.
//
// The hit area of a tile is a circle smaller than the tile (`hit`), and the
// path stays on a tile until the finger is farther from it than a larger
// circle (`stay`), so a diagonal slide does not skip a tile and a wobble near an
// edge does not flicker. Fast moves are walked in small steps, so no tile is
// jumped. All of these are starting values for a small finger and are the one
// thing a unit test cannot settle (docs/design/game.md); they are in tile
// widths, 1 being a tile.

namespace wjs::app {

struct Point {
    double x = 0;
    double y = 0;
};

struct TraceTolerance {
    // A press picks the nearest tile when it is within this of its centre.
    // Loose on purpose: a press in a gap, or near an edge, still counts.
    double press = 0.75;
    // Sliding, a tile joins when the finger is within this of its centre.
    double hit = 0.42;
    // Sliding back, the last tile leaves only once the finger is farther than
    // this from it.
    double stay = 0.60;
    // A fast move is walked in steps no longer than this.
    double step = 0.2;
};

class PathTracer {
public:
    enum class Event {
        none,       // nothing the screen needs to redraw
        changed,    // the path changed
        submit,     // hand path() to the game; the caller then calls clear()
        cancelled,  // the path was thrown away
    };

    // The board's shape and which tiles can be part of a path (a dead jewel
    // cannot). `usable` is row-major, or empty for all of them. Clears the path.
    void setBoard(std::size_t rows, std::size_t columns, std::vector<bool> usable = {});

    // The tiles' size and the gap between them, in pixels, with the origin at the
    // board's top-left corner. Does not clear the path: the window may rotate.
    void setGeometry(double tile, double gap);

    [[nodiscard]] const TraceTolerance& tolerance() const noexcept { return tolerance_; }
    void setTolerance(const TraceTolerance& tolerance) noexcept { tolerance_ = tolerance; }

    // Pointer input. Only the first touch draws: the caller sends one pointer.
    Event press(Point at);
    Event move(Point at);
    // `inside` is whether the finger lifted over the board (with some slack).
    Event release(Point at, bool inside);

    // Input with no pointer: a keyboard's Space, a screen reader's activation.
    // The same as tapping the tile.
    Event tap(wjs::Position position);
    // The Done jewel, Enter: hand the path in if there is one.
    Event submit();
    // Backspace: take the last tile off.
    Event removeLast();
    // Escape, or the end of a round: throw the path away.
    Event clear();

    [[nodiscard]] const std::vector<wjs::Position>& path() const noexcept { return path_; }
    [[nodiscard]] bool pointerDown() const noexcept { return active_; }

    // The centre of a tile in pixels, and the tile nearest to a point.
    [[nodiscard]] Point centre(wjs::Position position) const noexcept;
    [[nodiscard]] std::optional<wjs::Position> nearest(Point at, double within) const;

private:
    [[nodiscard]] bool usable(wjs::Position position) const;
    [[nodiscard]] bool used(wjs::Position position) const;
    [[nodiscard]] bool joins(wjs::Position position) const;
    bool step(Point at);

    std::size_t rows_ = 0;
    std::size_t columns_ = 0;
    std::vector<bool> usable_;
    double tile_ = 1;
    double gap_ = 0;
    TraceTolerance tolerance_;

    std::vector<wjs::Position> path_;
    bool active_ = false;
    bool ignored_ = false;        // the press hit nothing, or a non-neighbour
    bool moved_ = false;          // the finger added or took off a tile since pressing
    bool press_on_last_ = false;  // the press landed on the path's last tile
    Point last_point_;
};

}  // namespace wjs::app
