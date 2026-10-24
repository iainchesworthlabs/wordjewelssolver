// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/scan/board_finder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>

#include "scratch.hpp"
#include "wjs/profiling.hpp"
#include "wjs/scan/segment.hpp"
#include "wjs/scan/work.hpp"

namespace wjs::scan {

namespace {

// The longest side a board is drawn at: a fourth of the 65536 a frame may have,
// which is a hundred times what a board needs.
constexpr int kMaxWarpSide = 1 << 14;

// A comb that fits a board's outline this well (the share of the board's range
// between a die's middle and a seam) is a board.
constexpr double kGoodFit = 0.35;

// A blob that is shaped like a die.
struct Die {
    Point centre;
    double side;
    const Blob* blob;
};

// Dice: bright blobs whose outline is four-sided and convex and about as long as
// it is wide. A letter on a die makes a hole in the blob; the outline ignores it.
std::vector<Die> die_shaped(const std::vector<Blob>& blobs) {
    std::vector<Die> dice;
    for (const Blob& blob : blobs) {
        if (blob.hull.size() < 4 || blob.hull_area <= 0.0) {
            continue;
        }
        // A die with its letter in it is mostly bright.
        if (static_cast<double>(blob.area) / blob.hull_area < 0.5) {
            continue;
        }
        const std::optional<Quad> quad = enclosing_quad(blob.hull);
        if (!quad) {
            continue;
        }
        // Rounded corners and a ragged edge cost a little; a blob that is some other
        // shape (a cross, a long thin bar, a half-moon) costs more.
        const double quad_area = std::abs(signed_area(*quad));
        if (quad_area <= 0.0 || blob.hull_area / quad_area < 0.88) {
            continue;
        }
        double shortest = 1e300;
        double longest = 0.0;
        for (std::size_t k = 0; k < 4; ++k) {
            const double length = distance((*quad)[k], (*quad)[(k + 1) % 4]);
            shortest = std::min(shortest, length);
            longest = std::max(longest, length);
        }
        if (shortest <= 0.0 || longest > 2.2 * shortest) {
            continue;
        }
        dice.push_back({blob.centre, std::sqrt(blob.hull_area), &blob});
    }
    return dice;
}

// The dice that are each a neighbour of another at a size and spacing that agree,
// as the largest connected set of them.
std::vector<const Die*> largest_lattice(const std::vector<Die>& dice) {
    const std::size_t n = dice.size();
    std::vector<std::size_t> parent(n);
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find = [&](std::size_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const double ratio = dice[i].side / dice[j].side;
            if (ratio < 0.75 || ratio > 1.33) {
                continue;
            }
            const double mean_side = (dice[i].side + dice[j].side) / 2.0;
            const double apart = distance(dice[i].centre, dice[j].centre);
            // Next door, or diagonally next door; not on top of each other.
            if (apart < 0.9 * mean_side || apart > 1.9 * mean_side) {
                continue;
            }
            parent[find(i)] = find(j);
        }
    }

    std::vector<std::size_t> size(n, 0);
    std::vector<double> area(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        ++size[find(i)];
        area[find(i)] += dice[i].side * dice[i].side;
    }
    std::size_t best = n;
    for (std::size_t i = 0; i < n; ++i) {
        if (size[i] == 0) {
            continue;
        }
        if (best == n || size[i] > size[best] || (size[i] == size[best] && area[i] > area[best])) {
            best = i;
        }
    }

    std::vector<const Die*> members;
    if (best == n) {
        return members;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (find(i) == best) {
            members.push_back(&dice[i]);
        }
    }
    return members;
}

double axis_score(const std::vector<double>& profile, std::size_t cells) {
    const auto length = static_cast<double>(profile.size());
    const double cell = length / static_cast<double>(cells);
    const auto at = [&](double position) {
        const auto index = static_cast<std::ptrdiff_t>(position);
        return profile[static_cast<std::size_t>(
            std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(profile.size()) - 1))];
    };

    // The middle of each cell, where a die is, and the seam between neighbours,
    // where the gap is. (The outer sides of the board are die edges and not seams.)
    double interior = 0.0;
    int interior_count = 0;
    for (std::size_t k = 0; k < cells; ++k) {
        for (int t = 0; t <= 8; ++t) {
            interior += at((static_cast<double>(k) + 0.3 + 0.4 * t / 8.0) * cell);
            ++interior_count;
        }
    }
    double seam = 0.0;
    int seam_count = 0;
    for (std::size_t k = 1; k < cells; ++k) {
        for (int t = -2; t <= 2; ++t) {
            seam += at(static_cast<double>(k) * cell + t * 0.025 * cell);
            ++seam_count;
        }
    }
    return interior / interior_count - seam / seam_count;
}

