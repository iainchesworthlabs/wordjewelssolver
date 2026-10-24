// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

#include "rng.hpp"
#include "wjs/scan/stroke_font.hpp"

namespace wjs::scan::synth {

namespace {

constexpr double kDegrees = std::numbers::pi / 180.0;
constexpr double kPlaneDie = 1.0;

struct Mat3 {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

Mat3 operator*(const Mat3& a, const Mat3& b) {
    Mat3 out;
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += a.m[r * 3 + k] * b.m[k * 3 + c];
            }
            out.m[r * 3 + c] = sum;
        }
    }
    return out;
}

Mat3 rot_x(double a) {
    return {{1, 0, 0, 0, std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a)}};
}
Mat3 rot_y(double a) {
    return {{std::cos(a), 0, std::sin(a), 0, 1, 0, -std::sin(a), 0, std::cos(a)}};
}
Mat3 rot_z(double a) {
    return {{std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1}};
}

// What a condition varies, and by how much.
struct Look {
    double tilt_max_deg = 10.0;
    bool spin_any = false;
    double die_jitter_deg = 4.0;
    bool die_turn_any = false;
    double blur_low = 0.5;
    double blur_high = 0.8;
    double noise_low = 0.5;
    double noise_high = 2.5;
    double light = 0.10;
    int glare_patches_max = 0;
    bool streak = false;
};

Look look_for(Condition condition) {
    Look look;
    switch (condition) {
        case Condition::upright:
            break;
        case Condition::angled:
            look = {35.0, true, 8.0, false, 0.7, 1.3, 2.0, 5.0, 0.22, 0, false};
            break;
        case Condition::loose:
            look = {35.0, true, 8.0, true, 0.7, 1.3, 2.0, 5.0, 0.22, 0, false};
            break;
        case Condition::glare:
            look = {35.0, true, 8.0, false, 0.7, 1.3, 2.0, 5.0, 0.22, 2, true};
            break;
    }
    return look;
}

struct Rect {
    Point centre;
    double half_w;
    double half_h;
    double cos_a;
    double sin_a;
    double value;
};

// The room behind the board: a base grey with a gradient, a wood-grain stripe, and
// a table edge (a line, one side of it darker).
struct Room {
    double base;
    double gradient_x;
    double gradient_y;
    double stripe_amplitude;
    double stripe_period;
    double stripe_cos;
    double stripe_sin;
    double edge_cos;
    double edge_sin;
    double edge_offset;
};

struct Glare {
    Point centre;
    double ax;
    double ay;
    double angle;
    double amplitude;
};

struct Streak {
    Point from;
    double angle;
    double width;
    double amplitude;
    double length;
};

// One die: where it is, what it shows and how that is drawn.
struct Die {
    double brightness = 215.0;
    std::vector<Stroke> strokes{};  // die-local, in die units, y down
    double half_width = 0.0;
    bool has_glyph = false;
};

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The signed distance from a point to a square of half-side `half` with corners
// rounded to `radius`: negative inside.
double rounded_square_distance(double x, double y, double half, double radius) {
    const double qx = std::abs(x) - (half - radius);
    const double qy = std::abs(y) - (half - radius);
    const double outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
    return outside + std::min(std::max(qx, qy), 0.0) - radius;
}

double room_value(double u, double v, const Room& room, const std::vector<Rect>& rects) {
    double value = room.base + room.gradient_x * (u / kFrameWidth - 0.5) * 80.0 +
                   room.gradient_y * (v / kFrameHeight - 0.5) * 80.0;
    value += room.stripe_amplitude *
             std::sin(2.0 * std::numbers::pi * (u * room.stripe_cos + v * room.stripe_sin) /
                      room.stripe_period);
    const double side = (u - kFrameWidth / 2.0) * room.edge_cos +
                        (v - kFrameHeight / 2.0) * room.edge_sin - room.edge_offset;
    value -= 38.0 * smoothstep(-2.0, 2.0, side);
    for (const Rect& rect : rects) {
        const double dx = u - rect.centre.x;
        const double dy = v - rect.centre.y;
        const double x = dx * rect.cos_a + dy * rect.sin_a;
        const double y = -dx * rect.sin_a + dy * rect.cos_a;
        const double inside = std::min(rect.half_w - std::abs(x), rect.half_h - std::abs(y));
        const double t = smoothstep(-0.7, 0.7, inside);
        value = value * (1.0 - t) + rect.value * t;
    }
    return value;
}

