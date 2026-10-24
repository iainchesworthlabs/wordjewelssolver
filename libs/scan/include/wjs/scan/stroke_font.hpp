// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "wjs/scan/geometry.hpp"

namespace wjs::scan {

// The project's own stand-in for the letters on a die: 26 capitals and the
// two-letter Qu face, as strokes (polylines) in a box 4 units wide and 6 tall,
// y down. It is the project's own drawing and no typeface of anyone's; the
// letters of real dice differ from it, and what that costs is not measured
// (docs/design/scanner.md, "What this does not prove").
//
// The reader draws its templates from these strokes at any angle, and the
// synthetic corpus draws its dice from a perturbed copy of them.

using Stroke = std::vector<Point>;

struct StrokeGlyph {
    // "A" to "Z", or "QU".
    std::string_view letters;
    std::vector<Stroke> strokes;
};

inline constexpr std::size_t kGlyphCount = 27;
inline constexpr double kGlyphWidth = 4.0;
inline constexpr double kGlyphHeight = 6.0;
// The stroke width of the reference drawing, in the same units.
inline constexpr double kNominalStrokeWidth = 0.75;

// The glyphs in the order A to Z, then Qu. Built on first use.
[[nodiscard]] const std::array<StrokeGlyph, kGlyphCount>& stroke_font();

// The index in stroke_font() of the glyph that reads as these letters
// (upper-case), or nothing. Not noexcept: the first call builds the font,
// which allocates.
[[nodiscard]] std::optional<std::size_t> glyph_index(std::string_view letters);

// The shortest distance from a point to any of the strokes.
[[nodiscard]] double distance_to_strokes(std::span<const Stroke> strokes, Point p) noexcept;

}  // namespace wjs::scan
