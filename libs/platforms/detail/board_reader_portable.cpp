// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/platform/board_reader.hpp"

#include "wjs/scan/reader.hpp"

namespace wjs::platform::board_reader {

// The portable reader, for the platforms that have no recogniser of their own
// (today the three desktops). It is the library's pipeline, unchanged.

Kind kind() noexcept {
    return Kind::portable;
}

std::string_view name() noexcept {
    return "portable";
}

std::expected<scan::BoardReading, scan::ReadError> read(const scan::Frame& frame,
                                                        const scan::ReadOptions& options) {
    return scan::read_board(frame, options);
}

}  // namespace wjs::platform::board_reader