std::vector<float> gaussian_blur(const std::vector<float>& in, int width, int height,
                                 double sigma) {
    if (sigma < 0.05) {
        return in;
    }
    const int radius = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<float> kernel(static_cast<std::size_t>(2 * radius + 1));
    float total = 0.0F;
    for (int k = -radius; k <= radius; ++k) {
        const auto w = static_cast<float>(std::exp(-0.5 * k * k / (sigma * sigma)));
        kernel[static_cast<std::size_t>(k + radius)] = w;
        total += w;
    }
    for (float& w : kernel) {
        w /= total;
    }
    const auto at = [&](const std::vector<float>& image, int x, int y) {
        return image[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) *
                         static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(std::clamp(x, 0, width - 1))];
    };
    std::vector<float> across(in.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0F;
            for (int k = -radius; k <= radius; ++k) {
                sum += kernel[static_cast<std::size_t>(k + radius)] * at(in, x + k, y);
            }
            across[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] = sum;
        }
    }
    std::vector<float> out(in.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0F;
            for (int k = -radius; k <= radius; ++k) {
                sum += kernel[static_cast<std::size_t>(k + radius)] * at(across, x, y + k);
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)] = sum;
        }
    }
    return out;
}

Point project(const Homography& h, double x, double y) {
    return h.apply({x, y});
}

