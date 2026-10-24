// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace wjs::scan {

// Image coordinates: x to the right, y down, the centre of the top-left pixel at
// (0.5, 0.5) when a point is a position in the picture.
struct Point {
    double x = 0.0;
    double y = 0.0;

    friend constexpr bool operator==(const Point&, const Point&) = default;
};

[[nodiscard]] constexpr Point operator+(Point a, Point b) noexcept {
    return {a.x + b.x, a.y + b.y};
}
[[nodiscard]] constexpr Point operator-(Point a, Point b) noexcept {
    return {a.x - b.x, a.y - b.y};
}
[[nodiscard]] constexpr Point operator*(Point a, double k) noexcept {
    return {a.x * k, a.y * k};
}

// Four corners, clockwise as drawn on the screen (y down). For the corners of a
// board the first is the one from which the first edge runs most nearly along
// +x: top left, for a board that is upright in the picture.
using Quad = std::array<Point, 4>;

// The z of (b - a) x (c - a): positive when a, b, c turn clockwise on a screen.
[[nodiscard]] constexpr double cross(Point a, Point b, Point c) noexcept {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// The shoelace area: positive for a polygon whose points run clockwise on a
// screen (y down).
[[nodiscard]] double signed_area(std::span<const Point> polygon) noexcept;

[[nodiscard]] double distance(Point a, Point b) noexcept;

// The convex hull of the points, clockwise on a screen, without a repeated
// first point. Fewer than three distinct points give fewer than three points.
[[nodiscard]] std::vector<Point> convex_hull(std::vector<Point> points);

// A convex polygon of any number of corners reduced to four that enclose it,
// by repeatedly replacing the edge whose removal adds least area with the point
// where its two neighbouring edges meet. Empty if the polygon has fewer than
// four corners or no reduction converges (a triangle has no enclosing quad of
// this kind). The result keeps the polygon's winding.
[[nodiscard]] std::optional<Quad> enclosing_quad(std::span<const Point> convex_polygon);

// Turns the corners' order so the first edge runs most nearly along +x. The
// winding is kept.
[[nodiscard]] Quad rotate_to_upright(const Quad& quad) noexcept;

// A 3x3 projective map, row-major, acting on (x, y, 1).
struct Homography {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};

    [[nodiscard]] Point apply(Point p) const noexcept;
};

// The map taking the four points `from` to the four points `to`, or nothing if
// they do not determine one (three on a line).
[[nodiscard]] std::optional<Homography> homography(const Quad& from, const Quad& to) noexcept;

// The inverse map, or nothing if the matrix is singular.
[[nodiscard]] std::optional<Homography> inverse(const Homography& h) noexcept;

}  // namespace wjs::scan
