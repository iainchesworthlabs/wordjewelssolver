// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Holding the reader to the target of docs/design/scanner.md: the five numbers,
// counted over a condition of the corpus.

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

#include "scene.hpp"
#include "wjs/scan/reading.hpp"

namespace wjs::scan::synth {

// What is counted over a set of frames. The tile figures are over the frames in
// which the board was found.
struct Tally {
    std::size_t frames = 0;
    // The reader returned a board of the right size.
    std::size_t found = 0;
    std::size_t tiles = 0;
    std::size_t correct = 0;
    std::size_t flagged = 0;
    // Wrong and not flagged.
    std::size_t silent = 0;
    // Right, or among the runners-up it listed.
    std::size_t offered = 0;
    // Frames in which the board was found and read with the grid turned a quarter
    // other than the one its corners say: the reader numbered the board from the wrong
    // corner. Counted apart from the tiles (they are all judged against the turn the
    // corners say, so such a frame's tiles are wrong, and show in correct and silent).
    std::size_t rotation_errors = 0;

    void add(const Tally& other) {
        frames += other.frames;
        found += other.found;
        tiles += other.tiles;
        correct += other.correct;
        flagged += other.flagged;
        silent += other.silent;
        offered += other.offered;
        rotation_errors += other.rotation_errors;
    }

    [[nodiscard]] double found_rate() const { return ratio(found, frames); }
    [[nodiscard]] double correct_rate() const { return ratio(correct, tiles); }
    [[nodiscard]] double flagged_rate() const { return ratio(flagged, tiles); }
    [[nodiscard]] double silent_rate() const { return ratio(silent, tiles); }
    [[nodiscard]] double offered_rate() const { return ratio(offered, tiles); }

private:
    [[nodiscard]] static double ratio(std::size_t a, std::size_t b) {
        return b == 0 ? 0.0 : static_cast<double>(a) / static_cast<double>(b);
    }
};

// One tile of a frame in which the board was found, with what the reader said
// about it before any threshold was applied.
struct TileRecord {
    bool correct = false;
    bool offered = false;
    bool has_letters = false;
    double score = 0.0;
    double margin = 0.0;
    // What the tile truly is and what the reader said, for the confusion list.
    std::string truth{};
    std::string letters{};
};

// How many quarter turns clockwise the reader's grid of a board is from the board as
// it lies, given the board's corners in its own order (top left, top right, bottom
// right, bottom left, clockwise on the screen). The reader numbers a board from the
// corner at which the first edge runs most nearly along +x, so that is the corner
// that comes first, and the grid is the board turned until it does. Written here from
// that rule and not by calling the reader's own geometry, so that the judge does not
// repeat the reader's mistake.
[[nodiscard]] std::size_t expected_quarters(const Quad& corners);

// Judges a reading against the truth with the grid turned `quarters` quarter turns
// clockwise from the board as it lies. A reading in any other turn is wrong in its
// tiles, where the judge used to take whichever of the four turns matched best and so
// scored a reader that numbered the board from the wrong corner as perfect. Whether a
// different turn would have matched better is counted in `rotation_errors`.
[[nodiscard]] Tally judge_turned(const std::vector<std::string>& truth, std::size_t size,
                                 std::size_t quarters,
                                 const std::expected<BoardReading, ReadError>& reading,
                                 std::vector<TileRecord>* records = nullptr);

// Judges a reading of a frame the corpus drew, against its truth in the turn its
// corners give.
[[nodiscard]] Tally judge(const Rendered& frame,
                          const std::expected<BoardReading, ReadError>& reading,
                          std::vector<TileRecord>* records = nullptr);

// Reads the frames of one condition of a split and tallies them.
[[nodiscard]] Tally evaluate(Condition condition, Split split, const ReadOptions& options,
                             std::vector<TileRecord>* records = nullptr);

// What the tally would have been had a tile been doubtful at these thresholds, from
// the records of reads made with the doubt switched off.
[[nodiscard]] Tally with_thresholds(const std::vector<TileRecord>& records, double min_score,
                                    double min_margin);

}  // namespace wjs::scan::synth
