// SPDX-License-Identifier: GPL-3.0-or-later
// Finding the board in a frame, drawing it square, and counting its cells, on
// frames from the synthetic corpus and on drawn combs and squares.

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "fixtures.hpp"
#include "wjs/scan/board_finder.hpp"
#include "wjs/scan/image.hpp"

namespace {

using namespace wjs::scan;
using namespace wjs::scan::synth;
using Catch::Approx;

// How far the found corners are from the true ones: each found corner against the
// nearest true corner, the worst of the four, in pixels.
double worst_corner_error(const Quad& found, const Quad& truth) {
    double worst = 0.0;
    for (const Point& corner : found) {
        double nearest = std::numeric_limits<double>::max();
        for (const Point& real : truth) {
            nearest = std::min(nearest, distance(corner, real));
        }
        worst = std::max(worst, nearest);
    }
    return worst;
}

// The finder on a frame as the reader runs it: shrunk to 480 on the long side, with
// the outline brought back to the frame's pixels.
std::optional<BoardFind> find_in_frame(const GrayImage& image) {
    int factor = 1;
    const GrayImage working = shrink(image, 480, factor);
    auto found = find_board(working);
    if (found) {
        for (Point& corner : found->corners) {
            corner = corner * static_cast<double>(factor);
        }
        found->die_side *= static_cast<double>(factor);
    }
    return found;
}

}  // namespace

TEST_CASE("the board is found in a frame of each condition, at its true corners",
          "[scan][finder]") {
    struct Case {
        Condition condition;
        std::size_t size;
        unsigned index;
    };
    for (const Case& c : {Case{Condition::upright, 4, 1}, Case{Condition::angled, 5, 13},
                          Case{Condition::loose, 6, 19}, Case{Condition::glare, 4, 3}}) {
        const Rendered& frame = fixtures::tuning_frame(c.condition, c.size, c.index);
        const auto found = find_in_frame(frame.image);
        INFO(name(c.condition) << " " << c.size << "x" << c.size << " #" << c.index);
        REQUIRE(found.has_value());
        // Within 1.5% of the board's own width, which is under a pixel of the working
        // picture.
        const double board_width = distance(frame.corners[0], frame.corners[1]);
        CHECK(worst_corner_error(found->corners, frame.corners) <= 0.015 * board_width + 4.0);
        // The dice it found are the board's, give or take a stray or a lost one.
        CHECK(found->dice + 2 >= c.size * c.size);
        CHECK(found->dice <= c.size * c.size + 1);
        CHECK(found->die_side > 0.0);
        CHECK(found->size_estimate == Approx(static_cast<double>(c.size)).margin(0.6));
        // Clockwise on the screen.
        CHECK(signed_area(found->corners) > 0.0);
    }
}

TEST_CASE("a bright rectangle beside the board, a die's size and a pitch from it, is not the board",
          "[scan][finder]") {
    // A sheet of paper that lands next to the dice is grouped with them by size and
    // spacing; the outline is then held to what a board is, and the stray is let go.
    const Rendered& frame = fixtures::tuning_frame(Condition::upright, 4, 1);
    GrayImage image = frame.image;
    double right = 0.0;
    double top = 1e9;
    double bottom = 0.0;
    for (const Point& corner : frame.corners) {
        right = std::max(right, corner.x);
        top = std::min(top, corner.y);
        bottom = std::max(bottom, corner.y);
    }
    const double die = distance(frame.corners[0], frame.corners[1]) / 4.5;
    const int x0 = static_cast<int>(right + 0.9 * die);
    const int y0 = static_cast<int>(top + 0.5 * (bottom - top) - 0.5 * die);
    REQUIRE(x0 + static_cast<int>(die) < image.width);
    for (int y = y0; y < y0 + static_cast<int>(die); ++y) {
        for (int x = x0; x < x0 + static_cast<int>(die); ++x) {
            image.at(x, y) = 235;
        }
    }

    const auto found = find_in_frame(image);
    REQUIRE(found.has_value());
    const double board_width = distance(frame.corners[0], frame.corners[1]);
    CHECK(worst_corner_error(found->corners, frame.corners) <= 0.02 * board_width + 4.0);
}

