// SPDX-License-Identifier: GPL-3.0-or-later
// The Qt Multimedia camera backend, constructed headless: there is no camera on a
// build machine, and a test never opens one. What is checked is what the backend
// decides without a device: how a QML preview is fed, what it does when the
// platform refuses the camera or has not been asked, and that it is safe to stop,
// to attach to and to destroy with nothing there. The platform's permission
// answers are a fake's (CameraPermission is the seam for it). Compiled only when
// the kit has Qt Multimedia, the condition the backend itself is built under
// (tests/CMakeLists.txt).
//
// What this cannot show is a live preview on a real webcam: the frames here are
// pictures handed to a QVideoSink, not a camera's.

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <QVideoFrame>
#include <QVideoSink>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <memory>

#include "test_support.hpp"
#include "wjs/app/multimedia_camera_backend.hpp"

namespace {

using wjs::app::CameraBackend;
using wjs::app::CameraPermission;
using wjs::app::CameraProblem;

// The platform's camera permission, answered by the test.
struct FakePermission {
    Qt::PermissionStatus status = Qt::PermissionStatus::Granted;
    int requests = 0;
    std::function<void(Qt::PermissionStatus)> answer;

    [[nodiscard]] CameraPermission seam() {
        CameraPermission permission;
        permission.status = [this] { return status; };
        permission.request = [this](const QObject*,
                                    std::function<void(Qt::PermissionStatus)> answered) {
            ++requests;
            answer = std::move(answered);
        };
        return permission;
    }
};

// A QML VideoOutput as the backend sees one: an object with a "videoSink"
// property holding a sink it owns.
struct FakeVideoOutput {
    FakeVideoOutput() { item.setProperty("videoSink", QVariant::fromValue(&sink)); }

    QObject item;
    QVideoSink sink;
};

[[nodiscard]] QVideoFrame a_frame() {
    QImage image(2, 2, QImage::Format_RGB888);
    image.fill(QColor(10, 20, 30));
    return QVideoFrame(image);
}

struct Counter {
    int changes = 0;
    [[nodiscard]] CameraBackend::Changed callback() {
        return [this] { ++changes; };
    }
};

}  // namespace

TEST_CASE("the camera's frames are forwarded into the preview item's own sink",
          "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    QVideoSink camera_sink;
    FakeVideoOutput output;
    QObject context;

    const QMetaObject::Connection connection =
        wjs::app::forward_frames(camera_sink, &output.item, &context);
    REQUIRE(connection);

    camera_sink.setVideoFrame(a_frame());
    CHECK(output.sink.videoFrame().isValid());
    CHECK(output.sink.videoFrame().width() == 2);

    // After the forward is ended nothing more arrives.
    QObject::disconnect(connection);
    output.sink.setVideoFrame(QVideoFrame{});
    camera_sink.setVideoFrame(a_frame());
    CHECK_FALSE(output.sink.videoFrame().isValid());
}

TEST_CASE("an item with no video sink, or none at all, is not forwarded to",
          "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    QVideoSink camera_sink;
    QObject context;

    CHECK_FALSE(wjs::app::forward_frames(camera_sink, nullptr, &context));

    // An item that has no "videoSink", or has a null one: the result says so, and
    // the preview stays blank with a reason rather than without one.
    QObject bare;
    CHECK_FALSE(wjs::app::forward_frames(camera_sink, &bare, &context));
    QObject null_sink;
    null_sink.setProperty("videoSink", QVariant::fromValue(static_cast<QVideoSink*>(nullptr)));
    CHECK_FALSE(wjs::app::forward_frames(camera_sink, &null_sink, &context));
}

TEST_CASE("a preview whose sink is destroyed is not written to", "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    QVideoSink camera_sink;
    QObject context;
    QObject item;
    {
        auto sink = std::make_unique<QVideoSink>();
        item.setProperty("videoSink", QVariant::fromValue(sink.get()));
        REQUIRE(wjs::app::forward_frames(camera_sink, &item, &context));
    }
    // The sink is gone; a frame now must neither crash nor reach anything.
    camera_sink.setVideoFrame(a_frame());
    SUCCEED("the frame was dropped");
}

TEST_CASE("the backend attaches a preview, replaces it and lets it go", "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    FakePermission permission;
    std::unique_ptr<CameraBackend> backend = wjs::app::make_camera_backend(permission.seam());
    FakeVideoOutput first;
    FakeVideoOutput second;

    backend->attachPreview(&first.item);
    backend->attachPreview(&second.item);  // the first is let go
    backend->attachPreview(nullptr);
    backend->detachPreview();
    backend->detachPreview();  // safe more than once

    // An item destroyed before the backend is not touched afterwards.
    {
        FakeVideoOutput short_lived;
        backend->attachPreview(&short_lived.item);
    }
    backend->detachPreview();
    SUCCEED("no preview was left pointing at a destroyed item");
}

TEST_CASE("a refused camera is a permission problem, said once, and does not start",
          "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    FakePermission permission;
    permission.status = Qt::PermissionStatus::Denied;
    std::unique_ptr<CameraBackend> backend = wjs::app::make_camera_backend(permission.seam());
    Counter counter;
    backend->setChanged(counter.callback());

    CHECK_FALSE(backend->start());
    CHECK_FALSE(backend->running());
    CHECK(backend->problem() == CameraProblem::permission);
    CHECK(counter.changes == 1);
    CHECK(permission.requests == 0);  // a refusal is a fact, not asked again
}

TEST_CASE("a camera not yet asked about asks, and waits for the answer", "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    FakePermission permission;
    permission.status = Qt::PermissionStatus::Undetermined;
    std::unique_ptr<CameraBackend> backend = wjs::app::make_camera_backend(permission.seam());
    Counter counter;
    backend->setChanged(counter.callback());

    CHECK(backend->start());  // starting: the question is open
    CHECK(permission.requests == 1);
    CHECK_FALSE(backend->running());
    // While the person is being asked the camera is neither absent nor refused,
    // even on a platform that lists no camera until the answer.
    CHECK(backend->problem() == CameraProblem::none);

    // Starting again does not ask again.
    CHECK(backend->start());
    CHECK(permission.requests == 1);

    REQUIRE(permission.answer);
    const int before = counter.changes;
    permission.answer(Qt::PermissionStatus::Denied);
    CHECK_FALSE(backend->running());
    CHECK(backend->problem() == CameraProblem::permission);
    CHECK(counter.changes == before + 1);
}

TEST_CASE("an answer that comes after the camera was stopped does not start it",
          "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    FakePermission permission;
    permission.status = Qt::PermissionStatus::Undetermined;
    std::unique_ptr<CameraBackend> backend = wjs::app::make_camera_backend(permission.seam());

    REQUIRE(backend->start());
    backend->stop();
    REQUIRE(permission.answer);
    permission.answer(Qt::PermissionStatus::Granted);

    // The screen that wanted the camera was left before the person answered.
    CHECK_FALSE(backend->running());
}

TEST_CASE("the backend with nothing to capture is safe to stop and to read",
          "[app][camera][backend]") {
    wjs::app::test::GuiApplication app;
    FakePermission permission;
    permission.status = Qt::PermissionStatus::Denied;
    std::unique_ptr<CameraBackend> backend = wjs::app::make_camera_backend(permission.seam());

    CHECK_FALSE(backend->running());
    CHECK_FALSE(backend->capture().has_value());
    backend->stop();
    backend->stop();
    (void)backend->devices();
    (void)backend->available();
    backend.reset();
    SUCCEED("destroyed without a camera, a session or a preview in use");
}
