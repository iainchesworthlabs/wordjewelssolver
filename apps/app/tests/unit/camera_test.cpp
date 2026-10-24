// SPDX-License-Identifier: GPL-3.0-or-later
// Mode A's camera, without a camera: the picture conversion, the state a
// CameraSource reports through a backend the test supplies, and the model's use
// of it. The only frames here are made in memory or drawn by the scanner's
// synthetic corpus; nothing opens a device, a file or the network.

#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "camera_source.hpp"
#include "camera_test_frame.hpp"
#include "fake_camera.hpp"
#include "frame_conversion.hpp"
#include "scan_model.hpp"
#include "test_support.hpp"
#include "wjs/scan/frame.hpp"

namespace {

using wjs::app::CameraSource;
using wjs::app::ScanModel;
using wjs::app::test::FakeCameraBackend;
using wjs::scan::PixelFormat;

// A CameraSource over a fake backend, with the backend kept for the test to
// drive. The source owns the backend, as it does the real one.
struct FakeCamera {
    FakeCamera() : backend(std::make_unique<FakeCameraBackend>()) {
        fake = backend.get();
        source = std::make_unique<CameraSource>(std::move(backend));
    }

    std::unique_ptr<FakeCameraBackend> backend;
    FakeCameraBackend* fake = nullptr;
    std::unique_ptr<CameraSource> source;
};

// A small RGB frame with known bytes, for the model's frame path.
wjs::scan::OwnedFrame small_frame() {
    wjs::scan::OwnedFrame frame;
    frame.width = 2;
    frame.height = 2;
    frame.stride = 6;
    frame.format = PixelFormat::rgb8;
    frame.pixels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    return frame;
}

// A QCoreApplication is needed by the model's frame timer and by the reader's
// answer. It is made inside the case, so the cases that need neither make none.
using TestApplication = wjs::app::test::CoreApplication;

}  // namespace

TEST_CASE("an image becomes a packed RGB24 frame, row by row", "[app][scan][camera]") {
    QImage image(3, 2, QImage::Format_RGB888);
    REQUIRE_FALSE(image.isNull());
    image.setPixelColor(0, 0, QColor(10, 20, 30));
    image.setPixelColor(1, 0, QColor(40, 50, 60));
    image.setPixelColor(2, 0, QColor(70, 80, 90));
    image.setPixelColor(0, 1, QColor(100, 110, 120));
    image.setPixelColor(1, 1, QColor(130, 140, 150));
    image.setPixelColor(2, 1, QColor(160, 170, 180));

    const std::optional<wjs::scan::OwnedFrame> frame = wjs::app::frame_from_image(image);
    REQUIRE(frame.has_value());
    CHECK(frame->width == 3);
    CHECK(frame->height == 2);
    CHECK(frame->format == PixelFormat::rgb8);
    // Three bytes a pixel, packed: QImage's row padding (which makes the second
    // row start at a multiple of four) is not copied into the frame.
    CHECK(frame->stride == 9);
    CHECK(frame->view().row_bytes() == 9);
    REQUIRE(frame->pixels.size() == 18);

    CHECK(frame->pixels[0] == 10);
    CHECK(frame->pixels[1] == 20);
    CHECK(frame->pixels[2] == 30);
    CHECK(frame->pixels[3] == 40);
    CHECK(frame->pixels[4] == 50);
    CHECK(frame->pixels[5] == 60);
    CHECK(frame->pixels[6] == 70);
    CHECK(frame->pixels[7] == 80);
    CHECK(frame->pixels[8] == 90);
    // The second row begins at byte 9, not at the QImage row's byte offset.
    CHECK(frame->pixels[9] == 100);
    CHECK(frame->pixels[10] == 110);
    CHECK(frame->pixels[11] == 120);
    CHECK(frame->pixels[15] == 160);
    CHECK(frame->pixels[16] == 170);
    CHECK(frame->pixels[17] == 180);

    // The scanner can read it as it is.
    CHECK(frame->view().valid());
}

