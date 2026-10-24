// SPDX-License-Identifier: GPL-3.0-or-later
// The stroke font and the glyph reader, on a drawn die and nothing else: no board,
// no camera, no perturbation. What these hold down is that the reader finds the
// glyph it was shown at any turn, and says "nothing" when there is nothing.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>

#include "wjs/scan/glyph_reader.hpp"
#include "wjs/scan/stroke_font.hpp"

namespace {

using namespace wjs::scan;

// A cell of 100 x 100 pixels with a die in it (cream, 89 pixels a side) and the
// glyph at `index` on it, drawn from the stroke font, `turn` degrees clockwise. The
// glyph is 55% of the die high, as the reader expects.
GrayImage die_with_glyph(std::size_t index, double turn_degrees,
                         double weight = kNominalStrokeWidth, std::uint8_t die = 215,
                         std::uint8_t ink = 40) {
    GrayImage image(100, 100, 40);  // the tray around
    const auto& glyph = stroke_font()[index];
    const double scale = 0.55 * 89.0 / kGlyphHeight;  // pixels a stroke-box unit
    const double radians = turn_degrees * std::numbers::pi / 180.0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const double dx = x + 0.5 - 50.0;
            const double dy = y + 0.5 - 50.0;
            if (std::abs(dx) > 44.5 || std::abs(dy) > 44.5) {
                continue;  // the gap
            }
            // Back to the upright glyph's box: undo the turn, then the scale.
            const double ux = dx * std::cos(radians) + dy * std::sin(radians);
            const double uy = -dx * std::sin(radians) + dy * std::cos(radians);
            const Point box{2.0 + ux / scale, 3.0 + uy / scale};
            const double d = distance_to_strokes(glyph.strokes, box) * scale - weight * scale / 2.0;
            const double coverage = std::clamp(0.5 - d, 0.0, 1.0);
            image.at(x, y) = static_cast<std::uint8_t>(die * (1.0 - coverage) + ink * coverage);
        }
    }
    return image;
}

GlyphRead read(const GrayImage& cell) {
    return GlyphReader::standard().read(cell, {50.0, 50.0}, 34.0, 24.0);
}

}  // namespace

TEST_CASE("the stroke font has the 26 capitals and Qu, each with strokes", "[scan][font]") {
    const auto& font = stroke_font();
    REQUIRE(font.size() == kGlyphCount);
    for (std::size_t i = 0; i < 26; ++i) {
        const std::string letters(1, static_cast<char>('A' + i));
        CHECK(font[i].letters == letters);
        CHECK_FALSE(font[i].strokes.empty());
        CHECK(glyph_index(letters) == i);
    }
    CHECK(font[26].letters == "QU");
    CHECK(glyph_index("QU") == 26);
    CHECK_FALSE(glyph_index("Z1").has_value());
    CHECK_FALSE(glyph_index("").has_value());
}

TEST_CASE("every glyph of the font lies in its box", "[scan][font]") {
    for (const StrokeGlyph& glyph : stroke_font()) {
        for (const Stroke& stroke : glyph.strokes) {
            for (const Point& p : stroke) {
                INFO(glyph.letters);
                CHECK(p.x >= -0.01);
                CHECK(p.x <= kGlyphWidth + 0.01);
                CHECK(p.y >= -0.01);
                CHECK(p.y <= kGlyphHeight + 0.6);  // Q's tail goes a little below
            }
        }
    }
}

TEST_CASE("the distance to strokes is the distance to the nearest segment", "[scan][font]") {
    const std::vector<Stroke> strokes = {{{0, 0}, {4, 0}}, {{0, 3}, {0, 6}}};
    CHECK(distance_to_strokes(strokes, {2, 0}) == 0.0);
    CHECK(distance_to_strokes(strokes, {2, 1}) == 1.0);
    CHECK(distance_to_strokes(strokes, {-1, 4}) == 1.0);
    CHECK(distance_to_strokes(strokes, {0, 1.5}) == 1.5);  // equally far from both strokes
    CHECK(distance_to_strokes(strokes, {5, 0}) == 1.0);
}

