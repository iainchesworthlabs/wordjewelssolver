// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

#include "camera_problem.hpp"
#include "wjs/scan/frame.hpp"

// What a camera has to do for Mode A, behind the seam the screens and the model
// use. The interface exists for the same reason the feedback engines' backends do
// (apps/app/src/feedback/CMakeLists.txt): one build has a real camera and one does not,
// and no source asks which. CMake compiles one of
//
//   apps/app/src/variants/camera-multimedia   QCamera over Qt Multimedia
//   apps/app/src/variants/camera-none         there is no camera in this kit
//
// and only that one defines make_camera_backend(). The shared class, the QML
// element and the lifecycle are in apps/app/src/camera/camera_source.*; this header is
// C++ only, so a kit without Qt Multimedia compiles every file that includes it.
//
// The rules the backend holds to (docs/design/application.md, "Privacy, built in
// and checked"):
//   - Video only. No audio input is created, and no recording is made.
//   - Frames stay in memory. There is no save, no export and no network; the
//     backend returns an owned copy and drops it after the caller takes it.
//   - The camera runs only between start() and stop(). The caller starts it on
//     scan.ready and stops it as soon as a frame is in hand.

class QObject;

namespace wjs::app {

class CameraBackend {
public:
    // Called when the device, the running state or the problem changes, so that
    // CameraSource can tell QML. It may be called from inside start()/stop() and
    // from a camera signal; the source's slot is cheap and idempotent.
    using Changed = std::function<void()>;

    CameraBackend() = default;
    virtual ~CameraBackend() = default;

    CameraBackend(const CameraBackend&) = delete;
    CameraBackend& operator=(const CameraBackend&) = delete;

    virtual void setChanged(Changed changed) = 0;

    // Whether this build and device have a camera at all. A kit without Qt
    // Multimedia answers false; a desktop with no webcam answers false.
    [[nodiscard]] virtual bool available() const = 0;

    // The cameras' names, for a log or a future device chooser. Empty when there
    // is none.
    [[nodiscard]] virtual QStringList devices() const = 0;

    // `none` when there is nothing wrong, otherwise why: no_camera, permission or
    // failed (CameraProblem). no_camera is a fact about the device, not a fault.
    [[nodiscard]] virtual CameraProblem problem() const = 0;

    // The camera is on and delivering frames (or is about to).
    [[nodiscard]] virtual bool running() const = 0;

    // Starts the first rear camera (a desktop's first camera otherwise). False,
    // with problem() set, when it cannot. Safe to call while running. A platform
    // that has to ask the person first (the permission prompt) may answer true
    // while the question is open: running() turns true when the camera is on, and
    // problem() and the changed callback say if it is refused.
    virtual bool start() = 0;

    // Stops capturing and drops any frame not yet taken. Safe more than once.
    virtual void stop() = 0;

    // The newest frame, converted to the scanner's layout (RGB24, packed rows),
    // or nothing if none has arrived since the last call. The backend keeps no
    // copy: what is returned is the caller's to read and forget.
    [[nodiscard]] virtual std::optional<wjs::scan::OwnedFrame> capture() = 0;

    // Shows the camera in a QML VideoOutput, without this header naming Qt
    // Multimedia: the multimedia backend reads the item's "videoSink" (which the
    // item owns and QML cannot be given another of) and forwards the camera's
    // frames to it. The none backend does nothing. `videoOutput` may be null.
    virtual void attachPreview(QObject* videoOutput) = 0;

    // Forgets the item attachPreview() was given. It is not required before the
    // item is destroyed: the backend watches it weakly.
    virtual void detachPreview() = 0;
};

// The one backend this build's kit can have. Defined once, in the variant
// directory CMake selected (apps/app/CMakeLists.txt).
[[nodiscard]] std::unique_ptr<CameraBackend> make_camera_backend();

}  // namespace wjs::app
