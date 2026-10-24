// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The profiling calls with Tracy absent. CMake, not a preprocessor
// conditional, selects this file over its profiling-enabled sibling: when
// WJS_ENABLE_TRACY is off (the default, and every shipped build)
// libs/core/CMakeLists.txt puts this directory on wjs::lib's include path
// (cmake/Tracy.cmake). Both variants are at the same "wjs/profiling.hpp" path
// and have the same interface, so every call site compiles unchanged either
// way, and docs/profiling.md describes it once.
//
// Everything here is empty. The arguments of a call are still evaluated, which
// is the one difference from Tracy's own macros (they vanish): keep what is
// passed to plot() and Value() free of side effects, and cheap.

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <type_traits>

#include "wjs/profiling_types.hpp"

namespace wjs::profiling {

// True in the variant that hands events to Tracy. A test pins it against the
// CMake option, so that the wrong directory cannot be selected unnoticed.
inline constexpr bool enabled = false;

// What zone<>() returns, with the part of Tracy's ScopedZone that this code
// base uses. Its name is Tracy's, so that a call site is the same for both
// variants (the other one is an alias for tracy::ScopedZone).
//
// [[maybe_unused]] on the type, because `auto zone = zone<"x">();` is
// the whole point of a zone and an unused-variable warning is an error here.
class [[maybe_unused]] Zone {
public:
    Zone() = default;
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    Zone(Zone&&) = delete;
    Zone& operator=(Zone&&) = delete;
    ~Zone() = default;

    // A number shown beside the zone in the profiler and exported by
    // tracy-csvexport: a count, a size.
    void Value([[maybe_unused]] std::uint64_t value) const noexcept {}
    // Free text for this one occurrence of the zone.
    void Text([[maybe_unused]] const char* text, [[maybe_unused]] std::size_t size) const noexcept {
    }
};

// A timed span of the rest of the enclosing scope, named in the template
// argument. The call is the declaration:
//
//     auto zone = wjs::profiling::zone<"solver.solve">();
//
// Names are `<layer>.<thing>`, lower case, dotted (docs/profiling.md). One name
// is one place: the source location is captured the first time it runs, so the
// same name used at two sites shows the first one's line.
template <StaticName Name>
[[nodiscard]] Zone zone(
    [[maybe_unused]] std::source_location location = std::source_location::current()) noexcept {
    return {};
}

// A zone whose name comes from outside C++ as a string, for the QML seam. It
// has the same interface as the enabled variant's and does nothing: the
// constructor takes the name and the object is empty.
class [[maybe_unused]] RuntimeScope {
public:
    explicit RuntimeScope([[maybe_unused]] std::string_view name) noexcept {}

    RuntimeScope(const RuntimeScope&) = delete;
    RuntimeScope& operator=(const RuntimeScope&) = delete;
    RuntimeScope(RuntimeScope&&) = delete;
    RuntimeScope& operator=(RuntimeScope&&) = delete;
    ~RuntimeScope() = default;

    // A number shown beside the zone, as Zone::Value's. Nothing to show here.
    void Value([[maybe_unused]] std::uint64_t value) noexcept {}
};

// A frame boundary on the default set, and on a named one.
inline void frame_mark() noexcept {}
inline void frame_mark([[maybe_unused]] Literal name) noexcept {}

// A value on a timeline plot. Integers, floating point numbers and bool.
template <typename Number>
void plot([[maybe_unused]] Literal name, [[maybe_unused]] Number number) noexcept {
    static_assert(std::is_arithmetic_v<Number>, "plot() takes a number");
}

// A discrete event on the timeline. The text is copied.
inline void message([[maybe_unused]] std::string_view text) noexcept {}

// Names the calling thread in the profiler's thread list. Once per thread:
// every call adds a table entry, so never in a per-frame or per-task path.
inline void thread_name([[maybe_unused]] Literal name) noexcept {}

// Whether a profiler is connected now: never, in a build without one.
inline bool connected() noexcept {
    return false;
}

}  // namespace wjs::profiling
