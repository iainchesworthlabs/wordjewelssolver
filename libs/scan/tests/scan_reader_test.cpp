// SPDX-License-Identifier: GPL-3.0-or-later
// read_board, end to end on frames from the synthetic corpus: the answer, the
// pixel formats, the options, the errors, and the counts it keeps.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "evaluate.hpp"
#include "fixtures.hpp"
#include "wjs/scan/reader.hpp"
#include "wjs/scan/work.hpp"

namespace {

using namespace wjs::scan;
using namespace wjs::scan::synth;

Spec spec_of(Condition condition, std::size_t size, unsigned index) {
    Spec spec;
    spec.condition = condition;
    spec.split = Split::tuning;
    spec.size = size;
    spec.index = index;
    return spec;
}

// The frame again with its pixels as RGBA, and with a padded stride.
std::vector<std::uint8_t> as_rgba(const GrayImage& image) {
    std::vector<std::uint8_t> out;
    out.reserve(image.pixels.size() * 4);
    for (const std::uint8_t v : image.pixels) {
        out.insert(out.end(), {v, v, v, 255});
    }
    return out;
}

}  // namespace

TEST_CASE("a clean frame is read to the board that was drawn", "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);
    const auto& reading = fixtures::reading_of(spec);
    REQUIRE(reading.has_value());
    REQUIRE(reading->rows == 4);
    REQUIRE(reading->columns == 4);
    REQUIRE(reading->tiles.size() == 16);

    const Tally tally = judge(frame, reading);
    CHECK(tally.found == 1);
    CHECK(tally.correct >= 14);  // of 16

    // The reading is complete: every tile has its letters or says it has none, its
    // confidence is a share, its runners-up are other letters.
    for (const TileReading& tile : reading->tiles) {
        CHECK(tile.confidence >= 0.0);
        CHECK(tile.confidence <= 1.0);
        if (!tile.letters.empty()) {
            CHECK(tile.alternatives[0].letters != tile.letters);
            CHECK(tile.alternatives[1].letters != tile.alternatives[0].letters);
            // A tile is doubtful exactly when its confidence is under one half.
            CHECK(tile.doubtful == (tile.confidence < 0.5));
        }
    }
    // The size was counted from the picture and was clear.
    CHECK(reading->size_confidence > 0.3);
    CHECK(reading->dice_found >= 14);
    CHECK(reading->glyph_height_ratio > 0.4);
    CHECK(reading->glyph_height_ratio < 0.7);
    // The outline is in the frame's pixels, clockwise.
    for (const Point& corner : reading->corners) {
        CHECK(corner.x > 0.0);
        CHECK(corner.x < static_cast<double>(frame.image.width));
        CHECK(corner.y > 0.0);
        CHECK(corner.y < static_cast<double>(frame.image.height));
    }
    CHECK(signed_area(reading->corners) > 0.0);
}

TEST_CASE("the tile at a position is the one in row-major order", "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const auto& reading = fixtures::reading_of(spec);
    REQUIRE(reading.has_value());
    CHECK(&reading->tile({0, 0}) == &reading->tiles[0]);
    CHECK(&reading->tile({1, 2}) == &reading->tiles[6]);
    CHECK(&reading->tile({3, 3}) == &reading->tiles[15]);
}

TEST_CASE("the same picture in any pixel format, with any stride, reads the same",
          "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);
    const auto& expected = fixtures::reading_of(spec);
    REQUIRE(expected.has_value());

    const std::vector<std::uint8_t> rgba = as_rgba(frame.image);
    const Frame colour{std::span<const std::uint8_t>(rgba),
                       static_cast<std::size_t>(frame.image.width),
                       static_cast<std::size_t>(frame.image.height), 0, PixelFormat::rgba8};
    const auto from_colour = read_board(colour);
    REQUIRE(from_colour.has_value());
    REQUIRE(from_colour->tiles.size() == expected->tiles.size());
    for (std::size_t i = 0; i < expected->tiles.size(); ++i) {
        CHECK(from_colour->tiles[i].letters == expected->tiles[i].letters);
    }

    // Padding after each row: the Y plane of a camera buffer, say.
    const std::size_t width = static_cast<std::size_t>(frame.image.width);
    const std::size_t stride = width + 64;
    std::vector<std::uint8_t> padded(stride * static_cast<std::size_t>(frame.image.height), 17);
    for (int y = 0; y < frame.image.height; ++y) {
        std::copy_n(
            frame.image.pixels.begin() + static_cast<std::ptrdiff_t>(frame.image.index(0, y)),
            width,
            padded.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * stride));
    }
    const Frame strided{std::span<const std::uint8_t>(padded), width,
                        static_cast<std::size_t>(frame.image.height), stride, PixelFormat::gray8};
    const auto from_strided = read_board(strided);
    REQUIRE(from_strided.has_value());
    for (std::size_t i = 0; i < expected->tiles.size(); ++i) {
        CHECK(from_strided->tiles[i].letters == expected->tiles[i].letters);
    }
}

