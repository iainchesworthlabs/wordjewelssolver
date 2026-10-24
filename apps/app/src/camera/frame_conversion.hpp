// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <optional>

#include "wjs/scan/frame.hpp"

// The one conversion from a camera's picture to the scanner's frame, away from
// the backend so it can be tested without a camera. The backend has already
// turned its QVideoFrame into a QImage (the multimedia variant); this copies the
// pixels into an owned RGB24 buffer.
//
// The layout is fixed and stated so the scanner never has to guess: format
// rgb8, one pixel = R, G, B (the byte order of QImage::Format_RGB888), rows
// packed with stride == width * 3, row 0 at the top. Nothing is written anywhere:
// the returned frame is memory, and the caller drops it after the read
// (docs/design/application.md, "Privacy, built in and checked").

namespace wjs::app {

// A copy of the image as an RGB24 frame, or nothing for a null or empty image.
// The QImage's own row padding is not part of the frame.
[[nodiscard]] std::optional<wjs::scan::OwnedFrame> frame_from_image(const QImage& image);

}  // namespace wjs::app
