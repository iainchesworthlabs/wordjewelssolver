// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry: hulls, the four-sided outline of a convex shape, which corner is first,
// and the projective map from a square to the board in the picture.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <vector>

#include "wjs/scan/geometry.hpp"

namespace {

using namespace wjs::scan;
using Catch::Approx;

bool near(Point a, Point b, double tolerance = 1e-6) {
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
}

}  // namespace

TEST_CASE("the signed area is positive for points clockwise on a screen", "[scan][geometry]") {
    const std::vector<Point> clockwise = {{0, 0}, {4, 0}, {4, 3}, {0, 3}};
    CHECK(signed_area(clockwise) == Approx(12.0));
    const std::vector<Point> counter = {{0, 0}, {0, 3}, {4, 3}, {4, 0}};
    CHECK(signed_area(counter) == Approx(-12.0));
    CHECK(signed_area(std::vector<Point>{{1, 1}}) == 0.0);
}

TEST_CASE("the convex hull of points is their outline, clockwise", "[scan][geometry]") {
    // The corners of a square, with points inside it, on its edges and repeated.
    std::vector<Point> points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {5, 5}, {2, 7},
                                 {5, 0}, {10, 5}, {5, 10},  {0, 5},  {0, 0}, {10, 10}};
    const std::vector<Point> hull = convex_hull(points);
    REQUIRE(hull.size() == 4);
    CHECK(signed_area(hull) == Approx(100.0));
    for (const Point corner : {Point{0, 0}, Point{10, 0}, Point{10, 10}, Point{0, 10}}) {
        bool found = false;
        for (const Point& p : hull) {
            found = found || p == corner;
        }
        CHECK(found);
    }
}

TEST_CASE("the hull of too few points is the points", "[scan][geometry]") {
    CHECK(convex_hull({}).empty());
    CHECK(convex_hull({{1, 1}}).size() == 1);
    CHECK(convex_hull({{1, 1}, {1, 1}, {2, 2}}).size() == 2);
    // Three on a line have no area: the ends.
    CHECK(convex_hull({{0, 0}, {1, 1}, {2, 2}}).size() == 2);
}

TEST_CASE("a convex shape with rounded corners reduces to the four-sided one around it",
          "[scan][geometry]") {
    // A 100 x 60 rectangle with its corners rounded off, as a polygon of many points.
    std::vector<Point> points;
    const double radius = 8.0;
    const double centres[4][2] = {{100 - radius, radius},
                                  {100 - radius, 60 - radius},
                                  {radius, 60 - radius},
                                  {radius, radius}};
    for (int corner = 0; corner < 4; ++corner) {
        for (int k = 0; k <= 8; ++k) {
            const double angle =
                (-90.0 + corner * 90.0 + k * 90.0 / 8.0) * 3.14159265358979 / 180.0;
            points.push_back({centres[corner][0] + radius * std::cos(angle),
                              centres[corner][1] + radius * std::sin(angle)});
        }
    }
    const std::vector<Point> hull = convex_hull(points);
    REQUIRE(hull.size() > 8);
    const auto quad = enclosing_quad(hull);
    REQUIRE(quad.has_value());
    // The corners of the rectangle the rounding was cut from.
    std::vector<Point> expected = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
    for (const Point& corner : expected) {
        bool found = false;
        for (const Point& p : *quad) {
            found = found || near(p, corner, 0.5);
        }
        INFO("corner " << corner.x << ", " << corner.y);
        CHECK(found);
    }
    CHECK(signed_area(*quad) > 0.0);
}

TEST_CASE("a quadrilateral reduces to itself, and a triangle to nothing", "[scan][geometry]") {
    const std::vector<Point> square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const auto same = enclosing_quad(square);
    REQUIRE(same.has_value());
    for (std::size_t k = 0; k < 4; ++k) {
        CHECK((*same)[k] == square[k]);
    }
    CHECK_FALSE(enclosing_quad(std::vector<Point>{{0, 0}, {10, 0}, {5, 8}}).has_value());
}

TEST_CASE("the first corner is the one the first edge leaves most nearly along +x",
          "[scan][geometry]") {
    // A square tilted a little, written starting at each corner in turn.
    const Quad tilted = {{{10, 2}, {18, 0}, {20, 8}, {12, 10}}};
    // The edge (10, 2) -> (18, 0) runs most nearly along +x, so it starts the order,
    // however the corners were listed.
    for (std::size_t start = 0; start < 4; ++start) {
        const Quad shifted = {tilted[start], tilted[(start + 1) % 4], tilted[(start + 2) % 4],
                              tilted[(start + 3) % 4]};
        const Quad upright = rotate_to_upright(shifted);
        CHECK(upright[0] == tilted[0]);
        CHECK(upright[1] == tilted[1]);
    }
    // A board turned a quarter turn: still starts at the corner its top edge leaves.
    const Quad quarter = rotate_to_upright({{{0, 10}, {0, 0}, {10, 0}, {10, 10}}});
    CHECK(quarter[0] == Point{0, 0});
}

TEST_CASE("the map from a square to a quadrilateral takes corners to corners", "[scan][geometry]") {
    const Quad square = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    const Quad board = {{{120, 80}, {420, 100}, {400, 330}, {90, 300}}};
    const auto map = homography(square, board);
    REQUIRE(map.has_value());
    for (std::size_t k = 0; k < 4; ++k) {
        CHECK(near(map->apply(square[k]), board[k], 1e-6));
    }
    // The middle of the square is where the diagonals of the quadrilateral cross,
    // not the mean of its corners: perspective.
    const Point middle = map->apply({50, 50});
    CHECK(middle.x > 0.0);
    CHECK(middle.y > 0.0);

    const auto back = inverse(*map);
    REQUIRE(back.has_value());
    for (const Point p : {Point{10, 20}, Point{50, 50}, Point{99, 1}}) {
        CHECK(near(back->apply(map->apply(p)), p, 1e-6));
    }
}

