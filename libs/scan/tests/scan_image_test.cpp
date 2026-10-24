// SPDX-License-Identifier: GPL-3.0-or-later
// Frames and the image operations the finder is built from: luma, shrinking,
// sampling, blurring, the local threshold and the connected blobs.

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "wjs/scan/frame.hpp"
#include "wjs/scan/image.hpp"
#include "wjs/scan/segment.hpp"
#include "wjs/scan/work.hpp"

namespace {

using namespace wjs::scan;
using Catch::Approx;

Frame frame_of(const std::vector<std::uint8_t>& bytes, std::size_t width, std::size_t height,
               PixelFormat format, std::size_t stride = 0) {
    return Frame{std::span<const std::uint8_t>(bytes), width, height, stride, format};
}

// A mask of `width` x `height` with the given rectangles set.
std::vector<std::uint8_t> mask_with(int width, int height,
                                    std::initializer_list<std::array<int, 4>> rectangles) {
    std::vector<std::uint8_t> mask(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (const auto& r : rectangles) {
        for (int y = r[1]; y < r[1] + r[3]; ++y) {
            for (int x = r[0]; x < r[0] + r[2]; ++x) {
                mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)] = 1;
            }
        }
    }
    return mask;
}

}  // namespace

TEST_CASE("a frame is valid if its buffer holds its rows", "[scan][image]") {
    const std::vector<std::uint8_t> bytes(12, 0);
    CHECK(frame_of(bytes, 4, 3, PixelFormat::gray8).valid());
    CHECK_FALSE(frame_of(bytes, 4, 4, PixelFormat::gray8).valid());      // one row short
    CHECK_FALSE(frame_of(bytes, 0, 3, PixelFormat::gray8).valid());      // no width
    CHECK_FALSE(frame_of(bytes, 4, 0, PixelFormat::gray8).valid());      // no height
    CHECK_FALSE(frame_of(bytes, 4, 3, PixelFormat::rgb8).valid());       // three bytes a pixel
    CHECK(frame_of(bytes, 2, 2, PixelFormat::rgb8).valid());             // 2 * 3 * 2 bytes: just
    CHECK(frame_of(bytes, 2, 2, PixelFormat::gray8, 8).valid());         // a stride past the end of
    CHECK_FALSE(frame_of(bytes, 2, 2, PixelFormat::gray8, 11).valid());  // the second row's 2 bytes
    CHECK_FALSE(frame_of(bytes, 4, 1, PixelFormat::gray8, 3).valid());   // a stride under the width
    CHECK(OwnedFrame{}.view().valid() == false);
}

TEST_CASE("a hostile frame size cannot wrap the arithmetic that checks it", "[scan][image]") {
    constexpr std::size_t huge = std::size_t{1} << 62;
    const std::vector<std::uint8_t> bytes(16, 0);

    // 4 * (2^62 + 1) wraps to 4, which a four-byte row would hold.
    CHECK_FALSE(frame_of(bytes, huge + 1, 1, PixelFormat::rgba8).valid());
    CHECK_FALSE(frame_of(bytes, huge + 1, 1, PixelFormat::gray8).valid());
    // (3 - 1) * 2^63 wraps to 0, and a one-row buffer then looks long enough.
    CHECK_FALSE(frame_of(bytes, 2, 3, PixelFormat::gray8, huge * 2).valid());
    CHECK_FALSE(frame_of(bytes, 2, huge + 2, PixelFormat::gray8, 4).valid());

    // A side no picture has, even when the buffer is as long as the rows say.
    const std::vector<std::uint8_t> wide(Frame::max_side + 1, 0);
    CHECK_FALSE(frame_of(wide, Frame::max_side + 1, 1, PixelFormat::gray8).valid());
    CHECK(frame_of(wide, Frame::max_side, 1, PixelFormat::gray8).valid());
    // A side of one that is as tall as the cap is a frame, though; it just has a lot of rows.
    CHECK_FALSE(frame_of(bytes, 1, Frame::max_side + 1, PixelFormat::gray8, 1).valid());
}

TEST_CASE("the luma of a frame that is not valid is an empty picture, not an overrun",
          "[scan][image]") {
    constexpr std::size_t huge = std::size_t{1} << 62;
    const std::vector<std::uint8_t> bytes(4, 7);
    const GrayImage none = luma_from_frame(frame_of(bytes, huge + 1, 1, PixelFormat::rgba8));
    CHECK(none.empty());
    CHECK(none.width == 0);
    CHECK(none.height == 0);
    CHECK(luma_from_frame(Frame{}).empty());
}

