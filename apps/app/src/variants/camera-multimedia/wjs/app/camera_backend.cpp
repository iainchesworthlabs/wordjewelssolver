// SPDX-License-Identifier: GPL-3.0-or-later
// The Qt Multimedia camera: QCamera into a QMediaCaptureSession, its frames into
// a QVideoSink, and the newest one converted on capture. No ImageCapture, no
// QMediaRecorder and no file: a frame is memory, and the model drops it after the
// read (docs/design/scanner.md, "The Qt Multimedia desktop backend").
//
// This file is compiled only when the kit has Qt Multimedia
// (apps/app/CMakeLists.txt). The other variant is
// apps/app/src/variants/camera-none/wjs/app/camera_backend.cpp; both define
// make_camera_backend() and the shared interface is in
// apps/app/src/camera/camera_backend.hpp.

#include "camera_backend.hpp"

#include <QCamera>
#include <QCameraDevice>
#include <QCoreApplication>
#include <QDebug>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPermissions>
#include <QPointer>
#include <QVariant>
#include <QVideoFrame>
#include <QVideoSink>
#include <memory>
#include <optional>
#include <utility>

#include "frame_conversion.hpp"
#include "multimedia_camera_backend.hpp"

namespace wjs::app {
namespace {

// The camera the desktop should use first: a rear camera where the device has
// one (a tablet), otherwise the first the platform lists.
[[nodiscard]] QCameraDevice choose_device(const QList<QCameraDevice>& devices) {
    for (const QCameraDevice& device : devices) {
        if (device.position() == QCameraDevice::Position::BackFace) {
            return device;
        }
    }
    return devices.front();
}

}  // namespace

CameraPermission application_camera_permission() {
    CameraPermission permission;
    permission.status = [] {
        if (QCoreApplication* application = QCoreApplication::instance()) {
            return application->checkPermission(QCameraPermission{});
        }
        return Qt::PermissionStatus::Granted;
    };
    permission.request = [](const QObject* context,
                            std::function<void(Qt::PermissionStatus)> answered) {
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr) {
            answered(Qt::PermissionStatus::Granted);
            return;
        }
        application->requestPermission(QCameraPermission{}, context,
                                       [answered = std::move(answered)](const QPermission& answer) {
                                           answered(answer.status());
                                       });
    };
    return permission;
}

QMetaObject::Connection forward_frames(QVideoSink& from, QObject* video_output,
                                       const QObject* context) {
    if (video_output == nullptr) {
        return {};
    }
    // VideoOutput.videoSink is read-only: the item makes its sink, and setting the
    // property from here is refused (the result of setProperty says so, which is
    // how the preview used to stay blank without a word).
    const QPointer<QVideoSink> target =
        qvariant_cast<QVideoSink*>(video_output->property("videoSink"));
    if (target.isNull()) {
        return {};
    }
    return QObject::connect(&from, &QVideoSink::videoFrameChanged, context,
                            [target](const QVideoFrame& frame) {
                                if (target) {
                                    target->setVideoFrame(frame);
                                }
                            });
}

class MultimediaCameraBackend final : public CameraBackend {
public:
    explicit MultimediaCameraBackend(CameraPermission permission)
        : permission_(std::move(permission)) {
        session_.setVideoSink(&sink_);
        QObject::connect(&sink_, &QVideoSink::videoFrameChanged, &owner_,
                         [this](const QVideoFrame& frame) {
                             // A live view may deliver dozens of a second. Only
                             // the newest is kept, and only until it is taken.
                             if (frame.isValid()) {
                                 newest_ = frame;
                             }
                         });
    }

    ~MultimediaCameraBackend() override {
        // Stop first, then take the camera out of the session: the order Qt's
        // own examples use, and the one that leaves no capture running while the
        // objects are torn down. The session is told to forget the sink, which it
        // would otherwise still point at when the sink goes (they are members, and
        // the sink is declared first so that it goes last: this is the belt to
        // that order's braces).
        wanted_ = false;
        if (camera_) {
            camera_->stop();
            session_.setCamera(nullptr);
        }
        session_.setVideoSink(nullptr);
    }

    void setChanged(Changed changed) override { changed_ = std::move(changed); }

    [[nodiscard]] bool available() const override {
        return !QMediaDevices::videoInputs().isEmpty();
    }

    [[nodiscard]] QStringList devices() const override {
        QStringList names;
        for (const QCameraDevice& device : QMediaDevices::videoInputs()) {
            names.append(device.description().isEmpty() ? QString::fromUtf8(device.id())
                                                        : device.description());
        }
        return names;
    }

    [[nodiscard]] CameraProblem problem() const override {
        // While the person is being asked, the camera is neither absent nor
        // refused: some platforms list no camera until the question is answered.
        if (asking_) {
            return CameraProblem::none;
        }
        if (QMediaDevices::videoInputs().isEmpty() && problem_ != CameraProblem::permission) {
            return CameraProblem::no_camera;
        }
        return problem_;
    }

