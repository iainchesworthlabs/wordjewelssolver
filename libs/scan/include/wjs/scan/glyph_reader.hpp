// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "wjs/scan/geometry.hpp"
#include "wjs/scan/image.hpp"
#include "wjs/scan/stroke_font.hpp"

namespace wjs::scan {

// One glyph class's best match in a cell.
struct GlyphScore {
    // An index into stroke_font().
    std::size_t glyph = 0;
    // The normalised correlation of the cell with the glyph's template at its best
    // turn, from -1 to 1; a clean match of a clean drawing is above 0.9.
    double score = 0.0;
    // How far the glyph is turned clockwise from upright, in degrees, at that best.
    double rotation_degrees = 0.0;
};

// What one cell of the board holds.
struct GlyphRead {
    // False if the cell has no glyph worth the name: the picture inside it has
    // no ink, or less than `min_contrast` grey levels of it (a blank die, or a
    // highlight that has washed the letter out).
    bool has_ink = false;
    // The grey levels between the die and the ink.
    double contrast = 0.0;
    // The three glyph classes that match best, best first; `found` of them are set.
    std::array<GlyphScore, 3> best{};
    std::size_t found = 0;
};

// Reads the one glyph in a cell, whatever its turn, by comparing the cell's ink
// with templates drawn from the project's stroke font (wjs/scan/stroke_font.hpp).
//
// The cell is a circular patch of the warped board image, sampled to 24 x 24;
// the patch's ink is the darkness below a plane fitted to the lighter half of
// it, so a gradient of light across a die is not ink; it is turned through 48
// angles, every 7.5 degrees, and compared with each of the 27 templates by
// normalised correlation. The templates keep the glyphs' size, not stretched to
// a box, so N turned a quarter and Z differ in proportion as well as shape.
//
// Template matching here is a stand-in that can be replaced: a bank drawn from
// the real dice font, or a trained classifier, would take its place behind this
// class without the rest of the reader knowing.
class GlyphReader {
public:
    // The reader for the project's stroke font. Built on first use (a few
    // milliseconds), safe to call from any thread.
    [[nodiscard]] static const GlyphReader& standard();

    // Reads the cell centred at `centre` in `board`, a square of side
    // 2 * half_side pixels (the circle inside it is what counts). The glyphs of
    // the stroke font are 55% of a die high: a cell's half_side should be about
    // 0.34 of a cell for them.
    [[nodiscard]] GlyphRead read(const GrayImage& board, Point centre, double half_side,
                                 double min_contrast) const;

    GlyphReader(const GlyphReader&) = delete;
    GlyphReader& operator=(const GlyphReader&) = delete;
    GlyphReader(GlyphReader&&) = delete;
    GlyphReader& operator=(GlyphReader&&) = delete;
    ~GlyphReader() = default;

private:
    GlyphReader();

    // The 27 templates, each as the values of the patch's pixels inside its circle,
    // blurred, with the mean taken out and scaled to a length of 1.
    std::vector<std::vector<float>> templates_;
};

}  // namespace wjs::scan
