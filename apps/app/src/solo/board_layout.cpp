// SPDX-License-Identifier: GPL-3.0-or-later
#include "board_layout.hpp"

#include <algorithm>

namespace wjs::app {

BoardMetrics board_metrics(double width, double height, std::size_t n) noexcept {
    BoardMetrics metrics;
    const double shorter = std::min(width, height);
    const bool roomy = shorter >= roomy_area_side;
    metrics.inset = roomy ? roomy_inset : small_inset;
    metrics.gap = roomy ? roomy_gap : small_gap;
    if (n == 0) {
        return metrics;
    }

    const double count = static_cast<double>(n);
    const double span = shorter - 2 * metrics.inset;
    const double tile = (span - (count - 1) * metrics.gap) / count;
    if (tile <= 0) {
        return metrics;
    }
    metrics.tile = tile;
    metrics.side = count * tile + (count - 1) * metrics.gap;
    return metrics;
}

SoloLayoutResult solo_layout(double width, double height, std::size_t n,
                             bool right_to_left) noexcept {
    SoloLayoutResult result;
    result.wide = width >= height;
    if (result.wide) {
        const double board_width = width * 2 / 3;
        if (right_to_left) {
            // The panel takes the leading (left) side and the board the trailing
            // (right) one. Only the areas swap: the board is drawn from its own
            // top left in either direction.
            result.panel_area = {0, 0, width - board_width, height};
            result.board_area = {width - board_width, 0, board_width, height};
        } else {
            result.board_area = {0, 0, board_width, height};
            result.panel_area = {board_width, 0, width - board_width, height};
        }
    } else {
        const double board_height = height * 2 / 3;
        result.board_area = {0, 0, width, board_height};
        result.panel_area = {0, board_height, width, height - board_height};
    }

    result.metrics = board_metrics(result.board_area.width, result.board_area.height, n);
    const double side = result.metrics.side;
    result.board_rect = {result.board_area.x + (result.board_area.width - side) / 2,
                         result.board_area.y + (result.board_area.height - side) / 2, side, side};
    return result;
}

}  // namespace wjs::app