TEST_CASE("a grey image becomes an RGB frame with equal channels", "[app][scan][camera]") {
    QImage image(1, 1, QImage::Format_Grayscale8);
    // fill() sets the grey index itself; setPixel() would take a QRgb and
    // convert it through qGray, which is not the same value.
    image.fill(128);

    const std::optional<wjs::scan::OwnedFrame> frame = wjs::app::frame_from_image(image);
    REQUIRE(frame.has_value());
    CHECK(frame->width == 1);
    CHECK(frame->height == 1);
    CHECK(frame->format == PixelFormat::rgb8);
    CHECK(frame->stride == 3);
    REQUIRE(frame->pixels.size() == 3);
    CHECK(frame->pixels[0] == 128);
    CHECK(frame->pixels[1] == 128);
    CHECK(frame->pixels[2] == 128);
}

TEST_CASE("an ARGB32 image, the live camera's format, drops the alpha and keeps the colours",
          "[app][scan][camera]") {
    QImage image(2, 1, QImage::Format_ARGB32);
    REQUIRE_FALSE(image.isNull());
    image.setPixel(0, 0, qRgba(10, 20, 30, 128));
    image.setPixel(1, 0, qRgba(40, 50, 60, 0));

    const std::optional<wjs::scan::OwnedFrame> frame = wjs::app::frame_from_image(image);
    REQUIRE(frame.has_value());
    CHECK(frame->width == 2);
    CHECK(frame->height == 1);
    CHECK(frame->format == PixelFormat::rgb8);
    REQUIRE(frame->pixels.size() == 6);
    // Red, green, blue in that order: a swapped pair is a different picture.
    CHECK(frame->pixels[0] == 10);
    CHECK(frame->pixels[1] == 20);
    CHECK(frame->pixels[2] == 30);
    CHECK(frame->pixels[3] == 40);
    CHECK(frame->pixels[4] == 50);
    CHECK(frame->pixels[5] == 60);
}

TEST_CASE("an RGB32 and a premultiplied image become the same picture", "[app][scan][camera]") {
    QImage rgb32(1, 1, QImage::Format_RGB32);
    REQUIRE_FALSE(rgb32.isNull());
    rgb32.setPixel(0, 0, qRgb(70, 80, 90));
    const std::optional<wjs::scan::OwnedFrame> from_rgb32 = wjs::app::frame_from_image(rgb32);
    REQUIRE(from_rgb32.has_value());
    REQUIRE(from_rgb32->pixels.size() == 3);
    CHECK(from_rgb32->pixels[0] == 70);
    CHECK(from_rgb32->pixels[1] == 80);
    CHECK(from_rgb32->pixels[2] == 90);

    // Opaque, so premultiplying changes nothing: the colours must come out as
    // they went in, through the generic conversion.
    QImage premultiplied(1, 1, QImage::Format_ARGB32_Premultiplied);
    REQUIRE_FALSE(premultiplied.isNull());
    premultiplied.setPixel(0, 0, qRgba(70, 80, 90, 255));
    const std::optional<wjs::scan::OwnedFrame> from_premultiplied =
        wjs::app::frame_from_image(premultiplied);
    REQUIRE(from_premultiplied.has_value());
    REQUIRE(from_premultiplied->pixels.size() == 3);
    CHECK(from_premultiplied->pixels[0] == 70);
    CHECK(from_premultiplied->pixels[1] == 80);
    CHECK(from_premultiplied->pixels[2] == 90);
}

TEST_CASE("a null image is not a frame", "[app][scan][camera]") {
    const QImage none;
    CHECK_FALSE(wjs::app::frame_from_image(none).has_value());
}

TEST_CASE("a camera source with no device says no_camera and hands over nothing",
          "[app][scan][camera]") {
    FakeCamera camera;
    CHECK_FALSE(camera.source->available());
    CHECK(camera.source->devices().isEmpty());
    CHECK(camera.source->problem() == "no_camera");
    CHECK_FALSE(camera.source->start());
    CHECK_FALSE(camera.source->running());
    CHECK(camera.source->problem() == "no_camera");
    CHECK_FALSE(camera.source->capture());
    CHECK_FALSE(camera.source->takeFrame().has_value());

    // A null preview is not a hand-over: the backend is told, and counts none.
    camera.source->attachPreview(nullptr);
    CHECK(camera.fake->previews() == 0);
    CHECK(camera.fake->preview() == nullptr);
}