// Draws a frame. `size` is the board's side; `with_board` false is a room only.
Rendered draw(Rng& rng, const Look& look, std::size_t size, bool with_board, bool with_letters,
              int rectangles) {
    Rendered out;
    out.size = size;
    const auto& font = stroke_font();

    // --- the board -------------------------------------------------------------
    const double gap = rng.range(0.09, 0.14);
    out.gap = gap;
    const double length =
        static_cast<double>(size) * kPlaneDie + static_cast<double>(size - 1) * gap;
    const double half = length / 2.0;
    const double margin = rng.range(0.28, 0.40);
    const double tray_half = half + margin;
    const double pitch = kPlaneDie + gap;

    // --- the view --------------------------------------------------------------
    const double spin = look.spin_any ? rng.range(0.0, 360.0) : rng.range(-10.0, 10.0);
    const double tilt_direction = rng.range(0.0, 360.0) * kDegrees;
    const double tilt = rng.range(0.0, look.tilt_max_deg) * kDegrees;
    const Mat3 rotation = rot_x(tilt * std::cos(tilt_direction)) *
                          rot_y(tilt * std::sin(tilt_direction)) * rot_z(spin * kDegrees);
    constexpr double kFocal = 900.0;

    const auto make_homography = [&](double distance, double dx, double dy) {
        const double cx = kFrameWidth / 2.0 + dx;
        const double cy = kFrameHeight / 2.0 + dy;
        const auto& r = rotation.m;
        // K * [r1 r2 t], t = (0, 0, distance).
        Homography h;
        h.m = {kFocal * r[0] + cx * r[6],
               kFocal * r[1] + cx * r[7],
               cx * distance,
               kFocal * r[3] + cy * r[6],
               kFocal * r[4] + cy * r[7],
               cy * distance,
               r[6],
               r[7],
               distance};
        return h;
    };

    double distance = kFocal * (2.0 * tray_half) / (rng.range(0.55, 0.78) * kFrameHeight);
    double shift_x = 0.0;
    double shift_y = 0.0;
    Homography to_image = make_homography(distance, 0.0, 0.0);
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    if (with_board) {
        const auto bounds = [&](const Homography& h) {
            min_x = min_y = 1e300;
            max_x = max_y = -1e300;
            for (const auto& corner :
                 std::array<std::array<double, 2>, 4>{{{-tray_half, -tray_half},
                                                       {tray_half, -tray_half},
                                                       {tray_half, tray_half},
                                                       {-tray_half, tray_half}}}) {
                const Point p = project(h, corner[0], corner[1]);
                min_x = std::min(min_x, p.x);
                max_x = std::max(max_x, p.x);
                min_y = std::min(min_y, p.y);
                max_y = std::max(max_y, p.y);
            }
        };
        bounds(to_image);
        // Back off until the whole tray fits with a margin, then put it somewhere.
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (max_x - min_x <= kFrameWidth - 60.0 && max_y - min_y <= kFrameHeight - 60.0) {
                break;
            }
            distance *= 1.08;
            to_image = make_homography(distance, 0.0, 0.0);
            bounds(to_image);
        }
        const double slack_x = (kFrameWidth - 60.0) - (max_x - min_x);
        const double slack_y = (kFrameHeight - 60.0) - (max_y - min_y);
        shift_x = 30.0 - min_x + rng.range(0.0, std::max(slack_x, 0.0));
        shift_y = 30.0 - min_y + rng.range(0.0, std::max(slack_y, 0.0));
        to_image = make_homography(distance, shift_x, shift_y);
        bounds(to_image);
    }
    const std::optional<Homography> to_plane = inverse(to_image);
    if (with_board) {
        out.corners = {project(to_image, -half, -half), project(to_image, half, -half),
                       project(to_image, half, half), project(to_image, -half, half)};
    }

    // --- the dice --------------------------------------------------------------
    const double glyph_height = rng.range(0.50, 0.60);
    const double stroke_base = rng.range(0.55, 1.05);
    const double shear = rng.range(-0.12, 0.12);
    const double width_scale = rng.range(0.90, 1.10);
    const double point_jitter = rng.range(0.05, 0.20);
    const double scale = glyph_height / kGlyphHeight;
    out.glyph_height = glyph_height;
    out.stroke_weight = stroke_base;

    std::vector<Die> dice(size * size);
    if (with_board) {
        for (std::size_t i = 0; i < dice.size(); ++i) {
            Die& die = dice[i];
            die.brightness = 215.0 + rng.range(-14.0, 14.0);
            if (!with_letters) {
                out.letters.emplace_back();
                out.turns.push_back(0.0);
                continue;
            }
            const std::size_t g = rng.below(static_cast<std::uint32_t>(kGlyphCount));
            out.letters.emplace_back(font[g].letters);

            double turn = 0.0;
            if (look.die_turn_any) {
                turn = rng.range(0.0, 360.0);
            } else {
                turn = 90.0 * rng.below(4) + rng.range(-look.die_jitter_deg, look.die_jitter_deg);
            }
            out.turns.push_back(std::fmod(turn + 360.0, 360.0));
            const double radians = turn * kDegrees;
            const double offset_x = rng.range(-0.03, 0.03);
            const double offset_y = rng.range(-0.03, 0.03);
            const double die_width = stroke_base * rng.range(0.92, 1.08);
            die.half_width = die_width * scale / 2.0;
            die.has_glyph = true;
            // A smooth distortion of this die's glyph, a few waves across the box
            // (a typeface's letters differ from another's smoothly, not by a
            // lumpiness of the outline), the same for every point of it.
            const double wave_a = rng.range(0.0, 6.28);
            const double wave_b = rng.range(0.0, 6.28);
            const double wave_c = rng.range(0.0, 6.28);
            const double wave_d = rng.range(0.0, 6.28);
            for (const Stroke& stroke : font[g].strokes) {
                Stroke placed;
                for (const Point& p : stroke) {
                    double bx = p.x + point_jitter * (std::sin(0.55 * p.y + wave_a) +
                                                      0.6 * std::sin(0.7 * p.x + wave_b));
                    const double by = p.y + point_jitter * (std::sin(0.6 * p.x + wave_c) +
                                                            0.6 * std::sin(0.5 * p.y + wave_d));
                    bx += shear * (by - 3.0);
                    const double x = (bx - 2.0) * scale * width_scale;
                    const double y = (by - 3.0) * scale;
                    placed.push_back({x * std::cos(radians) - y * std::sin(radians) + offset_x,
                                      x * std::sin(radians) + y * std::cos(radians) + offset_y});
                }
                die.strokes.push_back(std::move(placed));
            }
        }
    }

    // --- the room --------------------------------------------------------------
    Room room{};
    room.base = rng.range(80.0, 170.0);
    room.gradient_x = rng.range(-1.0, 1.0);
    room.gradient_y = rng.range(-1.0, 1.0);
    room.stripe_amplitude = rng.range(0.0, 10.0);
    room.stripe_period = rng.range(20.0, 60.0);
    const double stripe_angle = rng.range(0.0, 3.14);
    room.stripe_cos = std::cos(stripe_angle);
    room.stripe_sin = std::sin(stripe_angle);
    const double edge_angle = rng.range(0.0, 6.28);
    room.edge_cos = std::cos(edge_angle);
    room.edge_sin = std::sin(edge_angle);
    room.edge_offset = rng.range(120.0, 600.0);
    std::vector<Rect> rects;
    for (int k = 0; k < rectangles; ++k) {
        for (int attempt = 0; attempt < 30; ++attempt) {
            Rect rect;
            rect.half_w = rng.range(0.06, 0.15) * kFrameHeight;
            rect.half_h = rng.range(0.06, 0.18) * kFrameHeight;
            rect.centre = {rng.range(0.0, kFrameWidth), rng.range(0.0, kFrameHeight)};
            const double rect_angle = rng.range(0.0, 3.14);
            rect.cos_a = std::cos(rect_angle);
            rect.sin_a = std::sin(rect_angle);
            rect.value = rng.chance(0.6) ? rng.range(205.0, 245.0) : rng.range(20.0, 60.0);
            const double reach = std::hypot(rect.half_w, rect.half_h) + 40.0;
            const bool clear = !with_board || rect.centre.x + reach < min_x ||
                               rect.centre.x - reach > max_x || rect.centre.y + reach < min_y ||
                               rect.centre.y - reach > max_y;
            if (clear) {
                rects.push_back(rect);
                break;
            }
        }
    }

    // --- light and glare -------------------------------------------------------
    const double light_x = rng.range(-look.light, look.light);
    const double light_y = rng.range(-look.light, look.light);
    const double vignette = rng.range(0.0, 0.2);
    const double exposure = rng.range(0.85, 1.08);
    std::vector<Glare> glares;
    Streak streak{};
    bool has_streak = false;
    if (with_board && look.glare_patches_max > 0) {
        const int patches =
            1 + static_cast<int>(rng.below(static_cast<std::uint32_t>(look.glare_patches_max)));
        const double board_pixels = wjs::scan::distance(out.corners[0], out.corners[1]);
        for (int k = 0; k < patches; ++k) {
            const double s = rng.range(0.1, 0.9);
            const double t = rng.range(0.1, 0.9);
            const Point top = out.corners[0] * (1.0 - s) + out.corners[1] * s;
            const Point bottom = out.corners[3] * (1.0 - s) + out.corners[2] * s;
            Glare glare;
            glare.centre = top * (1.0 - t) + bottom * t;
            glare.ax = rng.range(0.08, 0.22) * board_pixels;
            glare.ay = rng.range(0.08, 0.22) * board_pixels;
            glare.angle = rng.range(0.0, 3.14);
            glare.amplitude = rng.range(100.0, 190.0);
            glares.push_back(glare);
        }
        if (look.streak && rng.chance(0.5)) {
            const double s = rng.range(0.1, 0.9);
            streak.from = out.corners[0] * (1.0 - s) + out.corners[1] * s;
            streak.angle = rng.range(0.0, 3.14);
            streak.width = rng.range(2.5, 5.0);
            streak.amplitude = rng.range(50.0, 110.0);
            streak.length = board_pixels * rng.range(0.6, 1.4);
            has_streak = true;
        }
    }

    // --- the picture -----------------------------------------------------------
    const std::size_t pixels = static_cast<std::size_t>(kFrameWidth) * kFrameHeight;
    std::vector<float> field(pixels);
    const double tray_value = rng.range(30.0, 55.0);
    const Homography* inv = to_plane ? &*to_plane : nullptr;
    for (int v = 0; v < kFrameHeight; ++v) {
        for (int u = 0; u < kFrameWidth; ++u) {
            const double pu = u + 0.5;
            const double pv = v + 0.5;
            double value = room_value(pu, pv, room, rects);
            if (with_board && inv != nullptr) {
                const Point p = inv->apply({pu, pv});
                if (std::abs(p.x) <= tray_half && std::abs(p.y) <= tray_half) {
                    // The footprint of a pixel on the board, in dice.
                    const Point px = inv->apply({pu + 1.0, pv});
                    const Point py = inv->apply({pu, pv + 1.0});
                    const double footprint = 0.5 * (std::hypot(px.x - p.x, px.y - p.y) +
                                                    std::hypot(py.x - p.x, py.y - p.y));

                    value = tray_value;
                    const double cx = (p.x + half) / pitch;
                    const double cy = (p.y + half) / pitch;
                    const int column = static_cast<int>(std::floor(cx));
                    const int row = static_cast<int>(std::floor(cy));
                    if (column >= 0 && row >= 0 && column < static_cast<int>(size) &&
                        row < static_cast<int>(size)) {
                        const Die& die = dice[static_cast<std::size_t>(row) * size +
                                              static_cast<std::size_t>(column)];
                        const double qx = p.x + half - static_cast<double>(column) * pitch - 0.5;
                        const double qy = p.y + half - static_cast<double>(row) * pitch - 0.5;
                        const double sd = rounded_square_distance(qx, qy, 0.5, 0.12);
                        const double cover = std::clamp(0.5 - sd / footprint, 0.0, 1.0);
                        if (cover > 0.0) {
                            const double bevel = smoothstep(0.0, 0.08, -sd);
                            double face = die.brightness - 28.0 * (1.0 - bevel) + 6.0 * (qx - qy);
                            if (die.has_glyph) {
                                const double d =
                                    distance_to_strokes(die.strokes, {qx, qy}) - die.half_width;
                                const double ink = std::clamp(0.5 - d / footprint, 0.0, 1.0);
                                face = face * (1.0 - ink) + 40.0 * ink;
                            }
                            value = value * (1.0 - cover) + face * cover;
                        }
                    }
                }
            }
            value *= exposure * (1.0 + light_x * (2.0 * pu / kFrameWidth - 1.0) +
                                 light_y * (2.0 * pv / kFrameHeight - 1.0));
            const double rx = (pu / kFrameWidth - 0.5) * 2.0;
            const double ry = (pv / kFrameHeight - 0.5) * 2.0;
            value *= 1.0 - vignette * (rx * rx + ry * ry) / 2.0;

            for (const Glare& g : glares) {
                const double dx = pu - g.centre.x;
                const double dy = pv - g.centre.y;
                const double x = dx * std::cos(g.angle) + dy * std::sin(g.angle);
                const double y = -dx * std::sin(g.angle) + dy * std::cos(g.angle);
                value +=
                    g.amplitude * std::exp(-((x * x) / (g.ax * g.ax) + (y * y) / (g.ay * g.ay)));
            }
            if (has_streak) {
                const double dx = pu - streak.from.x;
                const double dy = pv - streak.from.y;
                const double along = dx * std::cos(streak.angle) + dy * std::sin(streak.angle);
                const double across = -dx * std::sin(streak.angle) + dy * std::cos(streak.angle);
                if (along > 0.0 && along < streak.length) {
                    const double fade = std::sin(std::numbers::pi * along / streak.length);
                    value += streak.amplitude * fade *
                             std::exp(-0.5 * across * across / (streak.width * streak.width));
                }
            }
            field[static_cast<std::size_t>(v) * kFrameWidth + static_cast<std::size_t>(u)] =
                static_cast<float>(std::min(value, 300.0));
        }
    }

    const double blur = rng.range(look.blur_low, look.blur_high);
    const double noise = rng.range(look.noise_low, look.noise_high);
    field = gaussian_blur(field, kFrameWidth, kFrameHeight, blur);
    out.image = GrayImage(kFrameWidth, kFrameHeight);
    for (std::size_t i = 0; i < pixels; ++i) {
        const double value = static_cast<double>(field[i]) + noise * rng.normal();
        out.image.pixels[i] = static_cast<std::uint8_t>(std::clamp(value + 0.5, 0.0, 255.0));
    }
    return out;
}

}  // namespace