TEST_CASE("reading is a function of the frame and the options", "[scan][reader]") {
    const Spec spec = spec_of(Condition::angled, 4, 4);
    const Rendered& frame = fixtures::frame(spec);
    const auto first = read_board(frame.image.view());
    const auto second = read_board(frame.image.view());
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->tiles.size() == second->tiles.size());
    for (std::size_t i = 0; i < first->tiles.size(); ++i) {
        CHECK(first->tiles[i].letters == second->tiles[i].letters);
        CHECK(first->tiles[i].confidence == second->tiles[i].confidence);
        CHECK(first->tiles[i].doubtful == second->tiles[i].doubtful);
    }
    CHECK(first->corners == second->corners);
}

TEST_CASE("a size given in the options is the size read", "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);
    ReadOptions options;
    options.size = 4;
    const auto reading = read_board(frame.image.view(), options);
    REQUIRE(reading.has_value());
    CHECK(reading->rows == 4);
    CHECK(reading->size_confidence == 1.0);
    CHECK(judge(frame, reading).correct >= 14);
}

TEST_CASE("the doubt thresholds decide what is doubtful and nothing else", "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);

    ReadOptions never;
    never.min_score = -2.0;
    never.min_margin = -2.0;
    const auto trusting = read_board(frame.image.view(), never);
    REQUIRE(trusting.has_value());
    for (const TileReading& tile : trusting->tiles) {
        CHECK_FALSE(tile.doubtful);
    }

    ReadOptions always;
    always.min_score = 2.0;
    const auto doubting = read_board(frame.image.view(), always);
    REQUIRE(doubting.has_value());
    for (const TileReading& tile : doubting->tiles) {
        CHECK(tile.doubtful);
    }
    // The letters are the same either way.
    for (std::size_t i = 0; i < trusting->tiles.size(); ++i) {
        CHECK(trusting->tiles[i].letters == doubting->tiles[i].letters);
    }
}

TEST_CASE("a glyph height can be given instead of estimated", "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);
    ReadOptions options;
    options.estimate_glyph_height = false;
    options.glyph_height_ratio = frame.glyph_height;
    const auto reading = read_board(frame.image.view(), options);
    REQUIRE(reading.has_value());
    CHECK(reading->glyph_height_ratio == frame.glyph_height);
    CHECK(judge(frame, reading).correct >= 14);
}

TEST_CASE("a frame that is not a frame is refused", "[scan][reader]") {
    const std::vector<std::uint8_t> bytes(100, 0);
    const Frame short_buffer{std::span<const std::uint8_t>(bytes), 20, 20, 0, PixelFormat::gray8};
    const auto refused = read_board(short_buffer);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == ReadError::invalid_frame);
    CHECK(read_board(Frame{}).error() == ReadError::invalid_frame);
}

TEST_CASE("a frame whose sizes would wrap the arithmetic is refused, not read", "[scan][reader]") {
    // Width 2^62 + 1 of four-byte pixels: 4 * width wraps to 4, which the four bytes
    // below hold. It was a valid frame, and the luma loop then ran to the full width.
    const std::vector<std::uint8_t> bytes(4, 90);
    constexpr std::size_t huge = (std::size_t{1} << 62) + 1;
    const Frame hostile{std::span<const std::uint8_t>(bytes), huge, 1, 0, PixelFormat::rgba8};
    const auto refused = read_board(hostile);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == ReadError::invalid_frame);

    // And a stride that wraps the offset of a later row.
    const Frame strided{std::span<const std::uint8_t>(bytes), 2, 3, std::size_t{1} << 63,
                        PixelFormat::gray8};
    CHECK(read_board(strided).error() == ReadError::invalid_frame);
}