// A profile smoothed over three samples and stretched to run from 0 to 1.
std::vector<double> normalised(std::vector<double> profile) {
    if (profile.empty()) {
        return profile;  // no range to find: minmax_element's end is not a value
    }
    std::vector<double> smooth(profile.size());
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const double before = profile[i == 0 ? 0 : i - 1];
        const double after = profile[std::min(i + 1, profile.size() - 1)];
        smooth[i] = (before + profile[i] + after) / 3.0;
    }
    const auto [low, high] = std::ranges::minmax_element(smooth);
    const double range = *high - *low;
    if (range < 1e-9) {
        std::ranges::fill(smooth, 0.0);
        return smooth;
    }
    for (double& x : smooth) {
        x = (x - *low) / range;
    }
    return smooth;
}

// The middles of the dice along one axis, from the gaps between them. `profile` is
// the normalised mean grey along the axis, with `cells` dice on it.
std::vector<double> axis_centres(const std::vector<double>& profile, std::size_t cells) {
    const auto length = static_cast<double>(profile.size());
    const double cell = length / static_cast<double>(cells);
    const auto last = static_cast<std::ptrdiff_t>(profile.size()) - 1;
    const auto at = [&](std::ptrdiff_t i) {
        return profile[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(i, 0, last))];
    };

    // The middle of the gap that should be near k cells along: the deepest point of
    // the dip within an eighth of a cell of it, then the centre of the part of the dip
    // below half its depth (so a flat-bottomed gap is centred and a die lit more on
    // one side than the other does not pull it), found again from there.
    std::vector<double> gap(cells + 1, 0.0);
    for (std::size_t k = 1; k < cells; ++k) {
        const double expected = static_cast<double>(k) * cell;
        gap[k] = expected;
        const auto low = static_cast<std::ptrdiff_t>(expected - 0.12 * cell);
        const auto high = static_cast<std::ptrdiff_t>(expected + 0.12 * cell);
        std::ptrdiff_t deepest = low;
        for (std::ptrdiff_t i = low; i <= high; ++i) {
            if (at(i) < at(deepest)) {
                deepest = i;
            }
        }
        double centre = static_cast<double>(deepest) + 0.5;
        const auto reach = std::max<std::ptrdiff_t>(3, static_cast<std::ptrdiff_t>(0.2 * cell));
        for (int pass = 0; pass < 3; ++pass) {
            const auto from = static_cast<std::ptrdiff_t>(centre) - reach;
            const auto to = static_cast<std::ptrdiff_t>(centre) + reach;
            double base = 0.0;
            double floor = 1e300;
            for (std::ptrdiff_t i = from; i <= to; ++i) {
                base = std::max(base, at(i));
                floor = std::min(floor, at(i));
            }
            const double half_depth = (base - floor) / 2.0;
            double weight = 0.0;
            double moment = 0.0;
            for (std::ptrdiff_t i = from; i <= to; ++i) {
                const double below = (base - at(i)) - half_depth;
                if (below > 0.0) {
                    weight += below;
                    moment += below * (static_cast<double>(i) + 0.5);
                }
            }
            // A dip with no depth to it is no gap: keep the even split, which
            // gap[k] already holds.
            if (weight <= 0.05) {
                break;
            }
            centre = moment / weight;
            gap[k] = centre;
        }
    }

    // The pitch from the gaps, which also places the two outer dice.
    std::vector<double> pitches;
    for (std::size_t k = 1; k + 1 < cells; ++k) {
        pitches.push_back(gap[k + 1] - gap[k]);
    }
    double pitch = cell;
    if (!pitches.empty()) {
        std::ranges::nth_element(pitches,
                                 pitches.begin() + static_cast<std::ptrdiff_t>(pitches.size() / 2));
        pitch = pitches[pitches.size() / 2];
    }

    std::vector<double> centres(cells);
    for (std::size_t j = 0; j < cells; ++j) {
        if (j == 0) {
            centres[j] = gap[1] - pitch / 2.0;
        } else if (j + 1 == cells) {
            centres[j] = gap[cells - 1] + pitch / 2.0;
        } else {
            centres[j] = (gap[j] + gap[j + 1]) / 2.0;
        }
    }
    return centres;
}

