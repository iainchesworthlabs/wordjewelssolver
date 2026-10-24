// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scan/segment.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "scratch.hpp"
#include "wjs/profiling.hpp"
#include "wjs/scan/work.hpp"

namespace wjs::scan {

namespace {

// The largest blur radius used as it is. A box this wide holds every pixel of any
// picture the reader reads, so a larger one cannot blur more.
constexpr int kMaxBlurRadius = 1 << 20;

// A union-find over blob labels, labels counted from 1 (0 is the background).
class Labels {
public:
    Labels() : parent_(1, 0) {}

    std::int32_t make() {
        const auto label = static_cast<std::int32_t>(parent_.size());
        parent_.push_back(label);
        return label;
    }

    std::int32_t find(std::int32_t label) {
        std::int32_t root = label;
        while (parent_[static_cast<std::size_t>(root)] != root) {
            root = parent_[static_cast<std::size_t>(root)];
        }
        while (parent_[static_cast<std::size_t>(label)] != root) {
            const std::int32_t next = parent_[static_cast<std::size_t>(label)];
            parent_[static_cast<std::size_t>(label)] = root;
            label = next;
        }
        return root;
    }

    void join(std::int32_t a, std::int32_t b) {
        const std::int32_t root_a = find(a);
        const std::int32_t root_b = find(b);
        if (root_a < root_b) {
            parent_[static_cast<std::size_t>(root_b)] = root_a;
        } else if (root_b < root_a) {
            parent_[static_cast<std::size_t>(root_a)] = root_b;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return parent_.size(); }

private:
    std::vector<std::int32_t> parent_;
};

// Whether a mask of `mask_size` bytes holds a picture of width x height, and the
// picture is one whose labels fit an int32 (a pixel makes at most one label).
bool blob_buffers_fit(std::size_t mask_size, int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    return pixels <= (std::uint64_t{1} << 30) && mask_size >= pixels;
}

}  // namespace

void detail::box_blur_into(GrayImage& out, std::span<std::uint32_t> across, const GrayImage& image,
                           int radius) {
    if (radius <= 0 || image.empty()) {
        out = image;
        return;
    }
    // A radius past this is the same picture in effect (the edge repeats), and
    // would put the sums of one pass past 32 bits.
    radius = std::min(radius, kMaxBlurRadius);
    const int width = image.width;
    const int height = image.height;
    const int span = 2 * radius + 1;

    // Rows, into a wider integer buffer so the second pass does not round twice.
    // `across` holds width * height sums; the reader hands the scratch's buffer.
    for (int y = 0; y < height; ++y) {
        std::uint32_t sum = 0;
        for (int k = -radius; k <= radius; ++k) {
            sum += image.at(std::clamp(k, 0, width - 1), y);
        }
        for (int x = 0; x < width; ++x) {
            across[image.index(x, y)] = sum;
            sum += image.at(std::min(x + radius + 1, width - 1), y);
            sum -= image.at(std::max(x - radius, 0), y);
        }
    }

    detail::acquire_image(out, width, height);
    // 64 bits: a radius of 2048 puts the second pass's sums past 32.
    const std::uint64_t area = static_cast<std::uint64_t>(span) * static_cast<std::uint64_t>(span);
    for (int x = 0; x < width; ++x) {
        std::uint64_t sum = 0;
        for (int k = -radius; k <= radius; ++k) {
            sum += across[image.index(x, std::clamp(k, 0, height - 1))];
        }
        for (int y = 0; y < height; ++y) {
            out.at(x, y) = static_cast<std::uint8_t>((sum + area / 2U) / area);
            sum += across[image.index(x, std::min(y + radius + 1, height - 1))];
            sum -= across[image.index(x, std::max(y - radius, 0))];
        }
    }
}

GrayImage box_blur(const GrayImage& image, int radius) {
    if (radius <= 0 || image.empty()) {
        return image;
    }
    GrayImage out;
    std::vector<std::uint32_t> across;
    const std::span<std::uint32_t> sums = detail::acquire(across, image.pixels.size());
    detail::box_blur_into(out, sums, image, radius);
    return out;
}

void detail::summed_area(std::span<std::uint64_t> table, const GrayImage& image) {
    const int width = image.width;
    const int height = image.height;
    // One row and one column larger so a window is four reads; the border is
    // zero and is written here, because the buffer may be a reused one.
    const std::size_t stride = static_cast<std::size_t>(width) + 1;
    for (int x = 0; x <= width; ++x) {
        table[static_cast<std::size_t>(x)] = 0;
    }
    for (int y = 1; y <= height; ++y) {
        table[static_cast<std::size_t>(y) * stride] = 0;
    }
    for (int y = 0; y < height; ++y) {
        std::uint64_t row = 0;
        for (int x = 0; x < width; ++x) {
            row += image.at(x, y);
            table[static_cast<std::size_t>(y + 1) * stride + static_cast<std::size_t>(x + 1)] =
                table[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x + 1)] + row;
        }
    }
}

void detail::bright_mask_into(std::span<std::uint8_t> mask, std::span<const std::uint64_t> table,
                              const GrayImage& image, int window, int margin) {
    const int width = image.width;
    const int height = image.height;
    if (image.empty()) {
        // The same answer as a fresh zeroed mask: the buffer may be reused.
        std::ranges::fill(mask, std::uint8_t{0});
        return;
    }

    const std::size_t stride = static_cast<std::size_t>(width) + 1;
    // A window under one pixel is one pixel (a half-side under zero would put the
    // window's far edge before its near one).
    const int half = std::max(window, 1) / 2;
    for (int y = 0; y < height; ++y) {
        const int y0 = std::max(0, y - half);
        const int y1 = std::min(height, y + half + 1);
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - half);
            const int x1 = std::min(width, x + half + 1);
            const auto at = [&](int cx, int cy) {
                return static_cast<std::int64_t>(
                    table[static_cast<std::size_t>(cy) * stride + static_cast<std::size_t>(cx)]);
            };
            const std::int64_t sum = at(x1, y1) - at(x0, y1) - at(x1, y0) + at(x0, y0);
            const std::int64_t count = static_cast<std::int64_t>(x1 - x0) * (y1 - y0);
            // pixel > sum / count + margin, without dividing. Every element is
            // written (the mask is reused, so the 0s are written too).
            mask[image.index(x, y)] =
                static_cast<std::int64_t>(image.at(x, y)) * count > sum + margin * count
                    ? std::uint8_t{1}
                    : std::uint8_t{0};
        }
    }
}

