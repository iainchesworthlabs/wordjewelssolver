// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "path_tracer.hpp"
#include "wjs/board.hpp"

namespace {

using wjs::Position;
using wjs::app::PathTracer;
using wjs::app::Point;
using Event = PathTracer::Event;

// A 3 x 3 board of tiles 100 wide with 10 between them: the centre of the tile
// at (row, column) is (50 + 110 * column, 50 + 110 * row).
constexpr double tile = 100;
constexpr double gap = 10;

Point centre(std::size_t row, std::size_t column) {
    return {50 + 110.0 * static_cast<double>(column), 50 + 110.0 * static_cast<double>(row)};
}

PathTracer make_tracer(std::vector<bool> usable = {}) {
    PathTracer tracer;
    tracer.setBoard(3, 3, std::move(usable));
    tracer.setGeometry(tile, gap);
    return tracer;
}

// The tiles of a path as "row column" pairs, for a CHECK that says what it saw.
using Tiles = std::vector<std::pair<std::size_t, std::size_t>>;

std::string shown(const PathTracer& tracer) {
    std::string text;
    for (const auto& position : tracer.path()) {
        text += "(" + std::to_string(position.row) + "," + std::to_string(position.column) + ")";
    }
    return text;
}

std::string tiles_of(const Tiles& tiles) {
    std::string text;
    for (const auto& [row, column] : tiles) {
        text += "(" + std::to_string(row) + "," + std::to_string(column) + ")";
    }
    return text;
}

// Slides the pointer in a straight line, in steps a finger's events would make.
void slide(PathTracer& tracer, Point from, Point to, int steps = 10) {
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        tracer.move({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t});
    }
}

}  // namespace

TEST_CASE("a press on a tile starts a path and a slide across neighbours extends it",
          "[app][trace]") {
    auto tracer = make_tracer();

    CHECK(tracer.press(centre(0, 0)) == Event::changed);
    slide(tracer, centre(0, 0), centre(0, 1));
    slide(tracer, centre(0, 1), centre(0, 2));

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {0, 2}}));
    // Lifting the finger over the board hands the path in.
    CHECK(tracer.release(centre(0, 2), true) == Event::submit);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {0, 2}}));
}

TEST_CASE("a diagonal slide takes the diagonal tile and skips neither side", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));

    slide(tracer, centre(0, 0), centre(1, 1));
    slide(tracer, centre(1, 1), centre(2, 2));

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {1, 1}, {2, 2}}));
}

TEST_CASE("a fast move that jumps over a tile still takes it", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));

    // One event from the first column to the third: the walk takes the middle.
    tracer.move(centre(0, 2));

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {0, 2}}));
}

TEST_CASE("the hit area is smaller than the tile, so a finger near an edge adds nothing",
          "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));

    // At the edge between the first and second tile, a few dp into the second.
    tracer.move({centre(0, 0).x + 60, centre(0, 0).y});
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}}));

    // Well inside it, the tile joins.
    tracer.move({centre(0, 1).x - 30, centre(0, 1).y});
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}}));
}

TEST_CASE("sliding back onto the previous tile takes the last tile off", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    slide(tracer, centre(0, 0), centre(0, 1));
    slide(tracer, centre(0, 1), centre(0, 2));
    REQUIRE(tracer.path().size() == 3);

    slide(tracer, centre(0, 2), centre(0, 1));

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}}));
}

TEST_CASE("a tile cannot be used twice and a non-neighbour is ignored", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    slide(tracer, centre(0, 0), centre(0, 1));
    slide(tracer, centre(0, 1), centre(1, 1));
    slide(tracer, centre(1, 1), centre(1, 0));

    // Back across the first tile: it is used, so it is not added again.
    slide(tracer, centre(1, 0), centre(0, 0));
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {1, 1}, {1, 0}}));

    // A tile that does not touch the last is not added and is not an error. (The
    // finger comes back to the last tile first, so it does not pass over the
    // previous one, which would take the last off.)
    slide(tracer, centre(0, 0), centre(1, 0));
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {1, 1}, {1, 0}}));
    tracer.move(centre(2, 2));
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {1, 1}, {1, 0}}));
}

TEST_CASE("a dead jewel is never part of a path and a press on it does nothing", "[app][trace]") {
    // The middle tile is a dead jewel.
    std::vector<bool> usable(9, true);
    usable[4] = false;
    auto tracer = make_tracer(usable);

    CHECK(tracer.press(centre(1, 1)) == Event::none);
    CHECK(tracer.path().empty());
    CHECK(tracer.release(centre(1, 1), true) == Event::none);

    tracer.press(centre(0, 0));
    slide(tracer, centre(0, 0), centre(1, 1));
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}}));
}

TEST_CASE("lifting outside the board cancels with nothing handed in", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    slide(tracer, centre(0, 0), centre(0, 1));

    CHECK(tracer.release({-80, 20}, false) == Event::cancelled);
    CHECK(tracer.path().empty());
}

TEST_CASE("tapping tiles in order builds a path and the last tile again hands it in",
          "[app][trace]") {
    auto tracer = make_tracer();

    for (const auto& [row, column] : Tiles{{2, 0}, {1, 0}, {0, 1}}) {
        tracer.press(centre(row, column));
        CHECK(tracer.release(centre(row, column), true) == Event::none);
    }
    CHECK(shown(tracer) == tiles_of(Tiles{{2, 0}, {1, 0}, {0, 1}}));

    // Tap the last tile again.
    tracer.press(centre(0, 1));
    CHECK(tracer.release(centre(0, 1), true) == Event::submit);
    CHECK(shown(tracer) == tiles_of(Tiles{{2, 0}, {1, 0}, {0, 1}}));
}