TEST_CASE("options out of range are refused before the frame is looked at", "[scan][reader]") {
    // A board_side of 0 had profiles_of dereference an empty profile; a negative one
    // wrapped a size_t into a request for an exabyte; a size of 500 read 250,000
    // cells; a threshold of 0 made a confidence of NaN.
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    CHECK(ReadOptions{}.valid());

    const std::vector<std::pair<const char*, ReadOptions>> bad = [] {
        std::vector<std::pair<const char*, ReadOptions>> list;
        const auto with = [&](const char* what, auto change) {
            ReadOptions options;
            change(options);
            list.emplace_back(what, options);
        };
        with("board_side 0", [](ReadOptions& o) { o.board_side = 0; });
        with("board_side -1", [](ReadOptions& o) { o.board_side = -1; });
        with("board_side 31", [](ReadOptions& o) { o.board_side = 31; });
        with("board_side 100000", [](ReadOptions& o) { o.board_side = 100000; });
        with("size 0", [](ReadOptions& o) { o.size = 0; });
        with("size 1", [](ReadOptions& o) { o.size = 1; });
        with("size 17", [](ReadOptions& o) { o.size = 17; });
        with("size 500", [](ReadOptions& o) { o.size = 500; });
        with("min_size 0", [](ReadOptions& o) { o.min_size = 0; });
        with("min_size 1", [](ReadOptions& o) { o.min_size = 1; });
        with("min_size over max", [](ReadOptions& o) { o.min_size = 6, o.max_size = 5; });
        with("max_size 17", [](ReadOptions& o) { o.max_size = 17; });
        with("working_size 0", [](ReadOptions& o) { o.working_size = 0; });
        with("working_size -480", [](ReadOptions& o) { o.working_size = -480; });
        with("working_size 15", [](ReadOptions& o) { o.working_size = 15; });
        with("analysis_size 0", [](ReadOptions& o) { o.analysis_size = 0; });
        with("analysis_size huge", [](ReadOptions& o) { o.analysis_size = 1 << 30; });
        with("glyph_height_ratio 0", [](ReadOptions& o) { o.glyph_height_ratio = 0.0; });
        with("glyph_height_ratio 2", [](ReadOptions& o) { o.glyph_height_ratio = 2.0; });
        with("glyph_height_ratio NaN", [&](ReadOptions& o) { o.glyph_height_ratio = nan; });
        with("min_score 0", [](ReadOptions& o) { o.min_score = 0.0; });
        with("min_score NaN", [&](ReadOptions& o) { o.min_score = nan; });
        with("min_score inf", [&](ReadOptions& o) { o.min_score = inf; });
        with("min_margin 0", [](ReadOptions& o) { o.min_margin = 0.0; });
        with("min_margin NaN", [&](ReadOptions& o) { o.min_margin = nan; });
        with("min_contrast -1", [](ReadOptions& o) { o.min_contrast = -1.0; });
        with("min_contrast NaN", [&](ReadOptions& o) { o.min_contrast = nan; });
        return list;
    }();

    // A blank frame, so that a refusal is told from an answer by its error.
    const GrayImage blank(320, 240, 120);
    for (const auto& [what, options] : bad) {
        INFO(what);
        CHECK_FALSE(options.valid());
        const auto refused = read_board(blank.view(), options);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error() == ReadError::invalid_options);
    }

    // What the tuning tools do is still allowed: a negative threshold is the doubt
    // switched off, and a contrast of zero asks for no ink at all.
    ReadOptions open;
    open.min_score = -2.0;
    open.min_margin = -2.0;
    open.min_contrast = 0.0;
    CHECK(open.valid());
    CHECK(read_board(blank.view(), open).error() == ReadError::board_not_found);
    ReadOptions sized;
    sized.size = 16;
    sized.board_side = 4096;
    CHECK(sized.valid());
    CHECK(read_board(blank.view()).error() == ReadError::board_not_found);
}

TEST_CASE("a frame that is not valid is refused before its options are", "[scan][reader]") {
    ReadOptions bad;
    bad.board_side = 0;
    CHECK(read_board(Frame{}, bad).error() == ReadError::invalid_frame);
}

TEST_CASE("a board with no letters on it is answered with no_letters", "[scan][reader]") {
    // A real lattice of blank dice, found and counted, with nothing to read in it.
    Spec spec = spec_of(Condition::upright, 4, 3);
    const Rendered blank = render_blank(spec);
    ReadOptions options;
    options.size = 4;
    const auto reading = read_board(blank.image.view(), options);
    REQUIRE_FALSE(reading.has_value());
    CHECK(reading.error() == ReadError::no_letters);
    CHECK(to_string(reading.error()) != to_string(ReadError::board_not_found));
}