const char* name(Condition condition) noexcept {
    switch (condition) {
        case Condition::upright:
            return "upright";
        case Condition::angled:
            return "angled";
        case Condition::loose:
            return "loose";
        case Condition::glare:
            return "glare";
    }
    return "?";
}

unsigned seed_of(const Spec& spec) noexcept {
    // The test split was drawn afresh (it was 2,000,000 while the reader was being
    // written, and those seeds were looked at; they are retired), so that the
    // numbers on it were not steered by.
    unsigned base = 1'000'000U;
    switch (spec.split) {
        case Split::tuning:
            break;
        case Split::test:
            base = 4'000'000U;
            break;
        case Split::heldout:
            base = 6'000'000U;
            break;
    }
    return base + static_cast<unsigned>(spec.condition) * 100'000U +
           static_cast<unsigned>(spec.size) * 1'000U + spec.index * 7U + 13U;
}

Rendered render(const Spec& spec) {
    Rng rng(seed_of(spec));
    Rendered out = draw(rng, look_for(spec.condition), spec.size, true, true,
                        1 + static_cast<int>(rng.below(2)));
    out.seed = seed_of(spec);
    return out;
}

Rendered render_blank(const Spec& spec) {
    Rng rng(seed_of(spec) + 3U);
    Rendered out = draw(rng, look_for(spec.condition), spec.size, true, false,
                        1 + static_cast<int>(rng.below(2)));
    out.seed = seed_of(spec);
    return out;
}