TEST_CASE("the cells are counted from the comb of the board drawn square", "[scan][finder]") {
    struct Case {
        Condition condition;
        std::size_t size;
        unsigned index;
    };
    for (const Case& c : {Case{Condition::upright, 4, 2}, Case{Condition::angled, 5, 14},
                          Case{Condition::loose, 6, 20}}) {
        const Rendered& frame = fixtures::tuning_frame(c.condition, c.size, c.index);
        const GrayImage board = warp_to_square(frame.image, frame.corners, 480);
        const auto count = count_cells(board, 3, 8);
        INFO(c.size << "x" << c.size);
        REQUIRE(count.has_value());
        CHECK(count->size == c.size);
        CHECK(count->confidence > 0.3);

        // The scores are one a size from 3 to 8, and the right size has the best.
        const std::vector<double> scores = comb_scores(board, 3, 8);
        REQUIRE(scores.size() == 6);
        CHECK(std::ranges::max_element(scores) - scores.begin() ==
              static_cast<std::ptrdiff_t>(c.size) - 3);
    }
}

TEST_CASE("what the finder knows narrows the count", "[scan][finder]") {
    const Rendered& frame = fixtures::tuning_frame(Condition::upright, 4, 2);
    const GrayImage board = warp_to_square(frame.image, frame.corners, 480);
    // With 16 dice found, no count under 4 fits.
    const auto with_dice = count_cells(board, 3, 8, 16);
    REQUIRE(with_dice.has_value());
    CHECK(with_dice->size == 4);
    // The estimate of the size from the outline narrows the count to within 0.8 of it:
    // right, it changes nothing; wrong, it cannot be answered with the right size.
    const auto with_estimate = count_cells(board, 3, 8, 0, 4.1);
    REQUIRE(with_estimate.has_value());
    CHECK(with_estimate->size == 4);
    const auto wrong = count_cells(board, 3, 8, 0, 6.5);
    CHECK((!wrong.has_value() || wrong->size != 4));
}

TEST_CASE("a picture with no comb has no cell count", "[scan][finder]") {
    CHECK_FALSE(count_cells(GrayImage(240, 240, 128), 3, 8).has_value());
    CHECK_FALSE(count_cells(GrayImage{}, 3, 8).has_value());
}

TEST_CASE("the centres of the dice are found from the gaps between them", "[scan][finder]") {
    // A board drawn square as N dice with gaps of 11 pixels between, the outer dice
    // touching the sides.
    for (const std::size_t n : {std::size_t{4}, std::size_t{5}, std::size_t{6}}) {
        constexpr int kSide = 480;
        constexpr int kGap = 11;
        const double die = (kSide - static_cast<double>(n - 1) * kGap) / static_cast<double>(n);
        GrayImage board(kSide, kSide, 40);
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                const double pitch = die + kGap;
                const double fx = std::fmod(x + 0.5, pitch);
                const double fy = std::fmod(y + 0.5, pitch);
                if (fx < die && fy < die) {
                    board.at(x, y) = 200;
                }
            }
        }
        const CellCentres centres = locate_cells(board, n);
        REQUIRE(centres.x.size() == n);
        REQUIRE(centres.y.size() == n);
        for (std::size_t k = 0; k < n; ++k) {
            const double expected = static_cast<double>(k) * (die + kGap) + die / 2.0;
            INFO(n << " cells, cell " << k);
            CHECK(centres.x[k] == Approx(expected).margin(1.5));
            CHECK(centres.y[k] == Approx(expected).margin(1.5));
        }
    }
}

TEST_CASE("centres of a board with fewer than three cells are the even split", "[scan][finder]") {
    const CellCentres centres = locate_cells(GrayImage(100, 100, 128), 2);
    REQUIRE(centres.x.size() == 2);
    CHECK(centres.x[0] == Approx(25.0));
    CHECK(centres.x[1] == Approx(75.0));
    CHECK(locate_cells(GrayImage{}, 4).x.empty());
}

TEST_CASE("the cell count and the cell centres share one computation of the profiles",
          "[scan][finder]") {
    // The reader computes the board's profiles once and passes them to both, so
    // the shared forms must answer exactly as the forms that compute their own.
    const Rendered& frame = fixtures::tuning_frame(Condition::upright, 4, 2);
    const GrayImage board = warp_to_square(frame.image, frame.corners, 480);

    const BoardProfiles profiles = profiles_of(board);
    const auto count = count_cells(board, profiles, 3, 8);
    const auto alone = count_cells(board, 3, 8);
    REQUIRE(count.has_value());
    REQUIRE(alone.has_value());
    CHECK(count->size == alone->size);
    CHECK(count->confidence == alone->confidence);

    const CellCentres shared = locate_cells(board, profiles, count->size);
    const CellCentres own = locate_cells(board, count->size);
    REQUIRE(shared.x.size() == count->size);
    CHECK(shared.x == own.x);
    CHECK(shared.y == own.y);

    // The profiles are the board's own dimensions and finite: the sharing is
    // about handing the one computation to both consumers, and neither may
    // receive garbage.
    REQUIRE(profiles.columns.size() == 480);
    REQUIRE(profiles.rows.size() == 480);
    CHECK(std::ranges::all_of(profiles.columns, [](double v) { return std::isfinite(v); }));
    CHECK(std::ranges::all_of(profiles.rows, [](double v) { return std::isfinite(v); }));
    CHECK(std::ranges::all_of(profiles.columns, [](double v) { return v >= 0.0; }));
    CHECK(std::ranges::all_of(profiles.rows, [](double v) { return v >= 0.0; }));
}

