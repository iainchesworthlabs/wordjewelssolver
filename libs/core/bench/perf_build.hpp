// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What kind of build the performance tests are running in, as far as
// measuring it goes. CMake decides (tests/CMakeLists.txt, the wjs_perf_tests
// block) and passes the facts as compile definitions; nothing here asks the
// compiler or the operating system, so no preprocessor conditional is needed.
//
//   kOptimised         the configuration is Release, RelWithDebInfo or
//                      MinSizeRel. Everything in this tier is sized for an
//                      optimised build (the corpus is under a second); in
//                      Debug it is much longer, and a Debug build has nothing
//                      to add: the visit counts are the same in every build.
//   kInstrumented      coverage, or Tracy, is compiled in. The code is what it
//                      was, but a clock reading is not about it (docs/profiling.md).
//   kTimingMeaningful  a stopwatch can be read as a statement about the
//                      program.

#include <string_view>

namespace wjs::perf {

inline constexpr bool kOptimised = WJS_PERF_OPTIMISED != 0;
inline constexpr bool kInstrumented = WJS_PERF_INSTRUMENTED != 0;
inline constexpr bool kTimingMeaningful = kOptimised && !kInstrumented;

// The configuration name, for the message a skipped case prints.
inline constexpr std::string_view kBuildType = WJS_PERF_BUILD_TYPE;

// The word list the budgets are measured on (ENABLE, data/dictionaries/en.txt),
// as a path in this checkout.
inline constexpr std::string_view kDictionaryPath = WJS_PERF_DICTIONARY;

// The project's dice, data/dice/<n>x<n>.txt, as a directory in this checkout:
// the standard board's work is measured on the sets that ship.
inline constexpr std::string_view kDiceDirectory = WJS_PERF_DICE_DIRECTORY;

}  // namespace wjs::perf
