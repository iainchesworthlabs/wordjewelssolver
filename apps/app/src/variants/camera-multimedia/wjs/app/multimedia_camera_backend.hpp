// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QtGlobal>
#include <functional>
#include <memory>

#include "camera_backend.hpp"

// What the Qt Multimedia camera backend lets a test reach. Only the multimedia
// variant has this header (apps/app/src/variants/camera-multimedia); the shared
// interface, camera_backend.hpp, names no Qt Multimedia type, and a kit without
// the module never sees this file.

class QVideoSink;

namespace wjs::app {

// What the backend asks the platform about the camera permission: its state now,
// and a question to the person. The seam exists so that a test can answer for the
// platform: the real answers come from the application (Qt's QCameraPermission),
// which a test run cannot be made to ask.
struct CameraPermission {
    std::function<Qt::PermissionStatus()> status;
    // Asks. `answered` is called later, with the answer, as long as `context`
    // lives; never from inside this call.
    std::function<void(const QObject* context, std::function<void(Qt::PermissionStatus)> answered)>
        request;
};

// The application's: Qt's QCameraPermission, or "granted" when there is no
// application to ask through (a camera made before one).
[[nodiscard]] CameraPermission application_camera_permission();

// A backend that asks `permission` rather than the application.
[[nodiscard]] std::unique_ptr<CameraBackend> make_camera_backend(CameraPermission permission);

// Shows `from`'s frames in the QML VideoOutput `video_output`. A VideoOutput
// owns its sink and its "videoSink" property is read-only: it is read here, and
// the frames are forwarded into it. The result is the connection, which is
// invalid (and nothing is shown) when the item has no video sink. The forward
// ends when `context` or the item's sink is destroyed, or the connection is
// disconnected.
[[nodiscard]] QMetaObject::Connection forward_frames(QVideoSink& from, QObject* video_output,
                                                     const QObject* context);

}  // namespace wjs::app
