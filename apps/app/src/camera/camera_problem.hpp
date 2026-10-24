// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <cstdint>
#include <string_view>

namespace wjs::app {

// Why Mode A's camera is not working (or no longer is). Inside the program it is
// this enum; QML is given the code, a lower-case word, and ScanWording.qml turns
// the code into a sentence. Two sources say it: CameraSource (Qt Multimedia, or
// none) and the platform's own camera interface, which ScanModel falls back to,
// and each has its own words for the same few facts. Both vocabularies reach QML
// today (ScanWording and its tests name them), so both are here; folding
// `unavailable` into `no_camera` and `permission_denied` into `permission` is a
// change to those QML files first.
enum class CameraProblem : std::uint8_t {
    none,
    // The camera source's.
    no_camera,   // no camera on the device or in the kit: a fact, not a fault
    permission,  // the person said no
    failed,      // the device or the platform failed
    // The platform interface's (wjs/platform/camera.hpp).
    unavailable,        // the platform has no camera backend
    permission_denied,  // the person said no
    no_such_device,
    already_running,
};

// The code QML is given; empty for `none`.
[[nodiscard]] constexpr std::string_view code(CameraProblem problem) noexcept {
    switch (problem) {
        case CameraProblem::none:
            return "";
        case CameraProblem::no_camera:
            return "no_camera";
        case CameraProblem::permission:
            return "permission";
        case CameraProblem::failed:
            return "failed";
        case CameraProblem::unavailable:
            return "unavailable";
        case CameraProblem::permission_denied:
            return "permission_denied";
        case CameraProblem::no_such_device:
            return "no_such_device";
        case CameraProblem::already_running:
            return "already_running";
    }
    return "failed";
}

[[nodiscard]] inline QString problem_code(CameraProblem problem) {
    const std::string_view text = code(problem);
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace wjs::app