std::vector<Spec> corpus(Split split) {
    std::vector<Spec> specs;
    for (std::size_t c = 0; c < kConditionCount; ++c) {
        for (unsigned k = 0; k < 24; ++k) {
            Spec spec;
            spec.condition = static_cast<Condition>(c);
            spec.split = split;
            spec.size = k < 12 ? 4 : (k < 18 ? 5 : 6);
            spec.index = k;
            specs.push_back(spec);
        }
    }
    return specs;
}

GrayImage render_negative(NegativeKind kind, unsigned index) {
    Rng rng(3'000'000U + static_cast<unsigned>(kind) * 1'000U + index);
    switch (kind) {
        case NegativeKind::room:
            return draw(rng, look_for(Condition::angled), 4, false, false, 0).image;
        case NegativeKind::clutter:
            return draw(rng, look_for(Condition::angled), 4, false, false,
                        2 + static_cast<int>(rng.below(3)))
                .image;
        case NegativeKind::tiles:
            return draw(rng, look_for(Condition::angled), 6 + rng.below(3), true, false, 1).image;
    }
    return {};
}

std::vector<unsigned char> to_rgb(const GrayImage& image) {
    std::vector<unsigned char> out;
    out.reserve(image.pixels.size() * 3);
    for (const std::uint8_t v : image.pixels) {
        out.push_back(v);
        out.push_back(v);
        out.push_back(v);
    }
    return out;
}

}  // namespace wjs::scan::synth