TEST_CASE("a board whose cells cannot be counted is answered with size_not_found",
          "[scan][reader]") {
    // A 4x4 board of 16 dice, asked to be counted as 7 or 8 a side: 49 cells for 16
    // dice is over three times as many, so no count in range fits.
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);
    ReadOptions options;
    options.min_size = 7;
    options.max_size = 8;
    const auto reading = read_board(frame.image.view(), options);
    REQUIRE_FALSE(reading.has_value());
    CHECK(reading.error() == ReadError::size_not_found);

    // And when no count in range is clear: the same frame is a board with the default range.
    CHECK(read_board(frame.image.view()).has_value());
}

TEST_CASE("a frame with no board is answered with board_not_found", "[scan][reader]") {
    const GrayImage blank(640, 480, 120);
    const auto reading = read_board(blank.view());
    REQUIRE_FALSE(reading.has_value());
    CHECK(reading.error() == ReadError::board_not_found);
}

TEST_CASE("a lattice of dice with no letters on them is not a board", "[scan][reader]") {
    // A tiled floor: bright squares at a pitch, nothing on them. None of seven is
    // answered with a board, not even one whose tiles are all doubted: this used to
    // pass on either, and so on a reader that believed a floor and doubted every tile.
    for (unsigned index = 0; index < 7; ++index) {
        const GrayImage tiles = render_negative(NegativeKind::tiles, index);
        const auto reading = read_board(tiles.view());
        INFO("floor " << index);
        REQUIRE_FALSE(reading.has_value());
        CHECK((reading.error() == ReadError::no_letters ||
               reading.error() == ReadError::board_not_found ||
               reading.error() == ReadError::size_not_found));
    }
}

TEST_CASE("each error says what happened without blaming anyone", "[scan][reader]") {
    for (const ReadError error :
         {ReadError::invalid_frame, ReadError::board_not_found, ReadError::size_not_found,
          ReadError::no_letters, ReadError::invalid_options}) {
        CHECK_FALSE(to_string(error).empty());
    }
}

TEST_CASE("a read counts what it did, and a warm read allocates no pixel buffers",
          "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);

    // The thread's scratch pool (libs/scan/src/scratch.hpp) is warmed by a first
    // read at this size, so the counted read below is the steady state.
    (void)read_board(frame.image.view());
    reset_scan_work();
    const auto reading = read_board(frame.image.view());
    REQUIRE(reading.has_value());
    const ScanWork work = scan_work();
    CHECK(work.reads == 1);
    CHECK(work.frame_pixels == 960U * 720U);
    CHECK(work.working_pixels == 480U * 360U);
    // The 16 cells once, plus the glyph-height search's 8 sample cells at each
    // of 5 trial heights, less the 8 sample cells taken from the winning trial
    // instead of being read again: exactly 48.
    CHECK(work.cells_read == 48);
    CHECK(work.comparisons >= 16 * 48 * 27);
    CHECK(work.blobs >= 16);
    CHECK(work.dice_candidates >= 16);
    CHECK(work.warp_pixels >= 480U * 480U);
    // Every pixel buffer came from the scratch: nothing was allocated, and the
    // reuses are the whole set of the read's acquisitions.
    CHECK(work.buffers == 0);
    CHECK(work.largest_buffers == 0);
    CHECK(work.buffer_reuses >= 16);

    // A read of a frame with no board gets no further than looking for one.
    reset_scan_work();
    (void)read_board(GrayImage(640, 480, 120).view());
    const ScanWork nothing = scan_work();
    CHECK(nothing.reads == 1);
    CHECK(nothing.cells_read == 0);
    CHECK(nothing.warp_pixels == 0);
}

TEST_CASE("a glyph height given in the options reads every cell once and no samples",
          "[scan][reader]") {
    const Spec spec = spec_of(Condition::upright, 4, 2);
    const Rendered& frame = fixtures::frame(spec);
    ReadOptions options;
    options.estimate_glyph_height = false;
    options.glyph_height_ratio = frame.glyph_height;

    (void)read_board(frame.image.view(), options);
    reset_scan_work();
    const auto reading = read_board(frame.image.view(), options);
    REQUIRE(reading.has_value());
    const ScanWork work = scan_work();
    CHECK(work.cells_read == 16);
    CHECK(work.buffers == 0);
}
