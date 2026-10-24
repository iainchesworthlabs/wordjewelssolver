// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The reader's scratch: one set of pixel buffers a thread, reused across reads
// so that a steady read allocates nothing. This is the pipeline's own header;
// it is not installed, and nothing outside libs/scan/src includes it.
//
// Each pooled buffer is handed out without being cleared: the contract is that
// the caller writes every byte it will read back (the loops below were each
// changed to write both branches rather than memset, and the summed-area table
// writes its own zero border). A buffer grows only when a larger picture than
// any before it is asked for; a growth is a counted allocation
// (wjs/scan/work.hpp), and every other hand-out is a counted reuse, so the
// performance tier can hold "a warm read allocates no pixel buffers" exactly.
//
// The pool is thread-local, not shared: two threads reading at once keep their
// own buffers and cannot disturb each other, which is what makes read_board
// thread-safe without a lock.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wjs/scan/board_finder.hpp"
#include "wjs/scan/image.hpp"
#include "wjs/scan/segment.hpp"
#include "wjs/scan/work.hpp"

namespace wjs::scan::detail {

struct Scratch {
    // The pipeline's owned pictures, in the order the reader makes them.
    GrayImage luma;
    GrayImage analysis;
    GrayImage working;
    // find_board's: the blurred working picture, its summed-area table (built
    // once and shared by the five looks, where each look built its own before),
    // the bright mask and the blob labels (one each, reused look to look), and
    // the 240-pixel square each outline attempt is drawn into.
    GrayImage blurred;
    std::vector<std::uint32_t> blur_across;
    std::vector<std::uint64_t> sat;
    std::vector<std::uint8_t> mask;
    std::vector<std::int32_t> labels;
    GrayImage look;
    // The board drawn square at the reader's side.
    GrayImage board;
};

// One pool per thread, shared by every call in that thread (an inline function's
// static is one object across translation units).
[[nodiscard]] inline Scratch& thread_scratch() noexcept {
    static thread_local Scratch scratch;
    return scratch;
}

// A raw pixel buffer of n elements: grown (and counted) only when it must be,
// counted as a reuse otherwise. The caller writes every element it reads.
template <typename T>
[[nodiscard]] inline std::span<T> acquire(std::vector<T>& buffer, std::size_t n) {
    if (buffer.capacity() < n) {
        buffer.reserve(n);
        detail::note_buffer();
    } else {
        ++detail::scan_tally().buffer_reuses;
    }
    if (buffer.size() < n) {
        buffer.resize(n);
    }
    return std::span<T>(buffer.data(), n);
}

// A pooled image of width x height pixels: grown and counted as acquire() is.
// The caller writes every pixel, and uses the image through the reference it
// passed in, so the return value is nothing to keep.
inline GrayImage& acquire_image(GrayImage& slot, int width, int height) {
    if (width <= 0 || height <= 0) {
        // Nothing to hold: an empty picture, its buffer kept for the next use.
        slot.width = 0;
        slot.height = 0;
        slot.pixels.clear();
        return slot;
    }
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (slot.pixels.capacity() < n) {
        slot.pixels.reserve(n);
        detail::note_buffer();
    } else {
        ++detail::scan_tally().buffer_reuses;
    }
    if (slot.pixels.size() < n) {
        slot.pixels.resize(n);
    }
    slot.width = width;
    slot.height = height;
    return slot;
}

// image.cpp: the luma of a frame into a pooled image, and the block average of
// an image into another. `shrink_into` leaves `out` untouched when the source
// already fits (factor 1): the caller keeps using the source.
void luma_into(GrayImage& out, const Frame& frame);
void shrink_into(GrayImage& out, const GrayImage& image, int max_long_side, int& factor);

// segment.cpp: the pooled forms of the public operations, plus the summed-area
// table that the five looks share.
void box_blur_into(GrayImage& out, std::span<std::uint32_t> across, const GrayImage& image,
                   int radius);
void summed_area(std::span<std::uint64_t> table, const GrayImage& image);
void bright_mask_into(std::span<std::uint8_t> mask, std::span<const std::uint64_t> table,
                      const GrayImage& image, int window, int margin);
std::vector<Blob> find_blobs(std::span<const std::uint8_t> mask, int width, int height,
                             std::int64_t min_area, std::int64_t max_area,
                             std::span<std::int32_t> labels);

// board_finder.cpp: the board drawn square into a pooled image, and the search
// over the five looks with its buffers from the scratch.
void warp_into(GrayImage& out, const GrayImage& source, const Quad& corners, int side);
std::optional<BoardFind> find_board_using(const GrayImage& image, std::size_t min_dice,
                                          Scratch& scratch);

}  // namespace wjs::scan::detail
