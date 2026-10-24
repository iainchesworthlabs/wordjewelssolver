// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The two small types the profiling calls (wjs/profiling.hpp) are written in
// terms of. They are the same for both variants of that header, so they live
// here and the variants hold only what differs: whether Tracy is behind them.
//
// Tracy keeps the POINTERS of the names it is given (a zone's name, a plot's,
// a thread's) and reads them on another thread, later. A name that is a
// temporary string is therefore a bug that shows up as garbage in a trace, in
// a build nobody was looking at. These types make that a compile error
// instead, so that no rule needs remembering.

#include <array>
#include <cstddef>

namespace wjs::profiling {

// A name written in the template argument list: zone<"solver.solve">(). (Not
// called ZoneName: Tracy's header defines a macro of that name, and the enabled
// variant includes it. Nothing in this namespace may be called ZoneName,
// ZoneText, ZoneValue, ZoneColor, FrameMark, FrameImage, LockMark or anything
// that begins Tracy.)
// A class type with a constexpr constructor from a string literal is a valid
// non-type template argument (C++20), and the template parameter object it
// becomes has static storage, so the pointer to its text lives for the whole
// program.
template <std::size_t N>
struct StaticName {
    std::array<char, N> text{};

    constexpr StaticName(const char (&literal)[N]) noexcept {  // NOLINT: implicit on purpose
        for (std::size_t i = 0; i < N; ++i) {
            text[i] = literal[i];
        }
    }
};

// A string that is known at compile time to outlive the call: a string literal,
// or a constexpr static array. The constructor is consteval and keeps the
// pointer, so an array that lives on the stack (whose address is not a
// constant) does not compile.
class Literal {
public:
    template <std::size_t N>
    consteval Literal(const char (&text)[N]) noexcept  // NOLINT: implicit on purpose
        : text_(text) {}

    [[nodiscard]] constexpr const char* c_str() const noexcept { return text_; }

private:
    const char* text_;
};

}  // namespace wjs::profiling
