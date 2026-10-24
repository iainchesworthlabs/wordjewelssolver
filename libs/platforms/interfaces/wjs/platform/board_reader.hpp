// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

#include "wjs/scan/frame.hpp"
#include "wjs/scan/reading.hpp"

// The board reader, for Mode A: a frame in, a reading of the board out. There is
// exactly one definition of each function below per platform, chosen in CMake by
// the platform's platform.cmake.
//
// The three desktops use the portable reader (wjs::scan::read_board), from one
// file all of them name. A platform with something better replaces it behind this
// interface: the iPad is meant to use Vision for what Vision does well, and that
// is Objective-C++ under libs/platforms/ios, which does not exist yet
// (docs/design/scanner.md, "What is left for a Mac and an iPad"). Whatever reads
// the frame, the answer is the same wjs::scan::BoardReading, so everything above
// this interface (the check screen's data flow, the confirm-and-correct draft) is
// the same on every platform.
//
// A reading is not a board: the solver sees only what BoardDraft::confirm() makes
// of it (wjs/scan/confirmation.hpp).

namespace wjs::platform::board_reader {

enum class Kind : std::uint8_t {
    // wjs::scan's own pipeline: found, drawn square and read by template matching.
    portable,
    // The platform's own recogniser.
    native,
};

[[nodiscard]] Kind kind() noexcept;

// A short name for logs and the About screen: "portable", "vision".
[[nodiscard]] std::string_view name() noexcept;

// Reads one frame. The frame is only looked at: nothing is kept, written or sent.
[[nodiscard]] std::expected<scan::BoardReading, scan::ReadError> read(
    const scan::Frame& frame, const scan::ReadOptions& options = {});

}  // namespace wjs::platform::board_reader