// One look for the board: the bright pixels by a local threshold of the given
// window (as a share of the short side) and margin, the die-shaped blobs among
// them, the lattice they make, and its outline held to the comb. `pass_fit` is how
// well the outline combs, for the caller to compare looks by. `sat` is the blurred
// picture's summed-area table, made once for all the looks, and the mask, the blob
// labels and the trial warps are the scratch's, reused across looks and reads.
std::optional<BoardFind> find_pass(const GrayImage& image, const GrayImage& blurred,
                                   std::span<const std::uint64_t> sat, std::size_t min_dice,
                                   double window_fraction, int margin, double& pass_fit,
                                   detail::Scratch& scratch) {
    pass_fit = -1e300;
    const int short_side = std::min(image.width, image.height);
    // A window a bit over a die and its neighbours across, so the gaps between
    // dice are in it and the dice stand out from its mean.
    const int window = std::max(15, static_cast<int>(short_side * window_fraction)) | 1;
    const std::size_t pixels =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    const std::span<std::uint8_t> mask = detail::acquire(scratch.mask, pixels);
    {
        [[maybe_unused]] auto mask_zone = profiling::zone<"scan.bright_mask">();
        detail::bright_mask_into(mask, sat, blurred, window, margin);
    }

    const double side_min = static_cast<double>(short_side) / 24.0;
    const double side_max = static_cast<double>(short_side) / 3.0;
    const std::span<std::int32_t> labels = detail::acquire(scratch.labels, pixels);
    const std::vector<Blob> blobs = detail::find_blobs(
        mask, image.width, image.height, static_cast<std::int64_t>(0.5 * side_min * side_min),
        static_cast<std::int64_t>(side_max * side_max), labels);

    const std::vector<Die> dice = die_shaped(blobs);
    detail::scan_tally().dice_candidates += dice.size();
    const std::vector<const Die*> lattice = largest_lattice(dice);
    if (lattice.size() < min_dice) {
        return std::nullopt;
    }

    // The board's outline from a set of dice, or nothing if they have none.
    const auto outline_of = [](const std::vector<const Die*>& members) -> std::optional<Quad> {
        std::vector<Point> points;
        for (const Die* die : members) {
            points.insert(points.end(), die->blob->hull.begin(), die->blob->hull.end());
        }
        const std::vector<Point> hull = convex_hull(std::move(points));
        const std::optional<Quad> quad = enclosing_quad(hull);
        if (!quad) {
            return std::nullopt;
        }
        return rotate_to_upright(*quad);
    };

    // A bright rectangle beside the board (a sheet of paper, a phone) can be about
    // a die's size and sit about a die's pitch from the edge of the lattice, and
    // then it is grouped with the dice and the outline is far too big. So each
    // outline is held to what a board is: drawn square, it is a comb of dice and
    // gaps. If it is not, the die that adds most to the outline is dropped and the
    // outline tried again, a few times.
    std::vector<const Die*> members = lattice;
    std::optional<Quad> best_outline;
    std::size_t best_dice = 0;
    double best_fit = -1e300;
    constexpr int kAttempts = 5;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const std::optional<Quad> outline = outline_of(members);
        if (!outline) {
            break;
        }
        // The trial warp reuses the scratch's 240-pixel square: the comb score is
        // read before the next attempt draws over it, so one buffer serves them all.
        detail::warp_into(scratch.look, image, *outline, 240);
        const std::vector<double> fits = comb_scores(scratch.look, 3, 8);
        const double fit = fits.empty() ? -1.0 : *std::ranges::max_element(fits);
        if (fit > best_fit) {
            best_fit = fit;
            best_outline = outline;
            best_dice = members.size();
        }
        if (fit >= kGoodFit || members.size() <= min_dice) {
            break;
        }

        // Drop the die whose removal shrinks the outline most.
        std::size_t drop = 0;
        double smallest = 1e300;
        for (std::size_t m = 0; m < members.size(); ++m) {
            std::vector<const Die*> without = members;
            without.erase(without.begin() + static_cast<std::ptrdiff_t>(m));
            std::vector<Point> points;
            for (const Die* die : without) {
                points.insert(points.end(), die->blob->hull.begin(), die->blob->hull.end());
            }
            const double area = std::abs(signed_area(convex_hull(std::move(points))));
            if (area < smallest) {
                smallest = area;
                drop = m;
            }
        }
        members.erase(members.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    if (!best_outline) {
        return std::nullopt;
    }
    pass_fit = best_fit;

    std::vector<double> sides;
    sides.reserve(lattice.size());
    for (const Die* die : lattice) {
        sides.push_back(die->side);
    }
    if (sides.empty()) {
        return std::nullopt;
    }
    std::ranges::nth_element(sides, sides.begin() + static_cast<std::ptrdiff_t>(sides.size() / 2));
    BoardFind find;
    find.corners = *best_outline;
    find.dice = best_dice;
    find.die_side = sides[sides.size() / 2];
    double perimeter = 0.0;
    for (std::size_t k = 0; k < 4; ++k) {
        perimeter += distance((*best_outline)[k], (*best_outline)[(k + 1) % 4]);
    }
    // The mean edge against a die's side (the square root of its area, which under
    // a tilt shortens as the edges do), in dice, less the gaps.
    constexpr double kGap = 0.115;
    find.size_estimate = (perimeter / 4.0 / find.die_side + kGap) / (1.0 + kGap);
    return find;
}

}  // namespace

