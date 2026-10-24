// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/platform/camera.hpp"

namespace wjs::platform::camera {

// A platform with no native camera backend. Named by the platform.cmake of every
// platform that has none (today the three desktops): a platform's own capture API
// (AVFoundation on the iPad) is what this interface is for. Mode A's desktop
// camera is the application's CameraSource over Qt Multimedia
// (apps/app/src/camera), not this interface, so a desktop with a webcam has a live
// view through the application and a desktop without one still gets the typed
// board from here. The answer is always "unavailable".

std::expected<std::vector<DeviceInfo>, CameraError> enumerate_devices() {
    return std::unexpected(CameraError::unavailable);
}

std::expected<void, CameraError> start(const DeviceInfo& /*device*/) {
    return std::unexpected(CameraError::unavailable);
}

void stop() {}

void open_permission_settings() {}

std::optional<scan::OwnedFrame> try_pop_latest_frame() {
    return std::nullopt;
}

bool is_capturing() noexcept {
    return false;
}

}  // namespace wjs::platform::camera