TEST_CASE("drawing a board square puts its first corner at the top left", "[scan][finder]") {
    // A picture with a bright square at its top right.
    GrayImage image(100, 100, 30);
    for (int y = 10; y < 30; ++y) {
        for (int x = 70; x < 90; ++x) {
            image.at(x, y) = 220;
        }
    }
    const Quad whole = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    const GrayImage same = warp_to_square(image, whole, 50);
    CHECK(same.at(40, 10) > 200);  // still at the top right, at half the size
    CHECK(same.at(10, 10) < 60);

    // The same corners with the first one moved on: the output's top left is the
    // picture's top right, so the picture is turned a quarter and the square is now at
    // the top left.
    const Quad turned = {{{100, 0}, {100, 100}, {0, 100}, {0, 0}}};
    const GrayImage quarter = warp_to_square(image, turned, 50);
    CHECK(quarter.at(10, 10) > 200);
    CHECK(quarter.at(40, 10) < 60);
}

TEST_CASE("a frame with nothing on it has no board", "[scan][finder]") {
    CHECK_FALSE(find_board(GrayImage(480, 360, 128)).has_value());
    CHECK_FALSE(find_board(GrayImage(8, 8, 128)).has_value());
    CHECK_FALSE(find_board(GrayImage{}).has_value());

    // A gradient of light and nothing else.
    GrayImage ramp(480, 360);
    for (int y = 0; y < 360; ++y) {
        for (int x = 0; x < 480; ++x) {
            ramp.at(x, y) = static_cast<std::uint8_t>(60 + x / 4);
        }
    }
    CHECK_FALSE(find_board(ramp).has_value());
}

TEST_CASE("a room with a sheet of paper in it is not a board", "[scan][finder]") {
    for (unsigned i = 0; i < 3; ++i) {
        const GrayImage room = render_negative(NegativeKind::clutter, 200 + i);
        CHECK_FALSE(find_in_frame(room).has_value());
    }
}

TEST_CASE("the profiles of a picture with nothing in it are empty, not a dereference",
          "[scan][finder]") {
    const BoardProfiles none = profiles_of(GrayImage{});
    CHECK(none.columns.empty());
    CHECK(none.rows.empty());
    const BoardProfiles no_rows = profiles_of(GrayImage(8, 0));
    CHECK(no_rows.rows.empty());
    CHECK(locate_cells(GrayImage{}, none, 4).x.empty());
    CHECK_FALSE(count_cells(GrayImage{}, none, 3, 8).has_value());
}

TEST_CASE("a comb needs two cells to have a seam", "[scan][finder]") {
    // A count of 0 or 1 has no seams, and the score is a mean over none of them.
    const GrayImage flat(120, 120, 128);
    CHECK(comb_scores(flat, 0, 4).empty());
    CHECK(comb_scores(flat, 1, 4).empty());
    CHECK(comb_scores(flat, 5, 4).empty());
    CHECK(comb_scores(flat, 2, 4).size() == 3);
    for (const double score : comb_scores(flat, 2, 8)) {
        CHECK(std::isfinite(score));
    }
}

TEST_CASE("a board is not drawn at a size that is not one", "[scan][finder]") {
    const GrayImage source(100, 100, 90);
    const Quad corners = {{{10, 10}, {90, 10}, {90, 90}, {10, 90}}};
    CHECK(warp_to_square(source, corners, 0).empty());
    CHECK(warp_to_square(source, corners, -5).empty());
    CHECK(warp_to_square(source, corners, 0).width == 0);
    CHECK(warp_to_square(source, corners, 1 << 20).empty());
    CHECK(warp_to_square(source, corners, 20).width == 20);
    // Corners that are not places draw nothing the picture does not hold: black.
    Quad bad = corners;
    bad[1].x = std::numeric_limits<double>::quiet_NaN();
    const GrayImage black = warp_to_square(source, bad, 8);
    CHECK(black.width == 8);
    for (const std::uint8_t v : black.pixels) {
        CHECK(v == 0);
    }
}
