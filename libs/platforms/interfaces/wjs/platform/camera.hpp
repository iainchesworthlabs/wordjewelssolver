// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/scan/frame.hpp"

// The camera, for Mode A, the family scanner. There is exactly one definition of
// each function below per platform, chosen in CMake by the platform's
// platform.cmake; the code has no preprocessor branching. The model is
// Cubeulator's interfaces/camera.hpp, without its OpenCV types (a frame is
// wjs::scan::OwnedFrame) and without its photometry lock, which is about sampling
// colours and not about reading letters.
//
// Rules this interface holds to (docs/design/application.md, "Privacy, built in
// and checked"):
//   - Video only. Nothing here opens an audio input, and a platform that needs to
//     declare what it uses declares the camera and not the microphone.
//   - Frames stay in memory. There is no function that saves a frame, and no
//     implementation may write one (the build check scan.no_frame_files holds
//     the library and this interface to it).
//   - The camera is on only while someone is waiting for a frame: the caller
//     starts it for the capture screen and stops it when it leaves. Nothing here
//     keeps it running.
//
// On a platform with no native camera backend (today the three desktops), every
// function answers `unavailable` or "nothing". The desktops' camera is not this
// interface: Mode A's live view and one capture are the application's
// CameraSource over Qt Multimedia (apps/app/src/camera), selected by CMake with the
// optional-module variant pattern. This interface stays for a platform whose own
// camera API must be used (the iPad's AVCaptureSession), and ScanModel falls
// back to it when no CameraSource is supplied. With neither, Mode A offers
// "Type in your board" instead (docs/design/game.md, "No camera").

namespace wjs::platform::camera {

// Which way the lens points relative to the user. A rear camera's preview is the
// scene and is not mirrored; a front camera's is, selfie-style; "unknown" is what
// a desktop webcam reports and is treated as facing the user.
enum class Facing : std::uint8_t { front, rear, unknown };

// A capture device as the platform's camera API lists it.
struct DeviceInfo {
    std::string identifier;
    std::string display_name;
    Facing facing = Facing::unknown;
};

enum class CameraError : std::uint8_t {
    // This platform has no camera backend, or the device has no camera.
    unavailable,
    // The person said no (or has not been asked and the platform will not ask).
    permission_denied,
    // The device passed to start() is not one enumerate_devices() listed.
    no_such_device,
    // start() while a capture is running.
    already_running,
    // The platform's camera API failed for another reason.
    failed,
};

[[nodiscard]] constexpr std::string_view to_string(CameraError error) noexcept {
    switch (error) {
        case CameraError::unavailable:
            return "there is no camera to use";
        case CameraError::permission_denied:
            return "the camera is not allowed";
        case CameraError::no_such_device:
            return "no such camera";
        case CameraError::already_running:
            return "the camera is already running";
        case CameraError::failed:
            return "the camera failed";
    }
    return "unknown camera error";
}

// The cameras the platform lists, or `unavailable` if it has no way to.
[[nodiscard]] std::expected<std::vector<DeviceInfo>, CameraError> enumerate_devices();

// Starts capturing from a device returned by enumerate_devices().
[[nodiscard]] std::expected<void, CameraError> start(const DeviceInfo& device);

// Stops capturing. Safe to call when nothing is running, and more than once.
void stop();

// Opens the operating system's settings page for this app, for when the camera was
// refused and there is no way left to ask again. Nothing on a platform with no
// per-app camera permission.
void open_permission_settings();

// The most recently captured frame not yet taken, or nothing if none has arrived
// since the last call or no capture is running. A single-slot mailbox, latest
// wins: a caller that polls slower than the camera sees only the newest frame.
// The frame's `t_ms` is steady-clock milliseconds as close to the exposure as the
// platform allows.
[[nodiscard]] std::optional<scan::OwnedFrame> try_pop_latest_frame();

[[nodiscard]] bool is_capturing() noexcept;

}  // namespace wjs::platform::camera
