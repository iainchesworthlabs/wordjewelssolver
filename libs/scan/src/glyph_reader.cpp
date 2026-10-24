// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scan/glyph_reader.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

#include "wjs/scan/stroke_font.hpp"
#include "wjs/scan/work.hpp"

namespace wjs::scan {

namespace {

constexpr int kPatch = 24;
constexpr std::size_t kPatchPixels = static_cast<std::size_t>(kPatch) * kPatch;
constexpr int kAngles = 48;
constexpr double kAngleStep = 360.0 / kAngles;
// Half-sides of the patch a unit of the stroke box spans: the box is 6 units
// tall, so a glyph is 1.45 half-sides high, which is 55% of a die when the
// patch's half-side is 0.38 of the die.
constexpr double kBoxUnit = 0.2417;
// The share of the cell's darkest pixels (by depth below the plane) taken as
// the ink's depth.
constexpr double kInkPercentile = 0.96;

// Less ink than this is none, whatever contrast was asked for: the ink map is
// made by dividing by the ink's depth.
constexpr double kMinInkDepth = 1e-6;

using PatchMap = std::array<float, kPatchPixels>;

// What one cell's read works in: about 35 KB of fixed arrays. On the stack that is
// a frame large enough for the analyser to object to (C6262), and it is not
// allocated per cell either way: this is one set per thread, so reads on two
// threads do not touch. Each array is written in full before it is read, except
// `ink`, which is cleared.
struct Work {
    PatchMap grey;
    std::array<double, kPatchPixels> values;
    std::array<double, kPatchPixels> sorted;
    std::array<double, kPatchPixels> pu;
    std::array<double, kPatchPixels> pv;
    std::array<double, kPatchPixels> pz;
    std::array<double, kPatchPixels> depth;
    PatchMap ink;
    std::array<float, kPatchPixels> turned;
};

Work& thread_work() noexcept {
    static thread_local Work work{};
    return work;
}

// The pixels of the patch inside its inscribed circle, and where they are, with
// the patch's half-side as the unit. There are at most kPatchPixels of them, so
// the per-cell work below can keep its values in fixed arrays instead of
// allocating a handful of vectors a cell.
struct Circle {
    std::vector<std::size_t> pixel;
    std::vector<double> u;
    std::vector<double> v;
};

Circle make_circle() {
    Circle circle;
    for (int j = 0; j < kPatch; ++j) {
        for (int i = 0; i < kPatch; ++i) {
            const double u = (i + 0.5) * 2.0 / kPatch - 1.0;
            const double v = (j + 0.5) * 2.0 / kPatch - 1.0;
            if (u * u + v * v <= 1.0) {
                circle.pixel.push_back(static_cast<std::size_t>(j) * kPatch +
                                       static_cast<std::size_t>(i));
                circle.u.push_back(u);
                circle.v.push_back(v);
            }
        }
    }
    return circle;
}

const Circle& circle() {
    static const Circle c = make_circle();
    return c;
}

// The cosine and sine of each turn, computed once: the same values the reader
// used to compute for every angle of every cell (96 trigonometic calls a cell).
struct Trig {
    double cos = 0.0;
    double sin = 0.0;
};

const std::array<Trig, kAngles>& turns() {
    static const std::array<Trig, kAngles> table = [] {
        std::array<Trig, kAngles> t{};
        for (int k = 0; k < kAngles; ++k) {
            const double radians = k * kAngleStep * std::numbers::pi / 180.0;
            t[static_cast<std::size_t>(k)] = {std::cos(radians), std::sin(radians)};
        }
        return t;
    }();
    return table;
}

// A 3 x 3 box blur of a patch map, its edge repeated.
void blur3(PatchMap& map) {
    PatchMap out{};
    for (int j = 0; j < kPatch; ++j) {
        for (int i = 0; i < kPatch; ++i) {
            float sum = 0.0F;
            for (int dj = -1; dj <= 1; ++dj) {
                for (int di = -1; di <= 1; ++di) {
                    const int x = std::clamp(i + di, 0, kPatch - 1);
                    const int y = std::clamp(j + dj, 0, kPatch - 1);
                    sum += map[static_cast<std::size_t>(y) * kPatch + static_cast<std::size_t>(x)];
                }
            }
            out[static_cast<std::size_t>(j) * kPatch + static_cast<std::size_t>(i)] = sum / 9.0F;
        }
    }
    map = out;
}

// Takes the mean out of the values and scales them to a length of 1. False if
// they have no variation to speak of.
bool normalise(std::span<float> values) {
    double sum = 0.0;
    for (const float x : values) {
        sum += static_cast<double>(x);
    }
    const double mean = sum / static_cast<double>(values.size());
    double squares = 0.0;
    for (float& x : values) {
        const double centred = static_cast<double>(x) - mean;
        x = static_cast<float>(centred);
        squares += centred * centred;
    }
    if (squares < 1e-9) {
        return false;
    }
    const auto scale = static_cast<float>(1.0 / std::sqrt(squares));
    for (float& x : values) {
        x *= scale;
    }
    return true;
}

float dot(std::span<const float> a, std::span<const float> b) {
    float s0 = 0.0F;
    float s1 = 0.0F;
    float s2 = 0.0F;
    float s3 = 0.0F;
    const std::size_t n = a.size();
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    for (; i < n; ++i) {
        s0 += a[i] * b[i];
    }
    return (s0 + s1) + (s2 + s3);
}

// A glyph drawn upright into a patch: the anti-aliased coverage of its strokes at
// the nominal weight, blurred and cut to the circle.
std::vector<float> render_template(const StrokeGlyph& glyph) {
    PatchMap ink{};
    const double half_width = kNominalStrokeWidth / 2.0;
    const double pixel = (2.0 / kPatch) / kBoxUnit;
    for (int j = 0; j < kPatch; ++j) {
        for (int i = 0; i < kPatch; ++i) {
            const double u = (i + 0.5) * 2.0 / kPatch - 1.0;
            const double v = (j + 0.5) * 2.0 / kPatch - 1.0;
            const Point box{2.0 + u / kBoxUnit, 3.0 + v / kBoxUnit};
            const double d = distance_to_strokes(glyph.strokes, box);
            ink[static_cast<std::size_t>(j) * kPatch + static_cast<std::size_t>(i)] =
                static_cast<float>(std::clamp((half_width - d) / pixel + 0.5, 0.0, 1.0));
        }
    }
    blur3(ink);
    blur3(ink);

    const Circle& c = circle();
    std::vector<float> values(c.pixel.size());
    for (std::size_t m = 0; m < values.size(); ++m) {
        values[m] = ink[c.pixel[m]];
    }
    normalise(std::span<float>(values));
    return values;
}

// A plane z = a + b u + c v through the points, least squares.
struct Plane {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;

