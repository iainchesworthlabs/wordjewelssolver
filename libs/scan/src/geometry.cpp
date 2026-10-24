// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scan/geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace wjs::scan {

namespace {

// How small a pivot or a determinant may be, as a share of the size of the numbers
// it was made from: the points are in pixels, a thousand times a unit, and a
// number that is rounding noise on them is nothing on a unit square. An absolute
// epsilon was too small for the first and too large for the second. (Rounding is
// about 1e-16 of the numbers; a map that is well posed has pivots of 1e-3 and over.)
constexpr double kRelativeEpsilon = 1e-9;

double cross2(Point a, Point b) noexcept {
    return a.x * b.y - a.y * b.x;
}

bool finite(Point p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

}  // namespace

double signed_area(std::span<const Point> polygon) noexcept {
    double twice = 0.0;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point a = polygon[i];
        const Point b = polygon[(i + 1) % n];
        twice += a.x * b.y - b.x * a.y;
    }
    return twice / 2.0;
}

double distance(Point a, Point b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

std::vector<Point> convex_hull(std::vector<Point> points) {
    // A NaN breaks the strict weak ordering the sort needs, which is undefined
    // behaviour and not just a wrong hull: a point that is not a place is no part
    // of the outline.
    std::erase_if(points, [](Point p) { return !finite(p); });
    std::ranges::sort(points, [](Point a, Point b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3) {
        return points;
    }

    // Andrew's monotone chain, keeping the turns that are clockwise on a screen.
    std::vector<Point> hull(2 * points.size());
    std::size_t k = 0;
    for (const Point& point : points) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], point) <= 0.0) {
            --k;
        }
        hull[k++] = point;
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = points.size() - 1; i > 0; --i) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.0) {
            --k;
        }
        hull[k++] = points[i - 1];
    }
    hull.resize(k - 1);
    return hull;
}

std::optional<Quad> enclosing_quad(std::span<const Point> convex_polygon) {
    std::vector<Point> p(convex_polygon.begin(), convex_polygon.end());
    if (p.size() < 4) {
        return std::nullopt;
    }

    while (p.size() > 4) {
        const std::size_t n = p.size();
        std::size_t best = n;
        double best_added = std::numeric_limits<double>::max();
        Point best_point{};

        for (std::size_t i = 0; i < n; ++i) {
            // Merge the edge b -> c: extend a -> b forward and d -> c backward
            // (that is, c -> d's line, past c) until they meet.
            const Point a = p[(i + n - 1) % n];
            const Point b = p[i];
            const Point c = p[(i + 1) % n];
            const Point d = p[(i + 2) % n];
            const Point r = b - a;
            const Point s = c - d;
            const double denominator = cross2(r, s);
            // Parallel, or as near as the sine of the angle between them can tell
            // (and a zero-length edge, which has no direction).
            if (std::abs(denominator) <=
                kRelativeEpsilon * std::hypot(r.x, r.y) * std::hypot(s.x, s.y)) {
                continue;
            }
            const Point from_a = d - a;
            const double t = cross2(from_a, s) / denominator;
            const double u = cross2(from_a, r) / denominator;
            if (t <= 1.0 || u <= 1.0) {
                continue;  // the edges diverge: no point beyond b and c joins them
            }
            const Point meet = a + r * t;
            const double added = std::abs(cross(b, meet, c)) / 2.0;
            if (added < best_added) {
                best_added = added;
                best = i;
                best_point = meet;
            }
        }

        if (best == n) {
            return std::nullopt;
        }
        const std::size_t next = (best + 1) % n;
        p[best] = best_point;
        p.erase(p.begin() + static_cast<std::ptrdiff_t>(next));
    }

    return Quad{p[0], p[1], p[2], p[3]};
}

Quad rotate_to_upright(const Quad& quad) noexcept {
    std::size_t first = 0;
    double best = std::numeric_limits<double>::max();
    for (std::size_t k = 0; k < 4; ++k) {
        const Point edge = quad[(k + 1) % 4] - quad[k];
        const double angle = std::abs(std::atan2(edge.y, edge.x));
        if (angle < best) {
            best = angle;
            first = k;
        }
    }
    // std::rotate rather than four modular subscripts: MSVC's analyzer cannot
    // prove that a computed index is below the array's size at each call, and
    // this is the same rotation without an index to prove.
    Quad upright = quad;
    std::rotate(upright.begin(), upright.begin() + static_cast<std::ptrdiff_t>(first),
                upright.end());
    return upright;
}

