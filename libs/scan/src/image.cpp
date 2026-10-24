// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scan/image.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "scratch.hpp"
#include "wjs/scan/work.hpp"

namespace wjs::scan {

GrayImage::GrayImage(int w, int h, std::uint8_t fill)
    : width(w > 0 && h > 0 ? w : 0),
      height(w > 0 && h > 0 ? h : 0),
      pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill) {
    detail::note_buffer();
}

void detail::luma_into(GrayImage& out, const Frame& frame) {
    // The loops below trust the frame's sizes: they are checked by valid(), which
    // also keeps them within an int.
    assert(frame.valid());
    const int width = static_cast<int>(frame.width);
    const int height = static_cast<int>(frame.height);
    detail::acquire_image(out, width, height);

    const std::size_t step = bytes_per_pixel(frame.format);
    const std::size_t row_bytes = frame.row_bytes();
    // The offsets of red, green and blue within a pixel.
    std::size_t red = 0;
    std::size_t green = 0;
    std::size_t blue = 0;
    switch (frame.format) {
        case PixelFormat::gray8:
            break;
        case PixelFormat::rgb8:
        case PixelFormat::rgba8:
            red = 0;
            green = 1;
            blue = 2;
            break;
        case PixelFormat::bgr8:
        case PixelFormat::bgra8:
            red = 2;
            green = 1;
            blue = 0;
            break;
    }

    for (std::size_t y = 0; y < frame.height; ++y) {
        const std::uint8_t* in = frame.pixels.data() + y * row_bytes;
        std::uint8_t* dest = out.pixels.data() + y * frame.width;
        if (frame.format == PixelFormat::gray8) {
            std::copy(in, in + frame.width, dest);
            continue;
        }
        for (std::size_t x = 0; x < frame.width; ++x, in += step) {
            const unsigned luma = 77U * in[red] + 150U * in[green] + 29U * in[blue] + 128U;
            dest[x] = static_cast<std::uint8_t>(luma >> 8U);
        }
    }
}

GrayImage luma_from_frame(const Frame& frame) {
    GrayImage out;
    if (!frame.valid()) {
        return out;
    }
    detail::luma_into(out, frame);
    return out;
}

GrayImage shrink(const GrayImage& image, int max_long_side, int& factor) {
    GrayImage out;
    detail::shrink_into(out, image, max_long_side, factor);
    if (factor == 1) {
        return image;
    }
    return out;
}

void detail::shrink_into(GrayImage& out, const GrayImage& image, int max_long_side, int& factor) {
    const int long_side = std::max(image.width, image.height);
    factor = 1;
    if (max_long_side > 0 && long_side > max_long_side) {
        factor = (long_side + max_long_side - 1) / max_long_side;
    }
    if (factor == 1) {
        // The source already fits: the reader uses it as it is, no copy.
        return;
    }

    const int width = std::max(1, image.width / factor);
    const int height = std::max(1, image.height / factor);
    detail::acquire_image(out, width, height);
    for (int y = 0; y < height; ++y) {
        // A block is whole except where the picture is smaller than a block.
        const int rows = std::min(factor, image.height - y * factor);
        for (int x = 0; x < width; ++x) {
            const int columns = std::min(factor, image.width - x * factor);
            // 64 bits: a block of more than 4096 on a side overflows 32.
            std::uint64_t sum = 0;
            for (int dy = 0; dy < rows; ++dy) {
                const std::uint8_t* row =
                    image.pixels.data() + image.index(x * factor, y * factor + dy);
                for (int dx = 0; dx < columns; ++dx) {
                    sum += row[dx];
                }
            }
            const auto block =
                static_cast<std::uint64_t>(rows) * static_cast<std::uint64_t>(columns);
            out.at(x, y) = static_cast<std::uint8_t>((sum + block / 2U) / block);
        }
    }
}

namespace {

// A coordinate held to [0, hi]; NaN is 0. A point beyond the picture reads its
// edge, and this is that rule before any conversion to an integer, so that no
// double the caller hands in (a degenerate warp gives infinities and NaNs) can
// reach a cast it does not fit.
double held(double v, double hi) noexcept {
    if (!(v > 0.0)) {
        return 0.0;
    }
    return v < hi ? v : hi;
}

}  // namespace

double sample(const GrayImage& image, double x, double y) noexcept {
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() <
            static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height)) {
        return 0.0;
    }
    const double fx = held(x - 0.5, image.width - 1);
    const double fy = held(y - 0.5, image.height - 1);
    const double floor_x = std::floor(fx);
    const double floor_y = std::floor(fy);
    const double tx = fx - floor_x;
    const double ty = fy - floor_y;
    const int x0 = static_cast<int>(floor_x);
    const int y0 = static_cast<int>(floor_y);
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const double top = image.at(x0, y0) * (1.0 - tx) + image.at(x1, y0) * tx;
    const double bottom = image.at(x0, y1) * (1.0 - tx) + image.at(x1, y1) * tx;
    return top * (1.0 - ty) + bottom * ty;
}

}  // namespace wjs::scan
