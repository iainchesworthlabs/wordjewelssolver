// SPDX-License-Identifier: GPL-3.0-or-later
// The one step of the multimedia backend that can be tested without a camera: a
// QVideoFrame made from a picture, converted the way QCamera's frames are
// (QVideoFrame::toImage() and then the shared conversion). It is compiled only
// when the kit has Qt Multimedia (tests/CMakeLists.txt), which is the condition
// the backend itself is built under.

#include <QColor>
#include <QImage>
#include <QVideoFrame>
#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "frame_conversion.hpp"
#include "wjs/scan/frame.hpp"

TEST_CASE("a video frame becomes an RGB24 scanner frame", "[app][scan][camera]") {
    QImage image(2, 2, QImage::Format_RGB888);
    REQUIRE_FALSE(image.isNull());
    image.setPixelColor(0, 0, QColor(1, 2, 3));
    image.setPixelColor(1, 0, QColor(4, 5, 6));
    image.setPixelColor(0, 1, QColor(7, 8, 9));
    image.setPixelColor(1, 1, QColor(10, 11, 12));

    const QVideoFrame video(image);
    REQUIRE(video.isValid());
    const QImage converted = video.toImage();
    REQUIRE_FALSE(converted.isNull());

    const std::optional<wjs::scan::OwnedFrame> frame = wjs::app::frame_from_image(converted);
    REQUIRE(frame.has_value());
    CHECK(frame->width == 2);
    CHECK(frame->height == 2);
    CHECK(frame->stride == 6);
    CHECK(frame->format == wjs::scan::PixelFormat::rgb8);
    REQUIRE(frame->pixels.size() == 12);
    CHECK(frame->pixels[0] == 1);
    CHECK(frame->pixels[1] == 2);
    CHECK(frame->pixels[2] == 3);
    CHECK(frame->pixels[9] == 10);
    CHECK(frame->pixels[10] == 11);
    CHECK(frame->pixels[11] == 12);
    CHECK(frame->view().valid());
}