TEST_CASE("the preview item is handed to the backend and taken back", "[app][scan][camera]") {
    FakeCamera camera;
    QObject item;

    camera.source->attachPreview(&item);
    CHECK(camera.fake->previews() == 1);
    CHECK(camera.fake->preview() == &item);

    camera.source->detachPreview();
    CHECK(camera.fake->detaches() == 1);
    CHECK(camera.fake->preview() == nullptr);
}

TEST_CASE("a source with no backend answers no camera and does nothing", "[app][scan][camera]") {
    CameraSource source{std::unique_ptr<wjs::app::CameraBackend>{}};
    QObject item;

    CHECK_FALSE(source.available());
    CHECK(source.devices().isEmpty());
    CHECK(source.problem() == "no_camera");
    CHECK(source.problemKind() == wjs::app::CameraProblem::no_camera);
    CHECK_FALSE(source.running());
    CHECK_FALSE(source.start());
    CHECK_FALSE(source.capture());
    CHECK_FALSE(source.takeFrame().has_value());
    source.attachPreview(&item);
    source.detachPreview();
    source.stop();
}

TEST_CASE("a capture says so, and starting and stopping tell QML", "[app][scan][camera]") {
    FakeCamera camera;
    camera.fake->setAvailable(true);
    camera.fake->setProblem(wjs::app::CameraProblem::none);
    camera.fake->setFrame(small_frame());

    int changed = 0;
    int captured = 0;
    QObject::connect(camera.source.get(), &CameraSource::changed, camera.source.get(),
                     [&changed] { ++changed; });
    QObject::connect(camera.source.get(), &CameraSource::captured, camera.source.get(),
                     [&captured] { ++captured; });

    REQUIRE(camera.source->start());
    CHECK(changed == 1);
    CHECK(captured == 0);

    // A frame the model did not take is dropped by the next start: one frame at a
    // time, and none carried over a restart.
    REQUIRE(camera.source->capture());
    CHECK(captured == 1);
    camera.source->stop();
    CHECK(changed == 2);
    CHECK_FALSE(camera.source->takeFrame().has_value());
}

TEST_CASE("a refused camera reports permission and does not run", "[app][scan][camera]") {
    FakeCamera camera;
    camera.fake->setAvailable(true);
    camera.fake->setProblem(wjs::app::CameraProblem::permission);

    CHECK(camera.source->available());
    CHECK(camera.source->devices().size() == 1);
    CHECK_FALSE(camera.source->start());
    CHECK_FALSE(camera.source->running());
    CHECK(camera.source->problem() == "permission");
}

TEST_CASE("a running camera hands its frame over once and a failure is reported",
          "[app][scan][camera]") {
    FakeCamera camera;
    camera.fake->setAvailable(true);
    camera.fake->setProblem(wjs::app::CameraProblem::none);
    camera.fake->setFrame(small_frame());

    int changed = 0;
    QObject::connect(camera.source.get(), &CameraSource::changed, camera.source.get(),
                     [&changed] { ++changed; });

    CHECK(camera.source->start());
    CHECK(camera.source->running());
    CHECK(camera.source->problem().isEmpty());

    CHECK(camera.source->capture());
    std::optional<wjs::scan::OwnedFrame> frame = camera.source->takeFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->width == 2);
    CHECK(frame->height == 2);
    CHECK(frame->pixels.size() == 12);
    // The mailbox holds one frame: taken once, gone.
    CHECK_FALSE(camera.source->takeFrame().has_value());
    CHECK_FALSE(camera.source->capture());

    camera.fake->fail(wjs::app::CameraProblem::failed);
    CHECK_FALSE(camera.source->running());
    CHECK(camera.source->problem() == "failed");
    CHECK(changed > 0);

    camera.source->stop();
    CHECK_FALSE(camera.source->running());
}

TEST_CASE("without a usable camera the model offers the typed board", "[app][scan][camera]") {
    FakeCamera camera;
    ScanModel model;
    model.setCameraSource(camera.source.get());

    model.open();
    CHECK_FALSE(model.cameraAvailable());
    CHECK_FALSE(model.cameraCapturing());
    CHECK(model.cameraProblem() == "no_camera");

    model.capture();
    CHECK(model.state() == "ready");
    CHECK(model.readProblem() == "no_camera");
    // Nothing was captured and nothing was faked: the source has no frame.
    CHECK_FALSE(camera.source->takeFrame().has_value());
}

