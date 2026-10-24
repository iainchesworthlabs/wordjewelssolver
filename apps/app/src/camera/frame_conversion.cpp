// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_conversion.hpp"

#include <QImage>
#include <chrono>
#include <cstdint>
#include <cstring>

#include "wjs/profiling.hpp"

namespace wjs::app {
namespace {

[[nodiscard]] std::int64_t now_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

std::optional<wjs::scan::OwnedFrame> frame_from_image(const QImage& image) {
    auto zone = wjs::profiling::zone<"camera.convert_frame">();
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return std::nullopt;
    }

    wjs::scan::OwnedFrame frame;
    frame.width = static_cast<std::size_t>(image.width());
    frame.height = static_cast<std::size_t>(image.height());
    frame.stride = frame.width * 3;
    frame.format = wjs::scan::PixelFormat::rgb8;
    frame.pixels.resize(frame.height * frame.stride);

    if (image.format() == QImage::Format_RGB888) {
        // Already the bytes the scanner wants, so the rows are copied without a
        // conversion pass. QImage pads each row to a multiple of four bytes;
        // the frame's rows are packed, so they are copied one by one.
        for (int y = 0; y < image.height(); ++y) {
            std::memcpy(frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride,
                        image.constScanLine(y), frame.stride);
        }
    } else if (image.format() == QImage::Format_ARGB32) {
        // 0xffRRGGBB a pixel (Format_ARGB32, also known as Format_RGB32): the
        // same picture as RGB888 with the alpha dropped, which is what
        // convertToFormat would do after allocating and filling a second
        // full-size image. This is the format a QVideoFrame's toImage()
        // produces, so it is the one conversion the live camera makes every
        // capture, and it is packed straight into the frame.
        for (int y = 0; y < image.height(); ++y) {
            const std::uint8_t* row = image.constScanLine(y);
            std::uint8_t* out = frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride;
            for (int x = 0; x < image.width(); ++x) {
                std::uint32_t pixel = 0;
                std::memcpy(&pixel, row + 4 * static_cast<std::size_t>(x), sizeof(pixel));
                out[3 * x] = static_cast<std::uint8_t>((pixel >> 16) & 0xffU);
                out[3 * x + 1] = static_cast<std::uint8_t>((pixel >> 8) & 0xffU);
                out[3 * x + 2] = static_cast<std::uint8_t>(pixel & 0xffU);
            }
        }
    } else {
        // Any other layout (grey, indexed, premultiplied, ...) is converted the
        // one Qt way, then the rows are packed into the frame.
        const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
        if (rgb.isNull() || rgb.width() <= 0 || rgb.height() <= 0) {
            return std::nullopt;
        }
        for (int y = 0; y < rgb.height(); ++y) {
            std::memcpy(frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride,
                        rgb.constScanLine(y), frame.stride);
        }
    }
    frame.t_ms = now_ms();
    return frame;
}

}  // namespace wjs::app
