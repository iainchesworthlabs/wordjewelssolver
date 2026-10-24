// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>

#include "camera_backend.hpp"
#include "camera_problem.hpp"
#include "wjs/scan/frame.hpp"

namespace wjs::app {

// Mode A's camera: the live view the ready screen shows, and the one frame a
// capture reads. It owns the camera's little lifecycle and nothing else; the
// reading is ScanModel's job (apps/app/src/scan/scan_model.*).
//
//   ready: start()  ──running──▶  the screen shows the preview
//          capture()  ──a frame in hand──▶  CameraSource holds it, the model
//                                            takes it and the camera is stopped
//
// The backend is one of two sources with this same header, chosen by CMake
// (wjs/app/camera_backend.hpp): QCamera over Qt Multimedia, or a backend that
// answers "no_camera" in a kit without the module. A camera that is absent,
// refused or broken is a code in `problem` (the wording file makes the sentence)
// and never a faked frame. The camera runs only while `running`; start() is
// called on scan.ready and stop() as soon as a frame is in hand or the screen
// leaves (docs/design/game.md, "The MVP: one capture").
//
// No frame is kept, written, exported or sent: capture() copies the newest
// picture into memory, takeFrame() hands it to the model, and the model drops it
// after the read. The preview receives the backend's QVideoSink and shows it; it
// never passes through this object.
class CameraSource : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // A camera is there and can be started. False on a device with none, and in
    // a build whose kit has no Qt Multimedia.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // The cameras' names, for a log or a future chooser. Not shown by a screen.
    Q_PROPERTY(QStringList devices READ devices NOTIFY changed)
    // Empty, "no_camera", "permission" or "failed" (CameraProblem). A sentence in
    // the wording file, never a sentence here.
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    // The camera is on: the ready screen's live view, and only while it is.
    Q_PROPERTY(bool running READ running NOTIFY changed)

public:
    explicit CameraSource(QObject* parent = nullptr);
    // A camera over a backend of the caller's: the seam a test uses to hand in a
    // frame without pretending a camera produced it.
    explicit CameraSource(std::unique_ptr<CameraBackend> backend, QObject* parent = nullptr);
    ~CameraSource() override;

    CameraSource(const CameraSource&) = delete;
    CameraSource& operator=(const CameraSource&) = delete;

    [[nodiscard]] bool available() const;
    [[nodiscard]] QStringList devices() const;
    [[nodiscard]] QString problem() const;
    [[nodiscard]] CameraProblem problemKind() const;
    [[nodiscard]] bool running() const;

    // Starts the first camera. False with problem() set when it cannot start.
    Q_INVOKABLE bool start();
    // Stops capturing and drops a frame not yet taken. Safe more than once.
    Q_INVOKABLE void stop();

    // Asks the backend for the newest frame and, if there is one, keeps it for
    // takeFrame(). True when a frame is in hand; also emits captured().
    Q_INVOKABLE bool capture();

    // The frame capture() put in hand, or nothing. It is moved out: the source
    // keeps no second copy.
    [[nodiscard]] std::optional<wjs::scan::OwnedFrame> takeFrame();

    // The QML preview: the backend points a VideoOutput at its sink. The
    // multimedia variant needs this because this header names no Qt Multimedia
    // type, which is what lets a kit without the module compile it.
    Q_INVOKABLE void attachPreview(QObject* videoOutput);
    Q_INVOKABLE void detachPreview();

signals:
    void changed();
    void captured();

private:
    std::unique_ptr<CameraBackend> backend_;
    std::optional<wjs::scan::OwnedFrame> frame_;
};

}  // namespace wjs::app
