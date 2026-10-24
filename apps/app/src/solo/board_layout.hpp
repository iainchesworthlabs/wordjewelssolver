// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

// The layout rule of docs/design/board.md ("Layouts and sizes"), as a pure
// function of the window and the board size: no Qt, so it is held down by
// tests on the reference windows of that page and the scene and the screens
// share one copy.
//
//   Wide (width at least height): the board area is the leading two thirds of
//   the width, the panel the trailing third, both full height. Leading is the
//   left in a left-to-right language and the right in a right-to-left one
//   (`right_to_left`): the split mirrors, the board's own tiles do not.
//   Tall: the board area is the top two thirds of the height, the panel the
//   bottom third, both full width.
//
//   The board is the largest square that fits its area less an inset, centred.
//   The inset is 12 dp (and the gap between tiles 6 dp) when the area's shorter
//   side is under 600 dp, 24 dp (and 8 dp) from 600 dp up.
//
//   tile = (S - (n - 1) * gap) / n,   S = the shorter side of the area - 2 * inset

namespace wjs::app {

struct LayoutRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;

    friend constexpr bool operator==(const LayoutRect&, const LayoutRect&) = default;
};

// An area, as the scene is given it, and what the rule makes of it.
struct BoardMetrics {
    double inset = 0;
    double gap = 0;
    double tile = 0;  // zero when the area is too small to hold a tile
    double side = 0;  // the board's side: n * tile + (n - 1) * gap
};

// The area's shorter side below which the small inset and gap are used.
inline constexpr double roomy_area_side = 600;
inline constexpr double small_inset = 12;
inline constexpr double roomy_inset = 24;
inline constexpr double small_gap = 6;
inline constexpr double roomy_gap = 8;

// A size is offered only if its tile is at least this (Cubeulator's floor for
// any target). One that does not fit is not shown, not shown disabled.
inline constexpr double min_offered_tile = 48;

// The board that fits an area of `width` by `height`, for an n by n board.
[[nodiscard]] BoardMetrics board_metrics(double width, double height, std::size_t n) noexcept;

struct SoloLayoutResult {
    bool wide = true;
    LayoutRect board_area;  // the leading two thirds, or the top two thirds
    LayoutRect panel_area;  // the rest
    LayoutRect board_rect;  // the board itself, centred in board_area
    BoardMetrics metrics;   // of board_area
};

[[nodiscard]] SoloLayoutResult solo_layout(double width, double height, std::size_t n,
                                           bool right_to_left = false) noexcept;

}  // namespace wjs::app
