// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera_source.hpp"

#include <utility>

#include "wjs/profiling.hpp"

namespace wjs::app {

CameraSource::CameraSource(QObject* parent) : CameraSource(make_camera_backend(), parent) {}

CameraSource::CameraSource(std::unique_ptr<CameraBackend> backend, QObject* parent)
    : QObject(parent), backend_(std::move(backend)) {
    if (backend_) {
        backend_->setChanged([this] { emit changed(); });
    }
}

CameraSource::~CameraSource() {
    // The backend's camera signals must not reach a half-destroyed source while
    // it is being emptied: forget the callback before stopping it.
    if (backend_) {
        backend_->setChanged({});
        backend_->stop();
    }
}

bool CameraSource::available() const {
    return backend_ && backend_->available();
}

QStringList CameraSource::devices() const {
    return backend_ ? backend_->devices() : QStringList{};
}

QString CameraSource::problem() const {
    return problem_code(problemKind());
}

CameraProblem CameraSource::problemKind() const {
    return backend_ ? backend_->problem() : CameraProblem::no_camera;
}

bool CameraSource::running() const {
    return backend_ && backend_->running();
}

bool CameraSource::start() {
    frame_.reset();
    return backend_ && backend_->start();
}

void CameraSource::stop() {
    frame_.reset();
    if (backend_) {
        backend_->stop();
    }
}

bool CameraSource::capture() {
    if (!backend_) {
        return false;
    }
    std::optional<wjs::scan::OwnedFrame> frame = backend_->capture();
    if (!frame) {
        return false;
    }
    // One frame at a time: a caller that did not take the last one loses it. The
    // camera is stopped by the model as soon as it has read it.
    frame_ = std::move(*frame);
    // A frame of the camera is a boundary in a profile, apart from the window's
    // own frames ("UI").
    wjs::profiling::frame_mark("Capture");
    emit captured();
    return true;
}

std::optional<wjs::scan::OwnedFrame> CameraSource::takeFrame() {
    std::optional<wjs::scan::OwnedFrame> frame = std::move(frame_);
    frame_.reset();
    return frame;
}

void CameraSource::attachPreview(QObject* videoOutput) {
    if (backend_) {
        backend_->attachPreview(videoOutput);
    }
}

void CameraSource::detachPreview() {
    if (backend_) {
        backend_->detachPreview();
    }
}

}  // namespace wjs::app
