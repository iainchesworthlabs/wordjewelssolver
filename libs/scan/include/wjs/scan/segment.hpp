// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wjs/scan/geometry.hpp"
#include "wjs/scan/image.hpp"

namespace wjs::scan {

// A box blur: each pixel becomes the mean of the (2 * radius + 1) square around
// it, the picture's edge repeated beyond itself.
[[nodiscard]] GrayImage box_blur(const GrayImage& image, int radius);

// A mask (row-major, width * height, 1 for set) of the pixels that are brighter
// than the mean of the `window` square around them by more than `margin` grey
// levels. A local threshold: a lit region and a dark one with the same pattern in
// them give the same mask, and a smooth gradient gives none.
[[nodiscard]] std::vector<std::uint8_t> bright_mask(const GrayImage& image, int window, int margin);

// A connected set of mask pixels (4-connected, so two dice touching at a corner
// are two blobs).
struct Blob {
    // The pixels in it. Holes in it (a dark letter on a bright die) are not
    // counted.
    std::int64_t area = 0;
    // The centroid of its pixels.
    Point centre{};
    // The convex hull of its pixels' outline, clockwise on the screen. A hole
    // does not matter to it.
    std::vector<Point> hull;
    double hull_area = 0.0;
};

// The blobs of the mask whose pixel area is within [min_area, max_area].
[[nodiscard]] std::vector<Blob> find_blobs(std::span<const std::uint8_t> mask, int width,
                                           int height, std::int64_t min_area,
                                           std::int64_t max_area);

}  // namespace wjs::scan
