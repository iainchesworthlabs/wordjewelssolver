// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/scan/geometry.hpp"

namespace wjs::scan {

// A letter the reader thought of and did not choose.
struct Alternative {
    // "A" to "Z" or "QU"; empty if there was no second thought.
    std::string letters;
    // The correlation that gave it, as for the choice.
    double score = 0.0;
};

// What the reader made of one die.
struct TileReading {
    // "A" to "Z" or "QU" (upper-case); empty if the cell held no glyph the reader
    // could see.
    std::string letters;
    // From 0 to 1. 0.5 is the doubt threshold: below it the tile is `doubtful`,
    // at 1 the reader is twice as sure as it needs to be.
    double confidence = 0.0;
    // The reader does not trust this tile. It is the ring and the `?` of the check
    // screen, and the tile a grown-up should look at.
    bool doubtful = true;
    // How far the glyph is turned clockwise from upright on the die, in degrees.
    double rotation_degrees = 0.0;
    // What the confidence is made of: the best letter's correlation with its
    // template, and how far that is ahead of the next letter's. Both are for
    // tuning and for looking at; a screen uses `doubtful` and `confidence`.
    double score = 0.0;
    double margin = 0.0;
    // The two next-best letters, best first, for the picker to offer first.
    std::array<Alternative, 2> alternatives{};
};

// What the reader made of a frame. It is NOT a Board: nothing converts it to one.
// The solver sees a board only after BoardDraft::confirm() (confirmation.hpp),
// which a grown-up's "Yes, play" calls.
struct BoardReading {
    std::size_t rows = 0;
    std::size_t columns = 0;
    // Row-major, rows * columns of them.
    std::vector<TileReading> tiles;
    // The board's outline in the frame, in its pixels, clockwise on the screen
    // from the corner nearest to the top left.
    Quad corners{};
    // From 0 to 1: how clearly the cell count stood out. Low means "is this
    // really N x N?" is a fair question for the check screen to ask.
    double size_confidence = 0.0;
    // The die-shaped blobs the board was found from.
    std::size_t dice_found = 0;
    // How tall the glyphs were taken to be, as a share of a die.
    double glyph_height_ratio = 0.55;

    [[nodiscard]] const TileReading& tile(Position position) const noexcept {
        return tiles[position.row * columns + position.column];
    }
};

// Why a frame was not read. None of them is the child's doing and none is to be
// worded as if it were: the screen answers each with "read again".
enum class ReadError {
    // The frame is empty or its buffer is too short for its size.
    invalid_frame,
    // No board: no lattice of dice stood out in the picture.
    board_not_found,
    // A board was found but no cell count fitted it.
    size_not_found,
    // A board and a size, and not a letter in it that could be seen.
    no_letters,
    // The options are out of range (ReadOptions::valid). The frame was not looked
    // at: this is the caller's mistake, never a camera's.
    invalid_options,
};

[[nodiscard]] std::string_view to_string(ReadError error) noexcept;

struct ReadOptions {
    // The long side, in pixels, of the working picture the board is looked for in.
    int working_size = 480;
    // The long side of the picture the board is drawn from (a camera frame is
    // averaged down to this first).
    int analysis_size = 960;
    // The board's side in cells, if known; otherwise counted from the picture,
    // between min_size and max_size.
    std::optional<std::size_t> size;
    std::size_t min_size = 3;
    std::size_t max_size = 8;
    // The board is drawn at this many pixels a side before it is cut into cells.
    int board_side = 480;
    // How tall the glyphs are, as a share of a die (the stroke font's are 55%). Used
    // as given if `estimate_glyph_height` is off, and as the middle of the search
    // if it is on.
    double glyph_height_ratio = 0.55;
    // Every die on a board has the same typeface at the same size, and a glyph's
    // match is much worse if it is drawn a tenth too big or small (a ring most of
    // all: an O drawn too big is nearer a D than the O). So the glyph height is
    // estimated from the board, from how well a sample of its cells match at each
    // trial height, before any cell is read for good.
    bool estimate_glyph_height = true;
    // The doubt thresholds: a tile is doubtful if its best correlation is under
    // `min_score` or beats the next letter by less than `min_margin`, or has less
    // than `min_contrast` grey levels of ink. Starting values, chosen on the tuning
    // split of the synthetic corpus and nothing else (docs/design/scanner.md).
    //
    // A negative threshold switches that doubt off (the tuning tools do); zero is
    // refused, since the confidence divides by it.
    double min_score = 0.55;
    double min_margin = 0.06;
    double min_contrast = 24.0;

    // Whether read_board can use these. The sizes are held to what the pictures and
    // the board can be (a working or analysis picture of 16 to 65536 pixels on its
    // long side, a board drawn at 32 to 4096, a count between 2 and
    // Board::max_dimension), the glyph height to a tenth to the whole of a die, and
    // the doubt thresholds to finite numbers, the score and margin not zero and the
    // contrast not negative. The defaults are valid.
    [[nodiscard]] bool valid() const noexcept;
};

}  // namespace wjs::scan
