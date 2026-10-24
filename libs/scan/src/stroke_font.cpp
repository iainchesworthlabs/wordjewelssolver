// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scan/stroke_font.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace wjs::scan {

namespace {

// The points of an elliptical arc, appended to a stroke: the ellipse centred at
// (cx, cy) with radii (rx, ry), from `from_degrees` to `to_degrees` (either way
// round), where 0 is to the right and 90 is down, y being down.
void arc(Stroke& stroke, double cx, double cy, double rx, double ry, double from_degrees,
         double to_degrees, int steps) {
    for (int k = 0; k <= steps; ++k) {
        const double t = from_degrees + (to_degrees - from_degrees) * k / steps;
        const double radians = t * std::numbers::pi / 180.0;
        stroke.push_back({cx + rx * std::cos(radians), cy + ry * std::sin(radians)});
    }
}

// Qu is a large Q and a small u, side by side, in the same box as every other
// glyph: the Q scaled to 70% across and 78% down, the u a lowercase letter on
// the Q's baseline.
std::vector<Stroke> qu_strokes(const std::vector<Stroke>& q) {
    std::vector<Stroke> out;
    for (const Stroke& stroke : q) {
        Stroke scaled;
        for (const Point& p : stroke) {
            scaled.push_back({p.x * 0.70, p.y * 0.78 + 0.3});
        }
        out.push_back(std::move(scaled));
    }
    const std::vector<Stroke> u = {
        {{0.0, 0.0}, {0.0, 0.62}, {0.15, 0.88}, {0.5, 1.0}, {0.85, 0.88}, {1.0, 0.62}},
        {{1.0, 0.0}, {1.0, 1.0}},
    };
    for (const Stroke& stroke : u) {
        Stroke placed;
        for (const Point& p : stroke) {
            placed.push_back({2.9 + 1.1 * p.x, 2.3 + 3.0 * p.y});
        }
        out.push_back(std::move(placed));
    }
    return out;
}

std::array<StrokeGlyph, kGlyphCount> build() {
    std::array<StrokeGlyph, kGlyphCount> font = {{
        {"A", {{{0, 6}, {2, 0}, {4, 6}}, {{0.8, 4.1}, {3.2, 4.1}}}},
        {"B",
         {{{0, 6}, {0, 0}, {2.6, 0}, {3.6, 0.6}, {3.6, 2.3}, {2.6, 3}, {0, 3}},
          {{2.6, 3}, {3.8, 3.6}, {3.8, 5.3}, {2.8, 6}, {0, 6}}}},
        {"C",
         {{{4, 1},
           {3.3, 0.2},
           {2, 0},
           {0.8, 0.3},
           {0, 1.3},
           {0, 4.7},
           {0.8, 5.7},
           {2, 6},
           {3.3, 5.8},
           {4, 5}}}},
        {"D", {{{0, 0}, {0, 6}, {2, 6}, {3.4, 5.4}, {4, 4}, {4, 2}, {3.4, 0.6}, {2, 0}, {0, 0}}}},
        {"E", {{{4, 0}, {0, 0}, {0, 6}, {4, 6}}, {{0, 3}, {3, 3}}}},
        {"F", {{{4, 0}, {0, 0}, {0, 6}}, {{0, 3}, {3, 3}}}},
        {"G",
         {{{4, 1},
           {3.3, 0.2},
           {2, 0},
           {0.8, 0.3},
           {0, 1.3},
           {0, 4.7},
           {0.8, 5.7},
           {2, 6},
           {3.3, 5.8},
           {4, 5},
           {4, 3.3},
           {2.3, 3.3}}}},
        {"H", {{{0, 0}, {0, 6}}, {{4, 0}, {4, 6}}, {{0, 3}, {4, 3}}}},
        {"I", {{{2, 0}, {2, 6}}}},
        {"J", {{{4, 0}, {4, 4.5}, {3.4, 5.6}, {2.2, 6}, {1, 5.7}, {0.2, 4.6}}}},
        {"K", {{{0, 0}, {0, 6}}, {{4, 0}, {0, 3.6}}, {{1.4, 2.6}, {4, 6}}}},
        {"L", {{{0, 0}, {0, 6}, {4, 6}}}},
        {"M", {{{0, 6}, {0, 0}, {2, 3.8}, {4, 0}, {4, 6}}}},
        {"N", {{{0, 6}, {0, 0}, {4, 6}, {4, 0}}}},
        {"O",
         {{{1.2, 0},
           {2.8, 0},
           {4, 1.2},
           {4, 4.8},
           {2.8, 6},
           {1.2, 6},
           {0, 4.8},
           {0, 1.2},
           {1.2, 0}}}},
        {"P", {{{0, 6}, {0, 0}, {2.8, 0}, {3.8, 0.8}, {3.8, 2.5}, {2.8, 3.3}, {0, 3.3}}}},
        {"Q",
         {{{1.2, 0},
           {2.8, 0},
           {4, 1.2},
           {4, 4.8},
           {2.8, 6},
           {1.2, 6},
           {0, 4.8},
           {0, 1.2},
           {1.2, 0}},
          {{2.4, 4.4}, {4, 6.5}}}},
        {"R",
         {{{0, 6}, {0, 0}, {2.8, 0}, {3.8, 0.8}, {3.8, 2.5}, {2.8, 3.3}, {0, 3.3}},
          {{2, 3.3}, {4, 6}}}},
        {"S",
         {{{3.8, 1},
           {3.1, 0.2},
           {1.9, 0},
           {0.8, 0.3},
           {0.2, 1.2},
           {0.5, 2.3},
           {2, 3},
           {3.5, 3.7},
           {3.8, 4.8},
           {3.2, 5.7},
           {2, 6},
           {0.8, 5.8},
           {0, 5}}}},
        {"T", {{{0, 0}, {4, 0}}, {{2, 0}, {2, 6}}}},
        {"U", {{{0, 0}, {0, 4.7}, {0.8, 5.7}, {2, 6}, {3.2, 5.7}, {4, 4.7}, {4, 0}}}},
        {"V", {{{0, 0}, {2, 6}, {4, 0}}}},
        {"W", {{{0, 0}, {1, 6}, {2, 2.4}, {3, 6}, {4, 0}}}},
        {"X", {{{0, 0}, {4, 6}}, {{4, 0}, {0, 6}}}},
        {"Y", {{{0, 0}, {2, 3.2}, {4, 0}}, {{2, 3.2}, {2, 6}}}},
        {"Z", {{{0, 0}, {4, 0}, {0, 6}, {4, 6}}}},
        {"QU", {}},
    }};
    // The round letters are drawn from elliptical arcs, as a typeface's are, so that
    // an O is round and a D is flat on one side.
    // By the letters, not by a position in the table: a table that is reordered would
    // otherwise put an O's strokes on another letter. `at` throws for letters the table
    // does not have, so the font fails to build, loudly, and not into a wrong glyph.
    const auto at = [&](std::string_view letters) -> StrokeGlyph& {
        for (StrokeGlyph& candidate : font) {
            if (candidate.letters == letters) {
                return candidate;
            }
        }
        throw std::logic_error("the stroke font has no glyph for these letters");
    };
    const auto glyph = [&](std::string_view letters, std::vector<Stroke> strokes) {
        at(letters).strokes = std::move(strokes);
    };
    {
        Stroke o;  // O: the whole ellipse
        arc(o, 2.0, 3.0, 2.0, 3.0, -90.0, 270.0, 28);
        glyph("O", {o});
        Stroke q = o;  // Q: an O and a tail
        glyph("Q", {q, {{2.4, 4.4}, {4.0, 6.5}}});
        Stroke c;  // C: open to the right
        arc(c, 2.0, 3.0, 2.0, 3.0, 40.0, 320.0, 22);
        glyph("C", {c});
        Stroke g;  // G: a C with a bar
        arc(g, 2.0, 3.0, 2.0, 3.0, 325.0, 10.0, 24);
        g.push_back({4.0, 3.3});
        g.push_back({2.3, 3.3});
        glyph("G", {g});
        Stroke d = {{0.0, 0.0}, {2.0, 0.0}};  // D: flat on the left, round on the right
        arc(d, 2.0, 3.0, 2.0, 3.0, -90.0, 90.0, 16);
        d.push_back({0.0, 6.0});
        d.push_back({0.0, 0.0});
        glyph("D", {d});
        Stroke u = {{0.0, 0.0}, {0.0, 3.0}};  // U: straight sides, a round bottom
        arc(u, 2.0, 3.0, 2.0, 3.0, 180.0, 0.0, 16);
        u.push_back({4.0, 0.0});
        glyph("U", {u});
        Stroke j = {{4.0, 0.0}, {4.0, 3.8}};  // J: a stem and a hook
        arc(j, 2.2, 3.8, 1.8, 2.2, 0.0, 160.0, 12);
        glyph("J", {j});
        Stroke b1 = {{0.0, 0.0}, {1.9, 0.0}};  // B: a stem and two bowls
        arc(b1, 1.9, 1.5, 1.9, 1.5, -90.0, 90.0, 10);
        b1.push_back({0.0, 3.0});
        Stroke b2 = {{0.0, 3.0}, {2.0, 3.0}};
        arc(b2, 2.0, 4.5, 2.0, 1.5, -90.0, 90.0, 10);
        b2.push_back({0.0, 6.0});
        glyph("B", {{{0.0, 0.0}, {0.0, 6.0}}, b1, b2});
        Stroke p = {{0.0, 6.0}, {0.0, 0.0}, {1.8, 0.0}};  // P: a stem and a bowl
        arc(p, 1.8, 1.6, 2.0, 1.6, -90.0, 90.0, 12);
        p.push_back({0.0, 3.2});
        glyph("P", {p});
        Stroke r = p;  // R: a P and a leg
        glyph("R", {r, {{1.8, 3.2}, {4.0, 6.0}}});
    }
    at("QU").strokes = qu_strokes(at("Q").strokes);
    return font;
}

double distance_to_segment(Point p, Point a, Point b) noexcept {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    double t = 0.0;
    if (length_squared > 0.0) {
        t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length_squared, 0.0, 1.0);
    }
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

}  // namespace

const std::array<StrokeGlyph, kGlyphCount>& stroke_font() {
    static const std::array<StrokeGlyph, kGlyphCount> font = build();
    return font;
}

// Not noexcept: the first call builds the font, which allocates.
std::optional<std::size_t> glyph_index(std::string_view letters) {
    const auto& font = stroke_font();
    for (std::size_t i = 0; i < font.size(); ++i) {
        if (font[i].letters == letters) {
            return i;
        }
    }
    return std::nullopt;
}

double distance_to_strokes(std::span<const Stroke> strokes, Point p) noexcept {
    double best = std::numeric_limits<double>::max();
    for (const Stroke& stroke : strokes) {
        if (stroke.size() == 1) {
            best = std::min(best, std::hypot(p.x - stroke[0].x, p.y - stroke[0].y));
            continue;
        }
        for (std::size_t i = 0; i + 1 < stroke.size(); ++i) {
            best = std::min(best, distance_to_segment(p, stroke[i], stroke[i + 1]));
        }
    }
    return best;
}

}  // namespace wjs::scan