std::vector<std::uint8_t> bright_mask(const GrayImage& image, int window, int margin) {
    const int width = image.width;
    const int height = image.height;
    std::vector<std::uint8_t> mask;
    const std::span<std::uint8_t> bytes =
        detail::acquire(mask, static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    if (image.empty()) {
        std::ranges::fill(bytes, std::uint8_t{0});
        return mask;
    }

    // The summed-area table, one row and one column larger so a window is four
    // reads.
    const std::size_t stride = static_cast<std::size_t>(width) + 1;
    std::vector<std::uint64_t> table;
    const std::span<std::uint64_t> sums =
        detail::acquire(table, stride * (static_cast<std::size_t>(height) + 1));
    detail::summed_area(sums, image);
    detail::bright_mask_into(bytes, sums, image, window, margin);
    return mask;
}

std::vector<Blob> detail::find_blobs(std::span<const std::uint8_t> mask, int width, int height,
                                     std::int64_t min_area, std::int64_t max_area,
                                     std::span<std::int32_t> labels) {
    [[maybe_unused]] auto zone = profiling::zone<"scan.find_blobs">();
    if (!blob_buffers_fit(mask.size(), width, height) ||
        labels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return {};
    }
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    Labels sets;

    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t i = y * w + x;
            if (mask[i] == 0) {
                // The labels are a reused buffer: background pixels are written
                // as well as set ones, so a stale label is never read.
                labels[i] = 0;
                continue;
            }
            const std::int32_t left = x > 0 ? labels[i - 1] : 0;
            const std::int32_t up = y > 0 ? labels[i - w] : 0;
            if (left == 0 && up == 0) {
                labels[i] = sets.make();
            } else if (left != 0 && up != 0) {
                labels[i] = std::min(left, up);
                sets.join(left, up);
            } else {
                labels[i] = left != 0 ? left : up;
            }
        }
    }

    // Each pixel to its set's root; the area and the centroid sums of each.
    struct Sums {
        std::int64_t area = 0;
        double x = 0.0;
        double y = 0.0;
    };
    std::vector<Sums> sums(sets.size());
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t i = y * w + x;
            if (labels[i] == 0) {
                continue;
            }
            labels[i] = sets.find(labels[i]);
            Sums& s = sums[static_cast<std::size_t>(labels[i])];
            ++s.area;
            s.x += static_cast<double>(x) + 0.5;
            s.y += static_cast<double>(y) + 0.5;
        }
    }

    // The blobs of a size worth looking at, each given a slot for its outline.
    std::vector<std::int32_t> slot(sets.size(), -1);
    std::vector<Blob> blobs;
    std::uint64_t seen = 0;
    for (std::size_t label = 1; label < sets.size(); ++label) {
        const Sums& s = sums[label];
        if (s.area == 0) {
            continue;
        }
        ++seen;
        if (s.area < min_area || s.area > max_area) {
            continue;
        }
        slot[label] = static_cast<std::int32_t>(blobs.size());
        Blob blob;
        blob.area = s.area;
        blob.centre = {s.x / static_cast<double>(s.area), s.y / static_cast<double>(s.area)};
        blobs.push_back(std::move(blob));
    }
    detail::scan_tally().blobs += seen;

    // The outline: the corners of every pixel with a neighbour that is not in
    // the same blob.
    std::vector<std::vector<Point>> outlines(blobs.size());
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t i = y * w + x;
            const std::int32_t label = labels[i];
            if (label == 0 || slot[static_cast<std::size_t>(label)] < 0) {
                continue;
            }
            const bool edge = x == 0 || y == 0 || x + 1 == w || y + 1 == h ||
                              labels[i - 1] != label || labels[i + 1] != label ||
                              labels[i - w] != label || labels[i + w] != label;
            if (!edge) {
                continue;
            }
            auto& outline =
                outlines[static_cast<std::size_t>(slot[static_cast<std::size_t>(label)])];
            const auto px = static_cast<double>(x);
            const auto py = static_cast<double>(y);
            outline.push_back({px, py});
            outline.push_back({px + 1.0, py});
            outline.push_back({px + 1.0, py + 1.0});
            outline.push_back({px, py + 1.0});
        }
    }
    for (std::size_t k = 0; k < blobs.size(); ++k) {
        blobs[k].hull = convex_hull(std::move(outlines[k]));
        blobs[k].hull_area = std::abs(signed_area(blobs[k].hull));
    }
    return blobs;
}

std::vector<Blob> find_blobs(std::span<const std::uint8_t> mask, int width, int height,
                             std::int64_t min_area, std::int64_t max_area) {
    if (!blob_buffers_fit(mask.size(), width, height)) {
        return {};
    }
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    std::vector<std::int32_t> labels;
    const std::span<std::int32_t> room = detail::acquire(labels, w * h);
    return detail::find_blobs(mask, width, height, min_area, max_area, room);
}

}  // namespace wjs::scan