TEST_CASE("the luma of a pixel is the BT.601 weighting, whatever the order of its bytes",
          "[scan][image]") {
    // Red, green, blue and white, one pixel each.
    const std::vector<std::uint8_t> rgb = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    const GrayImage from_rgb = luma_from_frame(frame_of(rgb, 4, 1, PixelFormat::rgb8));
    REQUIRE(from_rgb.width == 4);
    CHECK(from_rgb.at(0, 0) == 77);
    CHECK(from_rgb.at(1, 0) == 149);
    CHECK(from_rgb.at(2, 0) == 29);
    CHECK(from_rgb.at(3, 0) == 255);

    const std::vector<std::uint8_t> bgr = {0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255};
    const GrayImage from_bgr = luma_from_frame(frame_of(bgr, 4, 1, PixelFormat::bgr8));
    CHECK(from_bgr.pixels == from_rgb.pixels);

    const std::vector<std::uint8_t> rgba = {255, 0, 0,   9, 0,   255, 0,   9,
                                            0,   0, 255, 9, 255, 255, 255, 9};
    CHECK(luma_from_frame(frame_of(rgba, 4, 1, PixelFormat::rgba8)).pixels == from_rgb.pixels);
    const std::vector<std::uint8_t> bgra = {0,   0, 255, 9, 0,   255, 0,   9,
                                            255, 0, 0,   9, 255, 255, 255, 9};
    CHECK(luma_from_frame(frame_of(bgra, 4, 1, PixelFormat::bgra8)).pixels == from_rgb.pixels);
}

TEST_CASE("a grey frame is copied, and the padding at the end of a row is not read",
          "[scan][image]") {
    // Two rows of three pixels with a stride of five: the two bytes after each row are
    // not pixels.
    const std::vector<std::uint8_t> bytes = {1, 2, 3, 250, 250, 4, 5, 6, 250, 250};
    const GrayImage image = luma_from_frame(frame_of(bytes, 3, 2, PixelFormat::gray8, 5));
    REQUIRE(image.width == 3);
    REQUIRE(image.height == 2);
    CHECK(image.pixels == std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6});
}

TEST_CASE("shrinking averages whole blocks until the long side fits", "[scan][image]") {
    GrayImage image(4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            image.at(x, y) = static_cast<std::uint8_t>(x < 2 ? 10 : 200);
        }
    }
    int factor = 0;
    const GrayImage half = shrink(image, 2, factor);
    CHECK(factor == 2);
    REQUIRE(half.width == 2);
    REQUIRE(half.height == 2);
    CHECK(half.at(0, 0) == 10);
    CHECK(half.at(1, 1) == 200);

    // A picture that already fits is as it was.
    const GrayImage same = shrink(image, 4, factor);
    CHECK(factor == 1);
    CHECK(same.pixels == image.pixels);

    // A long side of 5 into 4 is a factor of 2, and the short side does not vanish.
    const GrayImage thin = shrink(GrayImage(5, 1, 80), 4, factor);
    CHECK(factor == 2);
    CHECK(thin.width == 2);
    CHECK(thin.height == 1);
}

TEST_CASE("sampling interpolates between pixel centres and repeats the edge", "[scan][image]") {
    GrayImage image(2, 2);
    image.at(0, 0) = 0;
    image.at(1, 0) = 100;
    image.at(0, 1) = 200;
    image.at(1, 1) = 50;
    CHECK(sample(image, 0.5, 0.5) == Approx(0.0));
    CHECK(sample(image, 1.5, 0.5) == Approx(100.0));
    CHECK(sample(image, 1.0, 0.5) == Approx(50.0));
    CHECK(sample(image, 1.0, 1.0) == Approx(87.5));
    CHECK(sample(image, -5.0, -5.0) == Approx(0.0));
    CHECK(sample(image, 10.0, 10.0) == Approx(50.0));
}