    [[nodiscard]] double at(double u, double v) const noexcept { return a + b * u + c * v; }
};

Plane fit_plane(const double* u, const double* v, const double* z, std::size_t n) {
    double count = 0.0;
    double su = 0.0;
    double sv = 0.0;
    double suu = 0.0;
    double suv = 0.0;
    double svv = 0.0;
    double sz = 0.0;
    double suz = 0.0;
    double svz = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        count += 1.0;
        su += u[k];
        sv += v[k];
        suu += u[k] * u[k];
        suv += u[k] * v[k];
        svv += v[k] * v[k];
        sz += z[k];
        suz += u[k] * z[k];
        svz += v[k] * z[k];
    }
    if (count < 3.0) {
        return {count > 0.0 ? sz / count : 0.0, 0.0, 0.0};
    }
    const double det =
        count * (suu * svv - suv * suv) - su * (su * svv - suv * sv) + sv * (su * suv - suu * sv);
    if (std::abs(det) < 1e-9) {
        return {sz / count, 0.0, 0.0};
    }
    // Cramer's rule on [[n su sv] [su suu suv] [sv suv svv]] (a b c) = (sz suz svz).
    const double da =
        sz * (suu * svv - suv * suv) - su * (suz * svv - suv * svz) + sv * (suz * suv - suu * svz);
    const double db =
        count * (suz * svv - suv * svz) - sz * (su * svv - suv * sv) + sv * (su * svz - suz * sv);
    const double dc =
        count * (suu * svz - suz * suv) - su * (su * svz - suz * sv) + sz * (su * suv - suu * sv);
    return {da / det, db / det, dc / det};
}

float bilinear(const PatchMap& map, double iu, double iv) {
    const double x = std::clamp(iu, 0.0, kPatch - 1.0);
    const double y = std::clamp(iv, 0.0, kPatch - 1.0);
    const auto x0 = static_cast<int>(x);
    const auto y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, kPatch - 1);
    const int y1 = std::min(y0 + 1, kPatch - 1);
    const auto tx = static_cast<float>(x - x0);
    const auto ty = static_cast<float>(y - y0);
    const auto at = [&](int cx, int cy) {
        return map[static_cast<std::size_t>(cy) * kPatch + static_cast<std::size_t>(cx)];
    };
    const float top = at(x0, y0) * (1.0F - tx) + at(x1, y0) * tx;
    const float bottom = at(x0, y1) * (1.0F - tx) + at(x1, y1) * tx;
    return top * (1.0F - ty) + bottom * ty;
}

}  // namespace

