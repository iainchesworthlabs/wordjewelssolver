# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# Tracy.cmake
#
# Defines an INTERFACE target `wjs::tracy` that, when WJS_ENABLE_TRACY is on,
# links Tracy's client library. The profiling calls the sources make
# (wjs/profiling.hpp) are one of two headers with the same path under
# libs/core/variants/; libs/core/CMakeLists.txt puts exactly one of them on
# wjs::lib's include path, chosen by this option and not by an #if, so every
# instrumented call site compiles unchanged either way. The disabled variant
# holds empty functions: a build without Tracy pays nothing for the
# instrumentation and does not need Tracy present.
#
# The switch itself is declared in the top-level CMakeLists.txt, before
# project(): the vcpkg manifest feature ("profiling" in vcpkg.json) must be
# appended before the toolchain reads the manifest. The default there is ON
# for a Debug build type and OFF otherwise, so:
#
#   cmake --preset windows-msvc-debug      # instrumented: connect the profiler
#   cmake --preset windows-msvc-profiling  # instrumented AND fit for numbers
#   cmake --preset windows-msvc-release    # not instrumented (as shipped)
#
# Debug builds are instrumented so a running development session can be
# connected to at any time; their timings are not numbers about the code
# (the sanitizers and no optimisation dominate), which is what the
# *-profiling presets (RelWithDebInfo) are for. A Release build can be
# instrumented by hand with -DWJS_ENABLE_TRACY=ON, for data from a real
# session.
#
# The feature also builds Tracy's command-line tools for the HOST
# (tracy-capture and tracy-csvexport), so a trace can be recorded and dumped
# without the GUI profiler. docs/profiling.md says how.
#
# Nothing here is for shipping: a Tracy client opens a TCP listener and answers
# UDP discovery broadcasts, which a released binary must not do. No preset a
# package is built from turns this on, and the profiling.shipped_presets test
# (tests/profiling/check_presets.cmake) holds that.
# ---------------------------------------------------------------------------

add_library(wjs_tracy INTERFACE)
add_library(wjs::tracy ALIAS wjs_tracy)

# Which wjs/profiling.hpp wjs::lib's users see.
if(WJS_ENABLE_TRACY)
    set(WJS_PROFILING_VARIANT_DIR "${PROJECT_SOURCE_DIR}/libs/core/variants/profiling-enabled")
else()
    set(WJS_PROFILING_VARIANT_DIR "${PROJECT_SOURCE_DIR}/libs/core/variants/profiling-disabled")
endif()

if(WJS_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(wjs_tracy INTERFACE Tracy::TracyClient)

    # The Debug client is built with the clock for its timer, not the CPU's cycle counter
    # (cmake/vcpkg-ports/tracy/portfile.cmake: the cycle counter is calibrated by a sleep of
    # 200 ms in every program's start, and a Debug build's timings are not numbers). The
    # timer is read inline from Tracy's headers, so the code that includes them has to be
    # built with the same two definitions as the client; the Release client, and the
    # *-profiling presets that link it, keep the cycle counter.
    target_compile_definitions(wjs_tracy INTERFACE
        "$<$<CONFIG:Debug>:TRACY_TIMER_FALLBACK;TRACY_DISALLOW_HW_TIMER>")

    # Coverage and Tracy measure the same program and would talk over each
    # other: a build with both is an error in the numbers, and nothing else
    # will say so. A Debug build with Tracy is the default now, so it gets a
    # note instead of a warning: connect the profiler freely, and read timings
    # from a *-profiling preset.
    if(WJS_ENABLE_COVERAGE)
        message(WARNING
            "WJS_ENABLE_TRACY and WJS_ENABLE_COVERAGE are both on. Coverage "
            "and profiler instrumentation dominate each other's numbers; "
            "turn one of them off for a build worth reading.")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS
            "Tracy is on for this Debug build (the default): connect the "
            "profiler to the running program. Timings from a Debug build are "
            "not numbers about the code - the sanitizers and no optimisation "
            "dominate them - so use a *-profiling preset for numbers worth "
            "reading.")
    endif()
endif()

# The one interface that makes wjs/profiling.hpp real: the library's public
# include path (the wrapper is a library header, and its types header lives
# beside the others), the chosen variant's include path and, with Tracy on, its
# client. wjs::lib carries it PUBLIC, so every target that links the library is
# instrumented the same way; a target that does not link the library but calls
# the wrapper (the board scene, which is Qt-only) links this instead. There is
# still no second door to Tracy.
add_library(wjs_profiling INTERFACE)
add_library(wjs::profiling ALIAS wjs_profiling)
target_include_directories(wjs_profiling INTERFACE
    "${PROJECT_SOURCE_DIR}/libs/core/include"
    "${PROJECT_BINARY_DIR}/generated"
    "${WJS_PROFILING_VARIANT_DIR}")
target_link_libraries(wjs_profiling INTERFACE wjs::tracy)