TEST_CASE("sampling a point that is not on the picture still gives a pixel's grey",
          "[scan][image]") {
    GrayImage image(3, 2);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = static_cast<std::uint8_t>(10 + 40 * i);
    }
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    // A warp through a degenerate map hands sample() these; none may be converted
    // to an int unchecked. The edge repeats, so a far point is the nearest corner.
    CHECK(sample(image, inf, inf) == Approx(image.at(2, 1)));
    CHECK(sample(image, -inf, -inf) == Approx(image.at(0, 0)));
    CHECK(sample(image, 1e300, -1e300) == Approx(image.at(2, 0)));
    CHECK(sample(image, -3e9, 3e9) == Approx(image.at(0, 1)));
    for (const double bad : {nan, -nan}) {
        const double grey = sample(image, bad, 1.0);
        CHECK((grey >= 10.0 && grey <= 255.0));
        const double other = sample(image, 1.0, bad);
        CHECK((other >= 10.0 && other <= 255.0));
        CHECK(std::isfinite(sample(image, bad, bad)));
    }

    // Nothing to sample is black, where it was an out-of-range clamp.
    CHECK(sample(GrayImage{}, 0.5, 0.5) == 0.0);
    CHECK(sample(GrayImage(0, 5), 0.5, 0.5) == 0.0);
    CHECK(sample(GrayImage(5, 0), 0.5, 0.5) == 0.0);
}

TEST_CASE("a grey picture cannot be asked for with a negative size", "[scan][image]") {
    CHECK(GrayImage(-4, 5).empty());
    CHECK(GrayImage(4, -5).empty());
    CHECK(GrayImage(-4, -5).empty());
    CHECK(GrayImage(-4, -5).width == 0);
    CHECK(GrayImage(-4, -5).height == 0);
}

TEST_CASE("shrinking by a huge factor does not overflow the block's sum", "[scan][image]") {
    // A block of 5000 x 5000 pixels of 255 sums to 6.4e9, over what 32 bits hold.
    const GrayImage image(5000, 5000, 255);
    int factor = 0;
    const GrayImage tiny = shrink(image, 1, factor);
    CHECK(factor == 5000);
    REQUIRE(tiny.width == 1);
    REQUIRE(tiny.height == 1);
    CHECK(tiny.at(0, 0) == 255);
}

TEST_CASE("a box blur keeps a constant picture and spreads a point", "[scan][image]") {
    const GrayImage flat(8, 8, 120);
    CHECK(box_blur(flat, 2).pixels == flat.pixels);

    GrayImage point(7, 7, 0);
    point.at(3, 3) = 252;
    const GrayImage blurred = box_blur(point, 1);
    CHECK(blurred.at(3, 3) == 28);
    CHECK(blurred.at(2, 2) == 28);
    CHECK(blurred.at(4, 4) == 28);
    CHECK(blurred.at(1, 3) == 0);
    CHECK(box_blur(point, 0).pixels == point.pixels);
}

TEST_CASE("the local threshold marks what is brighter than its surroundings", "[scan][image]") {
    GrayImage image(60, 60, 40);
    for (int y = 20; y < 40; ++y) {
        for (int x = 20; x < 40; ++x) {
            image.at(x, y) = 200;
        }
    }
    const std::vector<std::uint8_t> mask = bright_mask(image, 41, 8);
    CHECK(mask[image.index(30, 30)] == 1);
    CHECK(mask[image.index(21, 21)] == 1);
    CHECK(mask[image.index(5, 5)] == 0);
    CHECK(mask[image.index(30, 50)] == 0);
}

TEST_CASE("a smooth gradient of light is not bright anywhere inside the picture", "[scan][image]") {
    GrayImage ramp(120, 40);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 120; ++x) {
            ramp.at(x, y) = static_cast<std::uint8_t>(40 + x);
        }
    }
    const std::vector<std::uint8_t> mask = bright_mask(ramp, 31, 8);
    std::size_t set = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 20; x < 100; ++x) {
            set += mask[ramp.index(x, y)];
        }
    }
    CHECK(set == 0);
}

TEST_CASE("blobs are the 4-connected sets of the mask, with area, centre and hull",
          "[scan][image]") {
    // A 10 x 10 square and a 6 x 6 square, apart.
    const auto mask = mask_with(40, 40, {{2, 3, 10, 10}, {25, 20, 6, 6}});
    const std::vector<Blob> blobs = find_blobs(mask, 40, 40, 1, 1000);
    REQUIRE(blobs.size() == 2);
    CHECK(blobs[0].area == 100);
    CHECK(blobs[0].centre.x == Approx(7.0));
    CHECK(blobs[0].centre.y == Approx(8.0));
    CHECK(blobs[0].hull_area == Approx(100.0));
    CHECK(blobs[0].hull.size() == 4);
    CHECK(blobs[1].area == 36);

    // A size window keeps one of them.
    const std::vector<Blob> big = find_blobs(mask, 40, 40, 50, 1000);
    REQUIRE(big.size() == 1);
    CHECK(big[0].area == 100);
}

