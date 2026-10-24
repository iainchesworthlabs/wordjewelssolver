// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "wjs/scan/geometry.hpp"
#include "wjs/scan/image.hpp"

namespace wjs::scan {

struct BoardFind {
    // The board's outline: the corners of the outermost dice, clockwise on the
    // screen, the first being the one from which the first edge runs most nearly
    // along +x. In the coordinates of the image that was searched.
    Quad corners{};
    // The die-shaped blobs that agreed with each other, and the median length of a
    // side of them.
    std::size_t dice = 0;
    double die_side = 0.0;
    // How many dice there are along a side, as a real number, from the outline's
    // length against a die's side (a board is N dice and N - 1 gaps of about a
    // tenth of a die): 4.0 for a 4x4. Good to a few percent of N, which is enough to
    // tell 4 from 5 when a highlight has washed the comb out.
    double size_estimate = 0.0;
};

// Finds the board in a grey image: the largest convex quadrilateral of a lattice
// of dice.
//
// The dice are the bright, die-shaped blobs: a local threshold marks the pixels
// brighter than their surroundings, and a blob is a die if it is a convex,
// four-sided shape (letters make holes in it, which do not count against it),
// about as long as it is wide, and of a size a die can be. Dice are told from the
// other bright things in a room (a sheet of paper, a phone) by their company: the
// largest set of blobs that are each a neighbour of another, at a spacing and a
// size that agree, is the board. The board's outline is the convex hull of those,
// reduced to the four corners that enclose it.
//
// `image` should be small enough for a quarter-million pixels or so; the reader
// shrinks a frame to 480 on its long side first. Nothing if no group of at least
// `min_dice` is found.
[[nodiscard]] std::optional<BoardFind> find_board(const GrayImage& image, std::size_t min_dice = 8);

// The board's four corners drawn as a square of `side` pixels, by the projective
// map that takes the square to the quadrilateral, each pixel read from `source` by
// bilinear interpolation. The first corner is the square's top left.
[[nodiscard]] GrayImage warp_to_square(const GrayImage& source, const Quad& corners, int side);

// How many cells a side of a warped square board has.
struct CellCount {
    std::size_t size = 0;
    // From 0 to 1: how clearly that count stood out from the others.
    double confidence = 0.0;
};

// The mean grey of each column and each row of a warped board, smoothed over
// three samples and stretched to run from 0 to 1. Two full passes over the
// picture, so a caller that wants both the cell count and the cell centres
// (the reader) computes it once and passes it to both.
struct BoardProfiles {
    std::vector<double> columns;
    std::vector<double> rows;
};

[[nodiscard]] BoardProfiles profiles_of(const GrayImage& warped);

// Counts the cells of a warped square board by their gaps: the dice are bright and
// the gaps between them dark, so the row and column profiles are a comb, whose
// teeth are the dice. The count is the one whose comb fits best. Nothing if no
// count in [min_size, max_size] fits with a clear margin (there is no comb).
//
// `dice` and `estimate` are what the finder knows, if it is known (0 if not): a
// board of N x N holds at most N * N dice, so a count that has fewer cells than
// there are dice (less one, for a stray) is out, and one with many more than three
// times as many cells as dice; and a count more than 0.8 from the estimate of the
// size (BoardFind::size_estimate) is out. A highlight can wash a comb out; the dice
// it left are still dice, and the outline is still as long as it was.
[[nodiscard]] std::optional<CellCount> count_cells(const GrayImage& warped, std::size_t min_size,
                                                   std::size_t max_size, std::size_t dice = 0,
                                                   double estimate = 0.0);
// The same, from the profiles the caller computed once for this board.
[[nodiscard]] std::optional<CellCount> count_cells(const GrayImage& warped,
                                                   const BoardProfiles& profiles,
                                                   std::size_t min_size, std::size_t max_size,
                                                   std::size_t dice = 0, double estimate = 0.0);

// How well the comb of each count from min_size to max_size fits the warped board:
// the brightness at the middles of the cells less the brightness at the seams, as
// a share of the board's whole range, row and column profiles averaged. One score
// for each count, in order. count_cells chooses among them.
[[nodiscard]] std::vector<double> comb_scores(const GrayImage& warped, std::size_t min_size,
                                              std::size_t max_size);

// Where the middle of each die is along the two sides of a warped square board of
// `size` cells: x[j] for column j, y[i] for row i, in the warped image's pixels. The
// dice are not the whole of their cells (there is a gap between them), so the cell
// divisions of an even split are off by up to 4% of a cell, which is the better
// part of a glyph's stroke width: the centres are found from where the gaps
// actually are, between each die and the next. Falls back to the even split where a
// gap cannot be found.
struct CellCentres {
    std::vector<double> x;
    std::vector<double> y;
};

[[nodiscard]] CellCentres locate_cells(const GrayImage& warped, std::size_t size);
// The same, from the profiles the caller computed once for this board.
[[nodiscard]] CellCentres locate_cells(const GrayImage& warped, const BoardProfiles& profiles,
                                       std::size_t size);

}  // namespace wjs::scan
