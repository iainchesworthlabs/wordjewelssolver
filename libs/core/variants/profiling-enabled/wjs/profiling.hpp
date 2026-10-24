// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The profiling calls with Tracy behind them. CMake, not a preprocessor
// conditional, selects this file over its profiling-disabled sibling: when
// WJS_ENABLE_TRACY is on (the *-profiling presets, and nothing that ships)
// libs/core/CMakeLists.txt puts this directory on wjs::lib's include path
// (cmake/Tracy.cmake). Both variants are at the same "wjs/profiling.hpp" path
// and have the same interface, so every call site compiles unchanged either
// way, and docs/profiling.md describes it once.
//
// Tracy's own interface is macros, and this repository holds no #define and no
// #if (docs/platform-isolation.md). What its macros do is ordinary C++ in
// namespace tracy, and this file calls that directly. TRACY_ENABLE and
// TRACY_ON_DEMAND are compile definitions of Tracy's own target, which
// wjs::tracy links; they are not written in any source here.
//
// Tracy's header does define macros (ZoneScoped and the rest) in every file
// that includes this one. That is harmless unless one of their names collides
// with something here, which none does.

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <type_traits>

#include <tracy/Tracy.hpp>

#include "wjs/profiling_types.hpp"

namespace wjs::profiling {

// True in the variant that hands events to Tracy. A test pins it against the
// CMake option, so that the wrong directory cannot be selected unnoticed.
inline constexpr bool enabled = true;

// The static description of a place in the code: its name, function, file and
// line. Tracy keeps the pointer, which is why zone<>() makes one a
// function-local static.
using SourceLocation = tracy::SourceLocationData;

// What zone<>() returns: Tracy's own zone object, with its own member names
// (Value, Text, Name). Not copyable and not movable, so it is built where it
// is declared, by guaranteed copy elision, as Tracy's own macro builds it.
using Zone = tracy::ScopedZone;

// A timed span of the rest of the enclosing scope, named in the template
// argument. The call is the declaration:
//
//     auto zone = wjs::profiling::zone<"solver.solve">();
//
// Names are `<layer>.<thing>`, lower case, dotted (docs/profiling.md). The
// SourceLocation is a function-local static of this template, so there is one
// per name, built the first time the zone runs from the call site's
// std::source_location (a default argument is evaluated at the call). The
// consequence is that one name is one place: the same name used at two sites
// shows the first one's line. tests/profiling/check_zone_names.cmake finds a
// name used twice.
//
// Between connections of a profiler a zone costs the guard of that static and
// Tracy's connection check (TRACY_ON_DEMAND).
template <StaticName Name>
[[nodiscard]] Zone zone(std::source_location where = std::source_location::current()) {
    static const SourceLocation location{Name.text.data(), where.function_name(), where.file_name(),
                                         where.line(), 0};
    return Zone{&location};
}

// A zone whose name comes from outside C++ as a string, for the QML seam. The
// constructor opens the zone and the destructor closes it, so a scope that has
// to outlive its declaration -- a QML begin() and a later end() -- can be held
// in a std::optional and reset.
//
// A run-time name is not a compile-time one: Tracy cannot know a
// SourceLocationData that did not exist when the program was written, so the
// zone is opened the way Tracy's own transient zones are (its ZoneTransient
// macro): the name goes with the zone and the profiler registers it itself.
// Every run-time location is therefore the QML seam's -- function and file
// "QML", line 0 -- and a name used again is the same place in the table the CSV
// shows, because it is the same name. The allocation a transient zone makes is
// per zone and only while a profiler is connected; the seam is used a few times
// a frame, not in a loop.
class [[maybe_unused]] RuntimeScope {
public:
    explicit RuntimeScope(std::string_view name)
        : zone_(std::uint32_t{0}, "QML", std::size_t{3}, "QML", std::size_t{3}, name.data(),
                name.size(), std::int32_t{0}) {}

    RuntimeScope(const RuntimeScope&) = delete;
    RuntimeScope& operator=(const RuntimeScope&) = delete;
    RuntimeScope(RuntimeScope&&) = delete;
    RuntimeScope& operator=(RuntimeScope&&) = delete;
    ~RuntimeScope() = default;

    // A number shown beside the zone, as Zone::Value's.
    void Value(std::uint64_t value) noexcept { zone_.Value(value); }

private:
    Zone zone_;
};

// A frame boundary on the default set, and on a named one ("UI", "Capture").
inline void frame_mark() {
    tracy::Profiler::SendFrameMark(nullptr);
}

inline void frame_mark(Literal name) {
    tracy::Profiler::SendFrameMark(name.c_str());
}

// A value on a timeline plot. Tracy's PlotData is overloaded on int64_t, float
// and double, so an int is ambiguous between all three; integers (and bool) go
// as integers and anything floating as a double.
template <typename Number>
void plot(Literal name, Number value) {
    static_assert(std::is_arithmetic_v<Number>, "plot() takes a number");
    if constexpr (std::is_floating_point_v<Number>) {
        tracy::Profiler::PlotData(name.c_str(), static_cast<double>(value));
    } else {
        tracy::Profiler::PlotData(name.c_str(), static_cast<std::int64_t>(value));
    }
}

// A discrete event on the timeline. The text is copied.
inline void message(std::string_view text) {
    // Tracy 0.14.1's message entry point (its TracyMessage macro):
    // LogString(source, severity, color, callstack depth, length, text). Depth 0
    // keeps the callstack out of it, which is what a message needs.
    tracy::Profiler::LogString(tracy::MessageSourceType::User, tracy::MessageSeverity::Info, 0, 0,
                               text.size(), text.data());
}

// Names the calling thread in the profiler's thread list. Once per thread:
// every call adds a table entry, so never in a per-frame or per-task path.
inline void thread_name(Literal name) {
    tracy::SetThreadName(name.c_str());
}

// Whether a profiler is connected now. In on-demand mode nothing is recorded
// until one is; a workload that has to be seen from its first moment (the
// application's --smoke) can wait for this. The first call starts Tracy's
// client, and so its listener, if nothing has yet.
inline bool connected() {
    return tracy::GetProfiler().IsConnected();
}

}  // namespace wjs::profiling