TEST_CASE("two blobs that touch only at a corner are two blobs", "[scan][image]") {
    const auto mask = mask_with(30, 30, {{0, 0, 10, 10}, {10, 10, 10, 10}});
    CHECK(find_blobs(mask, 30, 30, 1, 1000).size() == 2);
    // Side by side they are one.
    const auto joined = mask_with(30, 30, {{0, 0, 10, 10}, {10, 0, 10, 10}});
    const auto one = find_blobs(joined, 30, 30, 1, 1000);
    REQUIRE(one.size() == 1);
    CHECK(one[0].area == 200);
}

TEST_CASE("a hole in a blob is not in its area but is in its outline", "[scan][image]") {
    // A die with a letter's worth of dark in it.
    auto mask = mask_with(30, 30, {{5, 5, 12, 12}});
    for (int y = 9; y < 13; ++y) {
        for (int x = 9; x < 13; ++x) {
            mask[static_cast<std::size_t>(y) * 30 + static_cast<std::size_t>(x)] = 0;
        }
    }
    const auto blobs = find_blobs(mask, 30, 30, 1, 1000);
    REQUIRE(blobs.size() == 1);
    CHECK(blobs[0].area == 128);
    CHECK(blobs[0].hull_area == Approx(144.0));
}

TEST_CASE("allocating a pixel buffer is counted", "[scan][image]") {
    reset_scan_work();
    const GrayImage a(10, 10);
    const GrayImage b(4, 4, 7);
    CHECK(scan_work().buffers == 2);
    reset_scan_work();
    CHECK(scan_work().buffers == 0);
    CHECK(scan_work().reads == 0);
}

TEST_CASE("a threshold window under a pixel is a pixel", "[scan][image]") {
    // A negative half-side put the window's far edge before its near one: the summed
    // area table was read outside itself and the count of pixels was negative.
    GrayImage image(20, 12, 100);
    image.at(5, 5) = 200;
    for (const int window : {1, 0, -1, -5, -2'000'000'000}) {
        INFO("window " << window);
        const std::vector<std::uint8_t> mask = bright_mask(image, window, 8);
        REQUIRE(mask.size() == 20U * 12U);
        // A pixel is never brighter than the mean of itself.
        CHECK(std::count(mask.begin(), mask.end(), std::uint8_t{1}) == 0);
    }
    // And a window the size of the picture is the picture's mean.
    const std::vector<std::uint8_t> mask = bright_mask(image, 1001, 8);
    CHECK(mask[image.index(5, 5)] == 1);
}

TEST_CASE("blobs are not found in a mask that is shorter than its picture", "[scan][image]") {
    const std::vector<std::uint8_t> short_mask(10 * 10 - 1, 1);
    CHECK(find_blobs(short_mask, 10, 10, 1, 1000).empty());
    const std::vector<std::uint8_t> mask(100, 1);
    CHECK(find_blobs(mask, 10, 10, 1, 1000).size() == 1);
    CHECK(find_blobs(mask, 0, 10, 1, 1000).empty());
    CHECK(find_blobs(mask, 10, 0, 1, 1000).empty());
    CHECK(find_blobs(mask, -10, -10, 1, 1000).empty());
    CHECK(find_blobs(mask, 100000, 100000, 1, 1000).empty());
    CHECK(find_blobs({}, 3, 3, 1, 1000).empty());
}

TEST_CASE("a blur of any radius keeps a constant picture constant", "[scan][image]") {
    // Radius 3000 on a bright picture: the second pass's sum is 255 * (2 * 3000 + 1)^2,
    // 9.2e9, which does not fit 32 bits and came out wrong.
    const GrayImage flat(8, 6, 255);
    for (const int radius : {2047, 2048, 3000, 70000, 1 << 24, 2'000'000'000}) {
        INFO("radius " << radius);
        const GrayImage blurred = box_blur(flat, radius);
        REQUIRE(blurred.pixels.size() == flat.pixels.size());
        for (const std::uint8_t v : blurred.pixels) {
            CHECK(v == 255);
        }
    }
    GrayImage ramp(8, 1);
    for (int x = 0; x < 8; ++x) {
        ramp.at(x, 0) = static_cast<std::uint8_t>(x * 30);
    }
    const GrayImage wide = box_blur(ramp, 1 << 24);
    for (const std::uint8_t v : wide.pixels) {
        CHECK(v == wide.pixels.front());  // the whole picture is in the window: one mean
    }
}