TEST_CASE("each glyph is read upright, with a clear lead", "[scan][glyph]") {
    for (std::size_t g = 0; g < kGlyphCount; ++g) {
        const GlyphRead result = read(die_with_glyph(g, 0.0));
        INFO(stroke_font()[g].letters);
        REQUIRE(result.has_ink);
        REQUIRE(result.found == 3);
        CHECK(result.best[0].glyph == g);
        CHECK(result.best[0].score > 0.85);
        CHECK(result.best[0].score - result.best[1].score > 0.05);
    }
}

TEST_CASE("each glyph is read at every quarter turn and at awkward angles", "[scan][glyph]") {
    for (const double turn : {90.0, 180.0, 270.0, 37.0, 123.0, 301.0}) {
        for (std::size_t g = 0; g < kGlyphCount; ++g) {
            const GlyphRead result = read(die_with_glyph(g, turn));
            INFO(stroke_font()[g].letters << " turned " << turn << ", read as "
                                          << stroke_font()[result.best[0].glyph].letters << " at "
                                          << result.best[0].rotation_degrees << " with "
                                          << result.best[0].score);
            REQUIRE(result.has_ink);
            CHECK(result.best[0].glyph == g);
        }
    }
}

TEST_CASE("the turn found is the turn drawn, for a glyph with no symmetry", "[scan][glyph]") {
    // G, R, K and the rest are read to within the 7.5 degrees the reader steps by.
    for (const std::string_view letters : {"G", "R", "K", "F", "J", "P"}) {
        const auto g = glyph_index(letters);
        REQUIRE(g.has_value());
        for (const double turn : {0.0, 60.0, 90.0, 200.0, 315.0}) {
            const GlyphRead result = read(die_with_glyph(*g, turn));
            double error = std::abs(result.best[0].rotation_degrees - turn);
            error = std::min(error, 360.0 - error);
            INFO(letters << " turned " << turn << " read at " << result.best[0].rotation_degrees);
            CHECK(error <= 8.0);
        }
    }
}

TEST_CASE("a lighter or heavier hand of the same letter is still that letter", "[scan][glyph]") {
    for (const double weight : {0.55, 1.0, 1.25}) {
        std::size_t right = 0;
        for (std::size_t g = 0; g < kGlyphCount; ++g) {
            const GlyphRead result = read(die_with_glyph(g, 90.0, weight));
            right += (result.has_ink && result.best[0].glyph == g) ? 1U : 0U;
        }
        INFO("weight " << weight);
        CHECK(right >= 25);
    }
}

TEST_CASE("a die with no letter has no ink", "[scan][glyph]") {
    GrayImage blank(100, 100, 215);
    CHECK_FALSE(read(blank).has_ink);

    // A gentle gradient of light across it is not ink either.
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            blank.at(x, y) = static_cast<std::uint8_t>(190 + x / 4);
        }
    }
    const GlyphRead gradient = read(blank);
    CHECK_FALSE(gradient.has_ink);
}

TEST_CASE("a letter washed out to a few grey levels is not read", "[scan][glyph]") {
    const GrayImage faint = die_with_glyph(0, 0.0, kNominalStrokeWidth, 215, 205);
    const GlyphRead result = read(faint);
    CHECK_FALSE(result.has_ink);
    CHECK(result.contrast < 24.0);
}

TEST_CASE("a gradient of light across the die does not hide the letter", "[scan][glyph]") {
    GrayImage lit = die_with_glyph(glyph_index("R").value(), 90.0);
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            if (lit.at(x, y) > 100) {
                lit.at(x, y) =
                    static_cast<std::uint8_t>(std::min(255, lit.at(x, y) - 40 + x * 55 / 100));
            }
        }
    }
    const GlyphRead result = read(lit);
    REQUIRE(result.has_ink);
    CHECK(result.best[0].glyph == glyph_index("R").value());
}