TEST_CASE("a frame from the camera source opens the check screen", "[app][scan][camera]") {
    TestApplication application;
    FakeCamera camera;
    camera.fake->setAvailable(true);
    camera.fake->setProblem(wjs::app::CameraProblem::none);
    camera.fake->setFrame(wjs::app::test::synthetic_board_frame());

    ScanModel model;
    model.setCameraSource(camera.source.get());
    model.open();
    REQUIRE(model.cameraAvailable());
    REQUIRE(model.cameraCapturing());

    model.capture();
    // The read is on the scanner's thread; the answer arrives through the loop.
    REQUIRE(wjs::app::test::spin_until([&model] { return model.state() != "reading"; }));
    CHECK(model.state() == "check");
    CHECK(model.rows() == 4);
    CHECK(model.columns() == 4);
    CHECK(model.letters().size() == 16);
    // The camera stops as soon as the frame is in hand, and the frame is read
    // and gone: only the reading's letters and doubts remain.
    CHECK_FALSE(model.cameraCapturing());
    CHECK_FALSE(camera.source->running());
    CHECK_FALSE(camera.source->takeFrame().has_value());
}

TEST_CASE("a camera that never delivers a frame returns the screen to ready",
          "[app][scan][camera]") {
    TestApplication application;
    FakeCamera camera;
    camera.fake->setAvailable(true);
    camera.fake->setProblem(wjs::app::CameraProblem::none);

    ScanModel model;
    // Three looks, five milliseconds apart: the screen waits four seconds, which a
    // case need not spend to know what happens when the wait is out.
    model.setFramePatience(5, 3);
    model.setCameraSource(camera.source.get());
    model.open();
    REQUIRE(model.cameraAvailable());

    model.capture();
    REQUIRE(model.state() == "reading");

    // The read problem is reported when the wait is over, and the screen is ready
    // again with the camera on.
    QEventLoop loop;
    QObject::connect(&model, &ScanModel::readProblemChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();

    CHECK(model.state() == "ready");
    CHECK(model.readProblem() == "no_frame");
    CHECK(model.cameraCapturing());
    CHECK(camera.source->running());
}

TEST_CASE("a camera that fails while the ready screen waits is described", "[app][scan][camera]") {
    FakeCamera camera;
    camera.fake->setAvailable(true);
    camera.fake->setProblem(wjs::app::CameraProblem::none);

    ScanModel model;
    model.setCameraSource(camera.source.get());
    model.open();
    REQUIRE(model.cameraAvailable());

    int camera_changes = 0;
    QObject::connect(&model, &ScanModel::cameraChanged, &model,
                     [&camera_changes] { ++camera_changes; });

    camera.fake->fail(wjs::app::CameraProblem::failed);
    CHECK_FALSE(model.cameraCapturing());
    CHECK_FALSE(model.cameraAvailable());
    CHECK(model.cameraProblem() == "failed");
    CHECK(camera_changes > 0);
}

TEST_CASE("a camera source destroyed while a capture waits returns the screen to ready",
          "[app][scan][camera]") {
    TestApplication application;
    ScanModel model;
    model.setFramePatience(5, 1000);
    {
        FakeCamera camera;
        camera.fake->setAvailable(true);
        camera.fake->setProblem(wjs::app::CameraProblem::none);
        model.setCameraSource(camera.source.get());
        model.open();
        REQUIRE(model.cameraCapturing());
        model.capture();
        REQUIRE(model.state() == "reading");
    }
    // The poll was stopped with the camera that fed it: the screen is not left
    // waiting on a source that is gone.
    CHECK(model.state() == "ready");
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();
    CHECK(model.state() == "ready");
    CHECK(model.readProblem().isEmpty());
}

TEST_CASE("a camera source that is destroyed leaves the model with none", "[app][scan][camera]") {
    ScanModel model;
    {
        FakeCamera camera;
        camera.fake->setAvailable(true);
        camera.fake->setProblem(wjs::app::CameraProblem::none);
        model.setCameraSource(camera.source.get());
        model.open();
        REQUIRE(model.cameraAvailable());
    }
    // The model does not own the source; a source that goes first is not a
    // pointer QML can still ask for.
    CHECK(model.cameraSource() == nullptr);
    CHECK_FALSE(model.cameraAvailable());
    CHECK_FALSE(model.cameraCapturing());
}