TEST_CASE("a map from corners with three on a line does not exist", "[scan][geometry]") {
    const Quad square = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    const Quad flat = {{{0, 0}, {50, 0}, {100, 0}, {150, 0}}};
    CHECK_FALSE(homography(square, flat).has_value());
}

TEST_CASE("the identity map and a singular one", "[scan][geometry]") {
    const Homography identity;
    CHECK(identity.apply({3, 4}) == Point{3, 4});
    Homography singular;
    singular.m = {1, 2, 3, 2, 4, 6, 0, 0, 1};
    CHECK_FALSE(inverse(singular).has_value());
}

TEST_CASE("distance and the cross product agree with the shapes they describe",
          "[scan][geometry]") {
    CHECK(distance({0, 0}, {3, 4}) == Approx(5.0));
    // Clockwise on a screen is a positive turn.
    CHECK(cross({0, 0}, {1, 0}, {1, 1}) > 0.0);
    CHECK(cross({0, 0}, {1, 0}, {1, -1}) < 0.0);
}

TEST_CASE("a map from corners that are on a line to within rounding does not exist",
          "[scan][geometry]") {
    // Pixel-sized numbers: the pivots of the elimination are rounding noise of about
    // 1e-10 here, which an absolute epsilon of 1e-12 took for a number.
    const Quad square = {{{0, 0}, {480, 0}, {480, 480}, {0, 480}}};
    const Quad nearly_flat = {{{10, 10}, {310, 10.0000000001}, {610, 9.99999999995}, {910, 10}}};
    CHECK_FALSE(homography(square, nearly_flat).has_value());
    CHECK_FALSE(homography(nearly_flat, square).has_value());
}

TEST_CASE("a map from corners that are not numbers does not exist", "[scan][geometry]") {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    const Quad square = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    Quad bad = square;
    bad[2].x = nan;
    CHECK_FALSE(homography(square, bad).has_value());
    CHECK_FALSE(homography(bad, square).has_value());
    bad = square;
    bad[1].y = inf;
    CHECK_FALSE(homography(square, bad).has_value());
    bad = square;
    bad[0] = {-inf, 0};
    CHECK_FALSE(homography(bad, square).has_value());
}

TEST_CASE("a map between small shapes exists as one between large ones does", "[scan][geometry]") {
    // The same pair of quadrilaterals at a millionth and at a thousand times the size.
    for (const double scale : {1e-6, 1e-3, 1.0, 1e3}) {
        const Quad from = {{{0, 0}, {scale, 0}, {scale, scale}, {0, scale}}};
        const Quad to = {
            {{0.1 * scale, 0}, {scale, 0.1 * scale}, {0.9 * scale, scale}, {0, 0.8 * scale}}};
        INFO("scale " << scale);
        const auto map = homography(from, to);
        REQUIRE(map.has_value());
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(near(map->apply(from[k]), to[k], 1e-6 * scale));
        }
        const auto back = inverse(*map);
        REQUIRE(back.has_value());
        CHECK(near(back->apply(to[2]), from[2], 1e-6 * scale));
    }
}

TEST_CASE("an inverse is judged against the size of the matrix, and not found for a non-number",
          "[scan][geometry]") {
    Homography small;
    small.m = {1e-6, 0, 0, 0, 1e-6, 0, 0, 0, 1e-6};  // a determinant of 1e-18, and invertible
    const auto inverted = inverse(small);
    REQUIRE(inverted.has_value());
    CHECK(inverted->m[0] == Approx(1e6));

    Homography nan;
    nan.m = {std::numeric_limits<double>::quiet_NaN(), 0, 0, 0, 1, 0, 0, 0, 1};
    CHECK_FALSE(inverse(nan).has_value());
    Homography flat;
    flat.m = {1e6, 1e6, 0, 1e6, 1e6 + 1e-6, 0, 0, 0, 1};  // rows alike to 1e-12 of their size
    CHECK_FALSE(inverse(flat).has_value());
}

TEST_CASE("the outline of a shape is found at any size", "[scan][geometry]") {
    // An octagon, at a pixel and at a millionth of one: the test for edges that never meet
    // was an absolute number, and at the small size no pair of edges passed it.
    for (const double scale : {1e-6, 1.0, 1e4}) {
        std::vector<Point> octagon;
        for (int k = 0; k < 8; ++k) {
            const double angle = (22.5 + 45.0 * k) * 3.14159265358979 / 180.0;
            octagon.push_back({scale * std::cos(angle), scale * std::sin(angle)});
        }
        INFO("scale " << scale);
        const auto quad = enclosing_quad(convex_hull(octagon));
        REQUIRE(quad.has_value());
        CHECK(std::abs(signed_area(*quad)) > 1.9 * scale * scale);  // the octagon is 2.83
    }
}

TEST_CASE("points that are not places are no part of a hull", "[scan][geometry]") {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    // A NaN in the sort makes the ordering not a strict weak one, which is undefined.
    const std::vector<Point> points = {{0, 0},   {nan, 3}, {10, 0},  {5, inf},
                                       {10, 10}, {0, 10},  {4, nan}, {-inf, 2}};
    const std::vector<Point> hull = convex_hull(points);
    CHECK(hull.size() == 4);
    CHECK(signed_area(hull) == Approx(100.0));
    CHECK(convex_hull(std::vector<Point>{{nan, nan}}).empty());
}