    [[nodiscard]] bool running() const override {
        return camera_ != nullptr && camera_->isActive();
    }

    bool start() override {
        wanted_ = true;
        if (running() || asking_) {
            return true;
        }
        // The permission comes before the device list: a platform that gates the
        // camera lists none until the person has said yes.
        switch (permission_.status ? permission_.status() : Qt::PermissionStatus::Granted) {
            case Qt::PermissionStatus::Denied:
                wanted_ = false;
                problem_ = CameraProblem::permission;
                notify();
                return false;
            case Qt::PermissionStatus::Undetermined:
                if (permission_.request) {
                    return ask();
                }
                break;
            case Qt::PermissionStatus::Granted:
                break;
        }
        return begin();
    }

    void stop() override {
        // An answer that arrives after this does not start the camera.
        wanted_ = false;
        if (camera_) {
            camera_->stop();
        }
        newest_.reset();
        // The sink keeps the last frame it rendered; clearing it here is what
        // makes "the buffer is dropped when the camera stops" true of the
        // preview as well as of the capture.
        sink_.setVideoFrame(QVideoFrame{});
        notify();
    }

    [[nodiscard]] std::optional<wjs::scan::OwnedFrame> capture() override {
        if (!newest_ || !newest_->isValid()) {
            return std::nullopt;
        }
        // The frame is taken out of the mailbox and converted; what the sink
        // holds is a shared buffer, and nothing keeps it after this call.
        const QVideoFrame frame = std::move(*newest_);
        newest_.reset();
        const QImage image = frame.toImage();
        if (image.isNull()) {
            problem_ = CameraProblem::failed;
            notify();
            return std::nullopt;
        }
        std::optional<wjs::scan::OwnedFrame> owned = frame_from_image(image);
        if (!owned) {
            problem_ = CameraProblem::failed;
            notify();
        }
        return owned;
    }

    void attachPreview(QObject* videoOutput) override {
        detachPreview();
        preview_ = videoOutput;
        if (preview_ == nullptr) {
            return;
        }
        forward_ = forward_frames(sink_, videoOutput, &owner_);
        if (!forward_) {
            qWarning() << "The camera preview has no video sink to show the camera in.";
        }
    }

    void detachPreview() override {
        QObject::disconnect(forward_);
        forward_ = {};
        preview_.clear();
    }

private:
    // Asks the person, once; the answer starts the camera if it is still wanted.
    bool ask() {
        asking_ = true;
        problem_ = CameraProblem::none;
        permission_.request(&owner_, [this](Qt::PermissionStatus answer) {
            asking_ = false;
            if (!wanted_) {
                return;
            }
            if (answer == Qt::PermissionStatus::Granted) {
                begin();
            } else {
                wanted_ = false;
                problem_ = CameraProblem::permission;
                notify();
            }
        });
        notify();
        return true;
    }

    bool begin() {
        const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        if (cameras.isEmpty()) {
            wanted_ = false;
            problem_ = CameraProblem::no_camera;
            notify();
            return false;
        }

        const QCameraDevice chosen = choose_device(cameras);
        if (!camera_) {
            camera_ = std::make_unique<QCamera>(chosen);
            session_.setCamera(camera_.get());
            QObject::connect(camera_.get(), &QCamera::errorOccurred, &owner_,
                             [this](QCamera::Error error, const QString&) {
                                 if (error != QCamera::NoError) {
                                     problem_ = CameraProblem::failed;
                                     if (camera_) {
                                         camera_->stop();
                                     }
                                     notify();
                                 }
                             });
            QObject::connect(camera_.get(), &QCamera::activeChanged, &owner_,
                             [this](bool /*active*/) { notify(); });
        } else if (camera_->cameraDevice() != chosen) {
            camera_->setCameraDevice(chosen);
        }

        problem_ = CameraProblem::none;
        // A camera that then fails is an errorOccurred, which sets "failed".
        camera_->start();
        notify();
        return true;
    }

    void notify() {
        if (changed_) {
            changed_();
        }
    }

    CameraPermission permission_;
    // A QObject for the connections' receiver context: it keeps them alive while
    // this backend lives, and drops them before the sink and the camera go.
    QObject owner_;
    // The sink is declared before the session, so that the session, which points
    // at it, is destroyed first.
    QVideoSink sink_;
    QMediaCaptureSession session_;
    std::unique_ptr<QCamera> camera_;
    QPointer<QObject> preview_;
    QMetaObject::Connection forward_;
    std::optional<QVideoFrame> newest_;
    CameraProblem problem_ = CameraProblem::none;
    // The camera is wanted: start() was called and stop() has not been since. An
    // answer to the permission question starts the camera only when it is.
    bool wanted_ = false;
    bool asking_ = false;
    Changed changed_;
};

std::unique_ptr<CameraBackend> make_camera_backend(CameraPermission permission) {
    return std::make_unique<MultimediaCameraBackend>(std::move(permission));
}

std::unique_ptr<CameraBackend> make_camera_backend() {
    return make_camera_backend(application_camera_permission());
}

}  // namespace wjs::app