std::optional<BoardFind> detail::find_board_using(const GrayImage& image, std::size_t min_dice,
                                                  Scratch& scratch) {
    [[maybe_unused]] auto zone = profiling::zone<"scan.find_board">();
    if (image.width < 16 || image.height < 16) {
        return std::nullopt;
    }
    const std::size_t pixels =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    const std::span<std::uint32_t> across = detail::acquire(scratch.blur_across, pixels);
    {
        [[maybe_unused]] auto blur_zone = profiling::zone<"scan.box_blur">();
        detail::box_blur_into(scratch.blurred, across, image, 1);
    }
    // The summed-area table of the blurred picture, made once here: each look's
    // threshold built its own copy of the same values before, five passes and five
    // buffers where one of each will do.
    const std::size_t stride = static_cast<std::size_t>(image.width) + 1;
    const std::span<std::uint64_t> sat =
        detail::acquire(scratch.sat, stride * (static_cast<std::size_t>(image.height) + 1));
    detail::summed_area(sat, scratch.blurred);

    // Light that is uneven across the board (a highlight on some dice and a shadow
    // on others) can hide dice from a look that is right for even light, and then
    // a good-looking board of nine dice is found in the middle of one of sixteen. So
    // each look is made, and of those whose outline combs well the one that found
    // the most dice is the board; if none combs well, the one that combed best.
    struct Look {
        double window_fraction;
        int margin;
    };
    constexpr std::array<Look, 5> kLooks{{{0.30, 8}, {0.20, 4}, {0.42, 5}, {0.30, 2}, {0.30, -4}}};
    std::optional<BoardFind> best;
    double best_fit = -1e300;
    bool best_is_good = false;
    for (const Look& look : kLooks) {
        double fit = 0.0;
        const std::optional<BoardFind> found = find_pass(
            image, scratch.blurred, sat, min_dice, look.window_fraction, look.margin, fit, scratch);
        if (!found) {
            continue;
        }
        const bool good = fit >= kGoodFit;
        bool better = true;
        if (best) {
            if (best_is_good != good) {
                better = good;
            } else if (good) {
                better = found->dice > best->dice;
            } else {
                better = fit > best_fit;
            }
        }
        if (better) {
            best = found;
            best_fit = fit;
            best_is_good = good;
        }
    }
    return best;
}

std::optional<BoardFind> find_board(const GrayImage& image, std::size_t min_dice) {
    detail::Scratch scratch;
    return detail::find_board_using(image, min_dice, scratch);
}

void detail::warp_into(GrayImage& out, const GrayImage& source, const Quad& corners, int side) {
    if (side <= 0 || side > kMaxWarpSide) {
        detail::acquire_image(out, 0, 0);
        return;
    }
    detail::acquire_image(out, side, side);
    const auto s = static_cast<double>(side);
    const Quad square{{{0.0, 0.0}, {s, 0.0}, {s, s}, {0.0, s}}};
    const std::optional<Homography> map = homography(square, corners);
    if (!map) {
        // A freshly made image would be all zeros; the reused one is cleared to the
        // same so the caller sees the one answer either way.
        std::ranges::fill(out.pixels.begin(),
                          out.pixels.begin() +
                              static_cast<std::ptrdiff_t>(side) * static_cast<std::ptrdiff_t>(side),
                          std::uint8_t{0});
        return;
    }
    detail::scan_tally().warp_pixels +=
        static_cast<std::uint64_t>(side) * static_cast<std::uint64_t>(side);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const Point p = map->apply({x + 0.5, y + 0.5});
            out.at(x, y) =
                static_cast<std::uint8_t>(std::clamp(sample(source, p.x, p.y) + 0.5, 0.0, 255.0));
        }
    }
}

GrayImage warp_to_square(const GrayImage& source, const Quad& corners, int side) {
    GrayImage out;
    detail::warp_into(out, source, corners, side);
    return out;
}

