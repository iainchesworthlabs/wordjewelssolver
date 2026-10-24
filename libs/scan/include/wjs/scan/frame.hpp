// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wjs::scan {

// How the bytes of one pixel are laid out. A camera's YUV buffer is read as
// gray8 through its Y plane, which is the luma and costs no conversion.
enum class PixelFormat : std::uint8_t { gray8, rgb8, bgr8, rgba8, bgra8 };

[[nodiscard]] constexpr std::size_t bytes_per_pixel(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::gray8:
            return 1;
        case PixelFormat::rgb8:
        case PixelFormat::bgr8:
            return 3;
        case PixelFormat::rgba8:
        case PixelFormat::bgra8:
            return 4;
    }
    return 1;
}

// A view of one camera frame: the pixels, row 0 first, and how they are laid
// out. It owns nothing, so a reader can look at a buffer the camera still holds.
//
// A frame is a picture of a room, and a child may be in it. It exists in memory
// only: nothing in this library writes one anywhere (docs/design/scanner.md,
// "Privacy, built in and checked").
struct Frame {
    std::span<const std::uint8_t> pixels;
    std::size_t width = 0;
    std::size_t height = 0;
    // Bytes from the start of one row to the start of the next. Zero means the
    // rows are packed: width * bytes_per_pixel(format).
    std::size_t stride = 0;
    PixelFormat format = PixelFormat::gray8;

    [[nodiscard]] std::size_t row_bytes() const noexcept {
        return stride != 0 ? stride : width * bytes_per_pixel(format);
    }

    // The longest side, in pixels, of a frame the reader will look at. It is far
    // beyond any camera's (a 16-bit side) and keeps every product of a side with a
    // pixel's bytes, and every side an `int`, inside what arithmetic can hold: the
    // reader's pictures index with `int`.
    static constexpr std::size_t max_side = std::size_t{1} << 16;

    // True if the sizes are positive and within `max_side`, and the buffer holds
    // every row. Written so that no product of the sizes is formed that could wrap:
    // the sizes and the stride come from a camera and are not trusted.
    [[nodiscard]] bool valid() const noexcept {
        if (width == 0 || height == 0 || width > max_side || height > max_side) {
            return false;
        }
        const std::size_t used = width * bytes_per_pixel(format);  // at most 2^18
        const std::size_t row = row_bytes();
        if (row < used || used > pixels.size()) {
            return false;
        }
        // (height - 1) * row + used <= size, as a division: row >= used > 0.
        return (height - 1) <= (pixels.size() - used) / row;
    }
};

// A frame the holder owns, which is what a camera hands out. `t_ms` is
// std::chrono::steady_clock milliseconds (monotonic, arbitrary epoch): only the
// difference between two frames means anything.
struct OwnedFrame {
    std::vector<std::uint8_t> pixels;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t stride = 0;
    PixelFormat format = PixelFormat::gray8;
    std::int64_t t_ms = 0;

    [[nodiscard]] Frame view() const noexcept {
        return Frame{std::span<const std::uint8_t>(pixels), width, height, stride, format};
    }
};

}  // namespace wjs::scan
