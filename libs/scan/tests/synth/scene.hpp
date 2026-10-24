// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The synthetic corpus: frames of a board of dice drawn from numbers and a seed,
// so that the scanner can be held to a target without a photograph of a child's
// room in the repository (docs/design/scanner.md, "The corpus plan"). A test
// tool: nothing in the application links this.
//
// A frame is drawn by inverse mapping. Every pixel asks the scene what it is: the
// board's plane through a pinhole camera (the dice and the tray), or the room
// around it. What it leaves out is listed in the design note; the largest thing
// is a different typeface, since each die's letter is a perturbed copy of the
// reader's own stroke skeleton.

#include <cstddef>
#include <string>
#include <vector>

#include "wjs/scan/geometry.hpp"
#include "wjs/scan/image.hpp"

namespace wjs::scan::synth {

inline constexpr int kFrameWidth = 960;
inline constexpr int kFrameHeight = 720;

enum class Condition { upright, angled, loose, glare };
inline constexpr std::size_t kConditionCount = 4;

[[nodiscard]] const char* name(Condition condition) noexcept;

// Which seeds a frame is drawn from.
//   tuning:  the reader is developed against this split, and the doubt thresholds and
//            the test thresholds are chosen from it, and from nothing else.
//   test:    the split the first targets were held against. It has been read and
//            reported on while the reader changed, so it is no longer held out; it is
//            kept for the tools and for the frames other tests draw.
//   heldout: seeds nothing has been tuned or tuned-to on. The target tests read it, and
//            it is read to check, never to steer: a miss on it is reported as a miss.
//            When it has been looked at to change something, it is retired like the two
//            before it (docs/design/scanner.md, "The corpus plan") and a new base seed
//            takes its place.
enum class Split { tuning, test, heldout };

struct Spec {
    Condition condition = Condition::upright;
    Split split = Split::test;
    // The board's side in dice: 4, 5 or 6.
    std::size_t size = 4;
    unsigned index = 0;
};

// The seed a spec is drawn from.
[[nodiscard]] unsigned seed_of(const Spec& spec) noexcept;

// A frame and what is true of it.
struct Rendered {
    GrayImage image{};
    std::size_t size = 0;
    // The letters of the board as it lies on the table, row-major, size * size.
    // "QU" for the Qu face.
    std::vector<std::string> letters{};
    // Where the corners of the board's outermost dice are in the image, in the
    // board's own order (top left, top right, bottom right, bottom left of the
    // board as it lies), clockwise on the screen.
    Quad corners{};
    // How far each die's letter is turned clockwise from upright, in degrees.
    std::vector<double> turns{};
    // The gap between neighbouring dice, in die sides.
    double gap = 0.0;
    // How tall the glyphs are, in die sides, and how heavy the strokes, in the
    // font's box units (the reader's nominal weight is 0.75).
    double glyph_height = 0.0;
    double stroke_weight = 0.0;
    unsigned seed = 0;
};

[[nodiscard]] Rendered render(const Spec& spec);

// The same board with nothing on the dice: a lattice of blank dice, in the lighting
// and view of the spec's condition. The truth is empty letters. For a frame in which
// a board and a size are found and not a letter.
[[nodiscard]] Rendered render_blank(const Spec& spec);

// The corpus of a split: for each condition 24 boards, 12 of 4x4, 6 of 5x5 and 6 of
// 6x6.
[[nodiscard]] std::vector<Spec> corpus(Split split);

// Frames with no board in them.
enum class NegativeKind {
    // A room: a light gradient, a table edge, noise.
    room,
    // A room with bright and dark rectangles in it (a sheet of paper, a phone).
    clutter,
    // A tiled floor: a lattice of blank bright squares, which is a board with no
    // letters on it.
    tiles,
};
inline constexpr std::size_t kNegativeKindCount = 3;

[[nodiscard]] GrayImage render_negative(NegativeKind kind, unsigned index);

// The same frame as three bytes a pixel, red, green, blue equal.
[[nodiscard]] std::vector<unsigned char> to_rgb(const GrayImage& image);

}  // namespace wjs::scan::synth