Point Homography::apply(Point p) const noexcept {
    const double w = m[6] * p.x + m[7] * p.y + m[8];
    const double x = m[0] * p.x + m[1] * p.y + m[2];
    const double y = m[3] * p.x + m[4] * p.y + m[5];
    return {x / w, y / w};
}

std::optional<Homography> homography(const Quad& from, const Quad& to) noexcept {
    if (!std::ranges::all_of(from, finite) || !std::ranges::all_of(to, finite)) {
        return std::nullopt;
    }
    // Eight unknowns h0..h7 (h8 = 1), two equations a point.
    std::array<std::array<double, 9>, 8> a{};
    for (std::size_t k = 0; k < 4; ++k) {
        const double x = from[k].x;
        const double y = from[k].y;
        const double u = to[k].x;
        const double v = to[k].y;
        auto& row_u = a.at(2 * k);
        auto& row_v = a.at(2 * k + 1);
        row_u[0] = x;
        row_u[1] = y;
        row_u[2] = 1.0;
        row_u[3] = 0.0;
        row_u[4] = 0.0;
        row_u[5] = 0.0;
        row_u[6] = -x * u;
        row_u[7] = -y * u;
        row_u[8] = u;
        row_v[0] = 0.0;
        row_v[1] = 0.0;
        row_v[2] = 0.0;
        row_v[3] = x;
        row_v[4] = y;
        row_v[5] = 1.0;
        row_v[6] = -x * v;
        row_v[7] = -y * v;
        row_v[8] = v;
    }

    // The size of each column's numbers, which its pivot is to be small against. The
    // columns are of very different sizes (1, a coordinate, a product of two), so
    // one size for the whole matrix would call a small shape's pivots noise.
    std::array<double, 8> column_size{};
    for (const auto& row : a) {
        for (std::size_t j = 0; j < 8; ++j) {
            column_size.at(j) = std::max(column_size.at(j), std::abs(row.at(j)));
        }
    }

    // Gaussian elimination with partial pivoting.
    for (std::size_t column = 0; column < 8; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 8; ++row) {
            if (std::abs(a.at(row).at(column)) > std::abs(a.at(pivot).at(column))) {
                pivot = row;
            }
        }
        if (!(std::abs(a.at(pivot).at(column)) > kRelativeEpsilon * column_size.at(column))) {
            return std::nullopt;
        }
        if (pivot != column) {
            for (std::size_t j = 0; j < 9; ++j) {
                std::swap(a.at(pivot).at(j), a.at(column).at(j));
            }
        }
        for (std::size_t row = 0; row < 8; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = a.at(row).at(column) / a.at(column).at(column);
            for (std::size_t j = column; j < 9; ++j) {
                a.at(row).at(j) -= factor * a.at(column).at(j);
            }
        }
    }

    Homography h;
    for (std::size_t i = 0; i < 8; ++i) {
        h.m[i] = a.at(i).at(8) / a.at(i).at(i);
    }
    h.m[8] = 1.0;
    if (!std::ranges::all_of(h.m, [](double v) { return std::isfinite(v); })) {
        return std::nullopt;
    }
    return h;
}

std::optional<Homography> inverse(const Homography& h) noexcept {
    const auto& m = h.m;
    const double c0 = m[4] * m[8] - m[5] * m[7];
    const double c1 = m[5] * m[6] - m[3] * m[8];
    const double c2 = m[3] * m[7] - m[4] * m[6];
    const double det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    // Singular against the size of the matrix: the product of its rows' lengths
    // bounds the determinant (Hadamard), so this is "has no volume to speak of".
    const auto row_length = [&](std::size_t r) {
        return std::sqrt(m[r] * m[r] + m[r + 1] * m[r + 1] + m[r + 2] * m[r + 2]);
    };
    const double bound = row_length(0) * row_length(3) * row_length(6);
    if (!std::isfinite(det) || !(std::abs(det) > kRelativeEpsilon * bound)) {
        return std::nullopt;
    }
    Homography out;
    out.m[0] = c0 / det;
    out.m[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    out.m[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    out.m[3] = c1 / det;
    out.m[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    out.m[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    out.m[6] = c2 / det;
    out.m[7] = (m[1] * m[6] - m[0] * m[7]) / det;
    out.m[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return out;
}

}  // namespace wjs::scan
