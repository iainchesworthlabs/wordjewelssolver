// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "wjs/scan/frame.hpp"

namespace wjs::scan {

// An owned 8-bit grey image: what the reader works on, and what the synthetic
// corpus draws. Allocating one is counted (wjs/scan/work.hpp).
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    GrayImage() = default;
    GrayImage(int width, int height, std::uint8_t fill = 0);

    [[nodiscard]] bool empty() const noexcept { return pixels.empty(); }

    [[nodiscard]] std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x);
    }
    [[nodiscard]] std::uint8_t at(int x, int y) const noexcept { return pixels[index(x, y)]; }
    [[nodiscard]] std::uint8_t& at(int x, int y) noexcept { return pixels[index(x, y)]; }

    [[nodiscard]] Frame view() const noexcept {
        return Frame{std::span<const std::uint8_t>(pixels), static_cast<std::size_t>(width),
                     static_cast<std::size_t>(height), 0, PixelFormat::gray8};
    }
};

// The luma of a frame, BT.601 in integers: (77 R + 150 G + 29 B + 128) / 256. A
// grey frame is copied. A frame that is not valid() gives an empty picture.
[[nodiscard]] GrayImage luma_from_frame(const Frame& frame);

// The image averaged down in whole blocks until its long side is at most
// `max_long_side` (or as it is, if it already is). `factor` is how many source
// pixels a result pixel spans on a side.
[[nodiscard]] GrayImage shrink(const GrayImage& image, int max_long_side, int& factor);

// The image at a point, by bilinear interpolation between the four nearest pixel
// centres (pixel (i, j) has its centre at (i + 0.5, j + 0.5)), repeating the edge
// beyond the picture.
[[nodiscard]] double sample(const GrayImage& image, double x, double y) noexcept;

}  // namespace wjs::scan