BoardProfiles profiles_of(const GrayImage& warped) {
    // The mean grey of each column and each row of the warped board, smoothed
    // and stretched to run from 0 to 1. Two full passes over the picture, so the
    // reader computes it once and shares it between the cell count and the cell
    // centres (the overloads of count_cells and locate_cells below take one).
    std::vector<double> columns(static_cast<std::size_t>(warped.width), 0.0);
    std::vector<double> rows(static_cast<std::size_t>(warped.height), 0.0);
    for (int y = 0; y < warped.height; ++y) {
        for (int x = 0; x < warped.width; ++x) {
            const double v = warped.at(x, y);
            columns[static_cast<std::size_t>(x)] += v / warped.height;
            rows[static_cast<std::size_t>(y)] += v / warped.width;
        }
    }
    return {normalised(std::move(columns)), normalised(std::move(rows))};
}

namespace {

// The comb scores from profiles that were already computed.
std::vector<double> comb_scores_of(const BoardProfiles& profiles, std::size_t min_size,
                                   std::size_t max_size) {
    std::vector<double> scores;
    for (std::size_t n = min_size; n <= max_size; ++n) {
        scores.push_back(0.5 * (axis_score(profiles.columns, n) + axis_score(profiles.rows, n)));
    }
    return scores;
}

// The count the scores chose, which count_cells uses in both its forms.
std::optional<CellCount> pick_count(const std::vector<double>& scores, std::size_t min_size) {
    double best = -1e300;
    double runner_up = -1e300;
    std::size_t best_size = 0;
    for (std::size_t k = 0; k < scores.size(); ++k) {
        if (scores[k] > best) {
            runner_up = best;
            best = scores[k];
            best_size = min_size + k;
        } else if (scores[k] > runner_up) {
            runner_up = scores[k];
        }
    }
    if (best < 0.2) {
        return std::nullopt;
    }
    CellCount count;
    count.size = best_size;
    count.confidence = std::clamp((best - runner_up) / 0.25, 0.0, 1.0);
    return count;
}

}  // namespace

std::vector<double> comb_scores(const GrayImage& warped, std::size_t min_size,
                                std::size_t max_size) {
    // A count of fewer than two has no seam to score (0 / 0), and the scores are
    // one for each count from min_size, so there is nothing to give instead.
    if (warped.empty() || min_size < 2 || max_size < min_size) {
        return {};
    }
    return comb_scores_of(profiles_of(warped), min_size, max_size);
}

std::optional<CellCount> count_cells(const GrayImage& warped, std::size_t min_size,
                                     std::size_t max_size, std::size_t dice, double estimate) {
    if (warped.empty()) {
        return std::nullopt;
    }
    return count_cells(warped, profiles_of(warped), min_size, max_size, dice, estimate);
}

std::optional<CellCount> count_cells(const GrayImage& warped, const BoardProfiles& profiles,
                                     std::size_t min_size, std::size_t max_size, std::size_t dice,
                                     double estimate) {
    if (warped.empty() || min_size < 2 || max_size < min_size) {
        return std::nullopt;
    }
    if (dice > 1) {
        // Fewer cells than dice (less a stray) is out, and so is a count with more
        // than three times as many cells as dice.
        while (min_size * min_size + 1 < dice) {
            ++min_size;
        }
        while (max_size > min_size && max_size * max_size > 3 * dice) {
            --max_size;
        }
        if (max_size < min_size) {
            return std::nullopt;
        }
    }
    if (estimate > 0.0) {
        while (min_size < max_size && static_cast<double>(min_size) < estimate - 0.8) {
            ++min_size;
        }
        while (max_size > min_size && static_cast<double>(max_size) > estimate + 0.8) {
            --max_size;
        }
    }
    return pick_count(comb_scores_of(profiles, min_size, max_size), min_size);
}

CellCentres locate_cells(const GrayImage& warped, std::size_t size) {
    CellCentres centres;
    if (warped.empty() || size == 0) {
        return centres;
    }
    return locate_cells(warped, profiles_of(warped), size);
}

CellCentres locate_cells(const GrayImage& warped, const BoardProfiles& profiles, std::size_t size) {
    CellCentres centres;
    if (warped.empty() || size == 0) {
        return centres;
    }
    if (size < 3) {
        // Too few gaps to measure a pitch: the even split.
        for (std::size_t j = 0; j < size; ++j) {
            const double middle = (static_cast<double>(j) + 0.5) / static_cast<double>(size);
            centres.x.push_back(middle * warped.width);
            centres.y.push_back(middle * warped.height);
        }
        return centres;
    }
    centres.x = axis_centres(profiles.columns, size);
    centres.y = axis_centres(profiles.rows, size);
    return centres;
}

}  // namespace wjs::scan