TEST_CASE("every glyph of the font has strokes, and the round letters are round", "[scan][glyph]") {
    // The curved letters are put in by their letters after the table is built; a letter that
    // missed would have been left as the straight strokes (or none) of the table.
    for (const StrokeGlyph& glyph : stroke_font()) {
        INFO(glyph.letters);
        REQUIRE_FALSE(glyph.strokes.empty());
        REQUIRE_FALSE(glyph.strokes.front().empty());
    }
    for (const char* round : {"O", "Q", "C", "G", "D", "U", "J", "B", "P", "R", "QU"}) {
        const std::size_t index = glyph_index(round).value();
        std::size_t points = 0;
        for (const Stroke& stroke : stroke_font()[index].strokes) {
            points += stroke.size();
        }
        INFO(round);
        CHECK(points >= 10);  // an arc is a dozen points or more; a straight letter has 2 to 7
    }
    // Q has O's ring and a tail; QU has Q's strokes and its own.
    CHECK(stroke_font()[glyph_index("Q").value()].strokes.size() == 2);
    CHECK(stroke_font()[glyph_index("QU").value()].strokes.size() > 2);
}

TEST_CASE("a flat patch has no glyph even when no contrast is asked for", "[scan][glyph]") {
    // A contrast of 0 let a patch with no ink in it through to the division by its ink
    // depth, and then to a "letter" chosen from scores that had never been compared.
    const GrayImage flat(100, 100, 150);
    const GlyphRead zero = GlyphReader::standard().read(flat, {50.0, 50.0}, 34.0, 0.0);
    CHECK_FALSE(zero.has_ink);
    CHECK(zero.found == 0);
    const GlyphRead negative = GlyphReader::standard().read(flat, {50.0, 50.0}, 34.0, -5.0);
    CHECK_FALSE(negative.has_ink);
    CHECK(negative.found == 0);
}

TEST_CASE("a cell that is not a place on the board is read as nothing, not as a letter",
          "[scan][glyph]") {
    const GrayImage board = die_with_glyph(glyph_index("R").value(), 0.0);
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    for (const Point centre : {Point{nan, 50.0}, Point{50.0, nan}, Point{inf, -inf},
                               Point{1e300, 1e300}, Point{-1e300, 5.0}}) {
        const GlyphRead far = GlyphReader::standard().read(board, centre, 34.0, 24.0);
        // Off the board the picture's edge repeats: the tray, with no ink on it.
        CHECK_FALSE(far.has_ink);
    }
    for (const double half_side : {0.0, -3.0, nan, inf}) {
        const GlyphRead odd = GlyphReader::standard().read(board, {50.0, 50.0}, half_side, 24.0);
        CHECK(odd.found <= 3);
    }
}

TEST_CASE("reading a cell twice gives the same answer, whatever was read between",
          "[scan][glyph]") {
    // The per-thread work area is reused across cells: nothing of one may reach the next.
    const GrayImage r = die_with_glyph(glyph_index("R").value(), 30.0);
    const GrayImage o = die_with_glyph(glyph_index("O").value(), 200.0);
    const GlyphRead first = GlyphReader::standard().read(r, {50.0, 50.0}, 34.0, 24.0);
    (void)GlyphReader::standard().read(o, {50.0, 50.0}, 34.0, 24.0);
    (void)GlyphReader::standard().read(GrayImage(100, 100, 150), {50.0, 50.0}, 34.0, 0.0);
    const GlyphRead again = GlyphReader::standard().read(r, {50.0, 50.0}, 34.0, 24.0);
    REQUIRE(first.has_ink);
    CHECK(again.best[0].glyph == first.best[0].glyph);
    CHECK(again.best[0].score == first.best[0].score);
    CHECK(again.best[1].score == first.best[1].score);
    CHECK(again.best[0].rotation_degrees == first.best[0].rotation_degrees);
}

TEST_CASE("what each clean upright glyph is read as, with its scores", "[.glyph-print]") {
    for (std::size_t g = 0; g < kGlyphCount; ++g) {
        const GlyphRead result = read(die_with_glyph(g, 0.0));
        std::cout << stroke_font()[g].letters << " ->";
        for (const GlyphScore& s : result.best) {
            std::cout << " " << stroke_font()[s.glyph].letters << " " << s.score << " @"
                      << s.rotation_degrees;
        }
        std::cout << "\n";
    }
}