GlyphReader::GlyphReader() {
    const auto& font = stroke_font();
    templates_.reserve(font.size());
    for (const StrokeGlyph& glyph : font) {
        templates_.push_back(render_template(glyph));
    }
}

const GlyphReader& GlyphReader::standard() {
    static const GlyphReader reader;
    return reader;
}

GlyphRead GlyphReader::read(const GrayImage& board, Point centre, double half_side,
                            double min_contrast) const {
    Work& work = thread_work();
    GlyphRead result;
    const Circle& c = circle();
    const std::size_t count = c.pixel.size();
    ++detail::scan_tally().cells_read;

    // The patch's grey levels, each averaged over 3 x 3 samples of the pixel's
    // footprint in the board image.
    PatchMap& grey = work.grey;  // every pixel is written below
    for (int j = 0; j < kPatch; ++j) {
        for (int i = 0; i < kPatch; ++i) {
            double sum = 0.0;
            for (int sy = 0; sy < 3; ++sy) {
                for (int sx = 0; sx < 3; ++sx) {
                    const double u = (i + (sx + 0.5) / 3.0) * 2.0 / kPatch - 1.0;
                    const double v = (j + (sy + 0.5) / 3.0) * 2.0 / kPatch - 1.0;
                    sum += sample(board, centre.x + u * half_side, centre.y + v * half_side);
                }
            }
            grey[static_cast<std::size_t>(j) * kPatch + static_cast<std::size_t>(i)] =
                static_cast<float>(sum / 9.0);
        }
    }

    // The die's own light: a plane through the brighter half of the circle. The
    // glyph is well under half of it, so that half is die. The values live in
    // fixed arrays: the circle holds at most kPatchPixels, so nothing is
    // allocated for one cell.
    auto& values = work.values;
    for (std::size_t m = 0; m < count; ++m) {
        values[m] = static_cast<double>(grey[c.pixel[m]]);
    }
    auto& sorted = work.sorted;
    std::copy_n(values.begin(), static_cast<std::ptrdiff_t>(count), sorted.begin());
    std::ranges::nth_element(sorted.begin(),
                             sorted.begin() + static_cast<std::ptrdiff_t>(count / 2),
                             sorted.begin() + static_cast<std::ptrdiff_t>(count));
    const double median = sorted[count / 2];
    auto& pu = work.pu;
    auto& pv = work.pv;
    auto& pz = work.pz;
    std::size_t bright = 0;
    for (std::size_t m = 0; m < count; ++m) {
        if (values[m] >= median) {
            pu[bright] = c.u[m];
            pv[bright] = c.v[m];
            pz[bright] = values[m];
            ++bright;
        }
    }
    const Plane light = fit_plane(pu.data(), pv.data(), pz.data(), bright);

    // How deep each pixel is below that light, and how deep the ink is.
    auto& depth = work.depth;
    for (std::size_t m = 0; m < count; ++m) {
        depth[m] = light.at(c.u[m], c.v[m]) - values[m];
    }
    std::copy_n(depth.begin(), static_cast<std::ptrdiff_t>(count), sorted.begin());
    const auto rank = static_cast<std::size_t>(kInkPercentile * static_cast<double>(count - 1));
    std::ranges::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(rank),
                             sorted.begin() + static_cast<std::ptrdiff_t>(count));
    const double ink_depth = sorted[rank];
    result.contrast = std::max(ink_depth, 0.0);
    // A contrast of zero asked for would let a patch with no ink in it through to
    // divide by its depth; there is no glyph in a patch that is flat.
    if (result.contrast < min_contrast || ink_depth < kMinInkDepth) {
        return result;
    }
    result.has_ink = true;

    PatchMap& ink = work.ink;
    ink.fill(0.0F);  // the blur spreads into the corners, which are not the circle's
    for (std::size_t m = 0; m < count; ++m) {
        ink[c.pixel[m]] = static_cast<float>(std::clamp(depth[m] / ink_depth, 0.0, 1.0));
    }
    blur3(ink);
    blur3(ink);

    // The ink at each of the turns, against each template.
    struct Best {
        float score = -2.0F;
        int angle = 0;
    };
    std::array<Best, kGlyphCount> best{};
    auto& turned = work.turned;
    bool compared = false;
    const std::array<Trig, kAngles>& table = turns();
    for (int k = 0; k < kAngles; ++k) {
        const double cos_a = table[static_cast<std::size_t>(k)].cos;
        const double sin_a = table[static_cast<std::size_t>(k)].sin;
        for (std::size_t m = 0; m < count; ++m) {
            // The template at p is the observed ink at the point p turned by the angle.
            const double qu = c.u[m] * cos_a - c.v[m] * sin_a;
            const double qv = c.u[m] * sin_a + c.v[m] * cos_a;
            turned[m] =
                bilinear(ink, (qu + 1.0) * kPatch / 2.0 - 0.5, (qv + 1.0) * kPatch / 2.0 - 0.5);
        }
        const std::span<float> span(turned.data(), count);
        if (!normalise(span)) {
            continue;
        }
        compared = true;
        for (std::size_t g = 0; g < kGlyphCount; ++g) {
            const float score =
                dot(span, std::span<const float>(templates_[g].data(), templates_[g].size()));
            if (score > best[g].score) {
                best[g] = {score, k};
            }
        }
    }
    if (!compared) {
        // No turn of the ink had any variation to compare: nothing was read, and the
        // scores below would be the starting value of every glyph, in no order.
        result.has_ink = false;
        return result;
    }
    detail::scan_tally().comparisons += static_cast<std::uint64_t>(kAngles) * kGlyphCount;

    std::array<std::size_t, kGlyphCount> order{};
    for (std::size_t g = 0; g < kGlyphCount; ++g) {
        order[g] = g;
    }
    std::ranges::partial_sort(
        order, order.begin() + static_cast<std::ptrdiff_t>(result.best.size()),
        [&](std::size_t a, std::size_t b) { return best[a].score > best[b].score; });
    for (std::size_t r = 0; r < result.best.size(); ++r) {
        const std::size_t g = order[r];
        result.best[r] = {g, static_cast<double>(best[g].score), best[g].angle * kAngleStep};
    }
    result.found = result.best.size();
    return result;
}

}  // namespace wjs::scan