TEST_CASE("a tap on a tile that does not touch the path is ignored", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    tracer.release(centre(0, 0), true);

    tracer.press(centre(2, 2));
    CHECK(tracer.release(centre(2, 2), true) == Event::none);

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}}));
}

TEST_CASE("a tap on the previous tile takes the last off, as sliding back does", "[app][trace]") {
    auto tracer = make_tracer();
    for (const auto& [row, column] : Tiles{{0, 0}, {0, 1}, {0, 2}}) {
        tracer.press(centre(row, column));
        tracer.release(centre(row, column), true);
    }

    tracer.press(centre(0, 1));
    tracer.release(centre(0, 1), true);

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}}));
}

TEST_CASE("a trace may carry on from a tapped path", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    tracer.release(centre(0, 0), true);

    // Press the neighbour and slide on from there: handed in at the lift.
    tracer.press(centre(0, 1));
    slide(tracer, centre(0, 1), centre(0, 2));

    CHECK(tracer.release(centre(0, 2), true) == Event::submit);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {0, 2}}));
}

TEST_CASE("a press that lands on nothing puts a waiting path away", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    tracer.release(centre(0, 0), true);

    // Far outside the board.
    CHECK(tracer.press({-500, -500}) == Event::cancelled);
    CHECK(tracer.path().empty());
}

TEST_CASE("a press in the gap between tiles still picks the nearest tile", "[app][trace]") {
    auto tracer = make_tracer();

    // The middle of the gap between the first two tiles, a little nearer the first.
    tracer.press({centre(0, 0).x + 52, centre(0, 0).y});

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}}));
}

TEST_CASE("the keyboard and the screen reader make the same moves", "[app][trace]") {
    auto tracer = make_tracer();

    CHECK(tracer.tap({0, 0}) == Event::changed);
    CHECK(tracer.tap({0, 1}) == Event::changed);
    CHECK(tracer.tap({2, 2}) == Event::none);  // does not touch the last tile
    CHECK(tracer.tap({1, 1}) == Event::changed);
    CHECK(tracer.tap({0, 0}) == Event::none);  // used already, and not the last
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {1, 1}}));

    // Tapping the previous tile takes the last off.
    CHECK(tracer.tap({0, 1}) == Event::changed);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}}));

    // The last tile again, or Enter, hands it in.
    CHECK(tracer.tap({0, 1}) == Event::submit);
    CHECK(tracer.submit() == Event::submit);

    CHECK(tracer.removeLast() == Event::changed);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}}));
    CHECK(tracer.clear() == Event::cancelled);
    CHECK(tracer.path().empty());
    CHECK(tracer.submit() == Event::none);
    CHECK(tracer.removeLast() == Event::none);
    CHECK(tracer.clear() == Event::none);
}

TEST_CASE("a tile outside the board or a dead jewel cannot be tapped", "[app][trace]") {
    std::vector<bool> usable(9, true);
    usable[0] = false;
    auto tracer = make_tracer(usable);

    CHECK(tracer.tap({0, 0}) == Event::none);
    CHECK(tracer.tap({3, 3}) == Event::none);
    CHECK(tracer.path().empty());
}

TEST_CASE("a path of one tile slid back to is not handed in", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    slide(tracer, centre(0, 0), centre(0, 1));
    slide(tracer, centre(0, 1), centre(0, 0));
    REQUIRE(shown(tracer) == tiles_of(Tiles{{0, 0}}));

    // It waits, as a tap's first tile does.
    CHECK(tracer.release(centre(0, 0), true) == Event::none);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}}));
}

TEST_CASE("only the pointer that is down moves the path", "[app][trace]") {
    auto tracer = make_tracer();

    CHECK(tracer.move(centre(0, 0)) == Event::none);
    CHECK(tracer.release(centre(0, 0), true) == Event::none);
    CHECK_FALSE(tracer.pointerDown());

    tracer.press(centre(0, 0));
    CHECK(tracer.pointerDown());
    tracer.release(centre(0, 0), true);
    CHECK_FALSE(tracer.pointerDown());
}

TEST_CASE("a move that stays on the tile reports nothing to rebuild", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));

    // A wobble inside the stay circle, and the centre itself: no new tile, so
    // nothing for the screen to redraw (the model tells the path per change,
    // and these are no change).
    CHECK(tracer.move({centre(0, 0).x + 5, centre(0, 0).y}) == Event::none);
    CHECK(tracer.move(centre(0, 0)) == Event::none);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}}));
}

TEST_CASE("a move to a far tile walks the tiles it crosses", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));

    // Two tiles away in a straight line: the walk adds the middle tile (one
    // change), and nothing else.
    CHECK(tracer.move(centre(0, 2)) == Event::changed);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {0, 2}}));

    // The corner is two tiles away: the walk crosses (1,2), which touches the
    // last tile, and then the corner, so both join in order.
    CHECK(tracer.move(centre(2, 2)) == Event::changed);
    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}}));
}

TEST_CASE("a rotated window keeps the path and uses the new geometry", "[app][trace]") {
    auto tracer = make_tracer();
    tracer.press(centre(0, 0));
    tracer.release(centre(0, 0), true);

    // Tiles half the size: the path stays, and a tap on the new second tile joins.
    tracer.setGeometry(50, 5);
    REQUIRE(tracer.path().size() == 1);
    tracer.press({25 + 55.0, 25});
    tracer.release({25 + 55.0, 25}, true);

    CHECK(shown(tracer) == tiles_of(Tiles{{0, 0}, {0, 1}}));
}
