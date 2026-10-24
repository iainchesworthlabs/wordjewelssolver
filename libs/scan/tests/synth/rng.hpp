// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The corpus's random numbers. std::mt19937 is specified exactly, and the draws
// are taken modulo, never through a std::*_distribution (whose algorithm is each
// standard library's own), so a seed is the same frame on MSVC, libstdc++ and
// libc++ (docs/testing.md, "The corpus").

#include <cmath>
#include <cstdint>
#include <random>

namespace wjs::scan::synth {

class Rng {
public:
    explicit Rng(std::uint32_t seed) : engine_(seed) {}

    [[nodiscard]] std::uint32_t next() { return static_cast<std::uint32_t>(engine_()); }

    // An integer in [0, n).
    [[nodiscard]] std::uint32_t below(std::uint32_t n) { return next() % n; }

    // A real in [0, 1), in steps of a millionth.
    [[nodiscard]] double unit() { return static_cast<double>(next() % 1'000'000U) / 1'000'000.0; }

    // A real in [low, high).
    [[nodiscard]] double range(double low, double high) { return low + (high - low) * unit(); }

    [[nodiscard]] bool chance(double p) { return unit() < p; }

    // A real with mean 0 and deviation 1 (Box-Muller).
    [[nodiscard]] double normal() {
        const double u = 1.0 - unit();  // (0, 1], so the log is finite
        const double v = unit();
        return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * 3.14159265358979323846 * v);
    }

private:
    std::mt19937 engine_;
};

}  // namespace wjs::scan::synth
