// SPDX-License-Identifier: GPL-3.0-or-later
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <utility>

#include "camera_source.hpp"
#include "camera_test_frame.hpp"
#include "fake_camera.hpp"
#include "scan_model.hpp"
#include "test_support.hpp"
#include "wjs/scan/frame.hpp"
#include "wjs/scan/reading.hpp"

// The check screen's model (apps/app/src/scan/scan_model.*): the draft a grown-up
// corrects, the reader's doubts and alternatives, the board size, the typed-board
// fallback, where the draft came from, the camera retry, and the confirmation
// that is the only way to a Board. The camera source's own cases are in
// camera_test.cpp; the model is given none or a fake backend here, so the
// platform interface's path (unavailable on the three desktops) and the source's
// path are both driven without a device.

namespace {

using wjs::app::ScanModel;

QStringList letters_of(const ScanModel& model) {
    return model.letters();
}

QVariantList indices(std::initializer_list<int> values) {
    QVariantList list;
    for (const int value : values) {
        list.append(value);
    }
    return list;
}

void fill(ScanModel& model, const QString& letter) {
    for (int index = 0; index < model.rows() * model.columns(); ++index) {
        REQUIRE(model.setTile(index, letter));
    }
}

wjs::scan::BoardReading two_by_two() {
    wjs::scan::BoardReading reading;
    reading.rows = 2;
    reading.columns = 2;
    reading.tiles.resize(4);
    reading.tiles[0] = {};
    reading.tiles[0].letters = "C";
    reading.tiles[0].confidence = 0.9;
    reading.tiles[0].doubtful = false;
    reading.tiles[1] = {};
    reading.tiles[1].letters = "A";
    reading.tiles[1].confidence = 0.4;
    reading.tiles[1].doubtful = true;
    reading.tiles[1].alternatives[0].letters = "O";
    reading.tiles[1].alternatives[0].score = 0.4;
    reading.tiles[2] = {};  // the reader saw nothing here
    reading.tiles[2].doubtful = true;
    reading.tiles[3] = {};
    reading.tiles[3].letters = "T";
    reading.tiles[3].confidence = 0.5;
    reading.tiles[3].doubtful = true;
    return reading;
}

}  // namespace

TEST_CASE("the typed board is an empty 4x4 the grown-up fills", "[app][scan]") {
    ScanModel model;
    CHECK(model.state() == "ready");
    CHECK(model.rows() == 0);
    CHECK(model.columns() == 0);

    model.typeBoard();

    CHECK(model.state() == "check");
    CHECK(model.rows() == 4);
    CHECK(model.columns() == 4);
    CHECK(model.size() == 4);
    CHECK(model.sizes().size() == 4);
    CHECK(model.letters().size() == 16);
    CHECK_FALSE(model.complete());
    CHECK_FALSE(model.draftFromCamera());
    CHECK(model.doubtful().isEmpty());
    for (const QString& letter : model.letters()) {
        CHECK(letter.isEmpty());
    }
    // The first tile is empty and was typed into, not read.
    const QVariantMap first = model.tiles().at(0).toMap();
    CHECK(first.value("letters").toString().isEmpty());
    CHECK(first.value("origin").toString() == "empty");
    CHECK_FALSE(first.value("doubtful").toBool());

    // A correction: one to three letters, upper-cased, and the tile settled.
    CHECK(model.setTile(0, "c"));
    CHECK(model.letters().at(0) == "C");
    CHECK(model.tiles().at(0).toMap().value("origin").toString() == "typed");
    CHECK_FALSE(model.complete());

    CHECK(model.setTile(0, "qu"));
    CHECK(model.letters().at(0) == "QU");
    CHECK(model.setTile(0, "abc"));
    CHECK(model.letters().at(0) == "ABC");

    // Not a letter, too many letters, or off the grid: nothing changes.
    CHECK_FALSE(model.setTile(0, "1"));
    CHECK_FALSE(model.setTile(0, "ABCD"));
    CHECK_FALSE(model.setTile(0, ""));
    CHECK_FALSE(model.setTile(-1, "A"));
    CHECK_FALSE(model.setTile(16, "A"));
    CHECK(model.letters().at(0) == "ABC");

    CHECK(model.clearTile(0));
    CHECK(model.letters().at(0).isEmpty());
    CHECK_FALSE(model.acceptTile(0));  // an empty tile cannot be accepted
}

TEST_CASE("a full grid confirms and an incomplete one does not", "[app][scan]") {
    ScanModel model;
    model.typeBoard();
    fill(model, "A");

    CHECK(model.complete());
    CHECK(model.confirm());
    REQUIRE(model.confirmedBoard().has_value());
    CHECK(model.confirmedBoard()->rows() == 4);
    CHECK(model.confirmedBoard()->columns() == 4);
    CHECK(model.confirmedBoard()->tile({0, 0}) == "A");
    CHECK(model.confirmedBoard()->tile({3, 3}) == "A");

    // One tile missing: no board. The confirmed one before is no longer the
    // draft's either.
    CHECK(model.clearTile(15));
    CHECK_FALSE(model.complete());
    CHECK_FALSE(model.confirm());
    CHECK_FALSE(model.confirmedBoard().has_value());

    // The size can be changed, and what overlaps is kept from the top left.
    CHECK(model.resize(3, 3));
    CHECK(model.rows() == 3);
    CHECK(model.columns() == 3);
    CHECK(model.size() == 3);
    CHECK(model.letters().at(0) == "A");
    CHECK(model.letters().size() == 9);

    // A quarter turn keeps the letters and the doubts; only their places move.
    CHECK(model.setTile(0, "Q"));
    model.turn();
    CHECK(model.rows() == 3);
    CHECK(model.letters().at(0) == "A");
    CHECK(model.letters().at(2) == "Q");
}

TEST_CASE("the reader's doubts are the tiles to ring, with its other thoughts", "[app][scan]") {
    ScanModel model;
    CHECK(model.useReading(two_by_two()));

    CHECK(model.state() == "check");
    CHECK(model.rows() == 2);
    CHECK(model.columns() == 2);
    CHECK(model.draftFromCamera());
    CHECK(letters_of(model) == QStringList({"C", "A", "", "T"}));
    CHECK_FALSE(model.complete());

    // A tile the reader saw nothing in is empty and doubtful; a read tile keeps
    // the reader's doubt.
    CHECK(model.doubtful() == indices({1, 2, 3}));
    CHECK(model.tiles().at(1).toMap().value("origin").toString() == "read");
    CHECK(model.tiles().at(2).toMap().value("origin").toString() == "empty");
    CHECK(model.tiles().at(2).toMap().value("doubtful").toBool());
    CHECK(model.alternatives(1) == QStringList({"O"}));
    CHECK(model.alternatives(0).isEmpty());

    // "This one is right" settles a doubt; an empty tile cannot be accepted.
    CHECK(model.acceptTile(1));
    CHECK(model.doubtful() == indices({2, 3}));
    CHECK_FALSE(model.acceptTile(2));

    // A correction keeps the draft's origin: it is still the board the camera
    // read, merely corrected.
    CHECK(model.setTile(2, "R"));
    CHECK(model.letters().at(2) == "R");
    CHECK(model.draftFromCamera());
    CHECK(model.doubtful() == indices({3}));

    // "Yes, play" does not refuse a board with a ringed tile left: the press is
    // the grown-up saying they looked (docs/design/scanner.md, "Confirm and
    // correct").
    CHECK(model.complete());
    CHECK(model.confirm());
    REQUIRE(model.confirmedBoard().has_value());
    CHECK(model.confirmedBoard()->tile({1, 0}) == "R");
    CHECK(model.confirmedBoard()->tile({0, 0}) == "C");
}

TEST_CASE("a reading that is not a grid is refused", "[app][scan]") {
    ScanModel model;
    wjs::scan::BoardReading reading;
    reading.rows = 0;
    CHECK_FALSE(model.useReading(reading));
    CHECK(model.state() == "ready");

    reading.rows = 2;
    reading.columns = 2;
    reading.tiles.resize(3);  // not rows * columns
    CHECK_FALSE(model.useReading(reading));
    CHECK(model.state() == "ready");

    // Larger than a board can be, though every tile is there.
    reading.rows = 17;
    reading.columns = 17;
    reading.tiles.resize(17 * 17);
    CHECK_FALSE(model.useReading(reading));
    CHECK(model.state() == "ready");
    CHECK(model.rows() == 0);
}

TEST_CASE("without a camera the typed board is the way on, and nothing is faked",
          "[app][scan][camera]") {
    ScanModel model;
    model.open();
    // The three desktops' camera interface answers `unavailable`
    // (libs/platforms/detail/camera_unavailable.cpp): no capture is pretended.
    CHECK_FALSE(model.cameraAvailable());
    CHECK_FALSE(model.cameraCapturing());
    CHECK(model.cameraProblem() == "unavailable");

    model.capture();
    CHECK(model.state() == "ready");
    CHECK(model.readProblem() == "no_camera");

    // Read again from the check screen goes back to ready with the camera on
    // again (or, here, unavailable again), and the draft is gone.
    model.typeBoard();
    CHECK(model.state() == "check");
    CHECK_FALSE(model.draftFromCamera());
    model.readAgain();
    CHECK(model.state() == "ready");
    CHECK(model.rows() == 0);
    CHECK_FALSE(model.draftFromCamera());
    CHECK_FALSE(model.confirmedBoard().has_value());
    CHECK(model.cameraProblem() == "unavailable");
}

TEST_CASE("Try camera again is a new attempt, and a camera that appears is found",
          "[app][scan][camera]") {
    // The platform path: no camera source, so the model enumerates the platform's
    // devices. There is none here, and the retry says the same truth again.
    {
        ScanModel model;
        model.open();
        CHECK(model.cameraProblem() == "unavailable");
        model.capture();
        CHECK(model.readProblem() == "no_camera");

        model.retryCamera();
        CHECK(model.state() == "ready");
        CHECK(model.cameraProblem() == "unavailable");
        // The last read's sentence is about the attempt that failed, not this one.
        CHECK(model.readProblem().isEmpty());
        CHECK_FALSE(model.cameraAvailable());
    }

    // The supplied-source path: a camera that was not available when the screen
    // opened is there when the person tries again. The backend is a fake, so no
    // device is opened; the model's retry starts it exactly as open() would.
    auto backend = std::make_unique<wjs::app::test::FakeCameraBackend>();
    auto* camera = backend.get();
    camera->setAvailable(false);
    camera->setProblem(wjs::app::CameraProblem::no_camera);
    wjs::app::CameraSource source(std::move(backend));

    ScanModel model;
    model.setCameraSource(&source);
    model.open();
    CHECK_FALSE(model.cameraAvailable());
    CHECK(model.cameraProblem() == "no_camera");
    CHECK_FALSE(model.cameraCapturing());

    // The camera becomes ready (uncovered, plugged in, freed by another app).
    camera->setAvailable(true);
    camera->setProblem(wjs::app::CameraProblem::none);
    model.retryCamera();

    CHECK(model.state() == "ready");
    CHECK(model.cameraAvailable());
    CHECK(model.cameraCapturing());
    CHECK(model.cameraProblem().isEmpty());
}

// --- when the frame cannot be read ---------------------------------------------------

namespace {

// A model over a running fake camera that will give `frame`, with a short wait so
// that the case does not spend the screen's four seconds.
struct Rig {
    explicit Rig(wjs::scan::OwnedFrame frame) {
        auto backend = std::make_unique<wjs::app::test::FakeCameraBackend>();
        camera = backend.get();
        camera->setAvailable(true);
        camera->setProblem(wjs::app::CameraProblem::none);
        camera->setFrame(std::move(frame));
        source = std::make_unique<wjs::app::CameraSource>(std::move(backend));
        model.setFramePatience(5, 20);
        model.setCameraSource(source.get());
        model.open();
        REQUIRE(model.cameraCapturing());
    }

    // Captures and waits for the read to be over, whichever way it went.
    void capture() {
        model.capture();
        REQUIRE(wjs::app::test::spin_until([this] { return model.state() != "reading"; }));
    }

    wjs::app::test::CoreApplication application;
    wjs::app::test::FakeCameraBackend* camera = nullptr;
    std::unique_ptr<wjs::app::CameraSource> source;
    ScanModel model;
};

[[nodiscard]] wjs::scan::OwnedFrame blank_frame(std::size_t side) {
    wjs::scan::OwnedFrame frame;
    frame.width = side;
    frame.height = side;
    frame.stride = side;
    frame.format = wjs::scan::PixelFormat::gray8;
    frame.pixels.assign(side * side, 200);
    return frame;
}

}  // namespace

TEST_CASE("a frame with no board in it returns the screen to ready with the camera on",
          "[app][scan][camera]") {
    Rig rig(blank_frame(64));
    int read_problems = 0;
    QObject::connect(&rig.model, &ScanModel::readProblemChanged, &rig.model,
                     [&read_problems] { ++read_problems; });

    rig.capture();

    CHECK(rig.model.state() == "ready");
    CHECK(rig.model.readProblem() == "board_not_found");
    CHECK(read_problems >= 1);
    CHECK(rig.model.rows() == 0);
    // Another go is on offer at once: the camera is running again.
    CHECK(rig.model.cameraCapturing());
    CHECK(rig.source->running());
    CHECK_FALSE(rig.model.confirmedBoard().has_value());
}

TEST_CASE("a frame the scanner cannot take is an invalid frame", "[app][scan][camera]") {
    wjs::scan::OwnedFrame empty;  // no size, no pixels
    Rig rig(std::move(empty));

    rig.capture();

    CHECK(rig.model.state() == "ready");
    CHECK(rig.model.readProblem() == "invalid_frame");
    CHECK(rig.model.readProblemKind() == wjs::app::ReadProblem::invalid_frame);
}

TEST_CASE("the next capture clears the last read's problem", "[app][scan][camera]") {
    Rig rig(blank_frame(64));
    rig.capture();
    REQUIRE(rig.model.readProblem() == "board_not_found");

    // A second frame, the synthetic board: the problem is cleared by the capture
    // and the check screen opens.
    rig.camera->setFrame(wjs::app::test::synthetic_board_frame());
    rig.model.capture();
    CHECK(rig.model.readProblem().isEmpty());
    REQUIRE(wjs::app::test::spin_until([&rig] { return rig.model.state() != "reading"; }));
    CHECK(rig.model.state() == "check");
    CHECK(rig.model.readProblemKind() == wjs::app::ReadProblem::none);
}

TEST_CASE("leaving the screen while a frame is being read drops the read", "[app][scan][camera]") {
    Rig rig(wjs::app::test::synthetic_board_frame());
    rig.model.capture();
    REQUIRE(rig.model.state() == "reading");

    rig.model.close();

    // The screen is idle and ready; the read's answer, when it arrives, finds no
    // one waiting for it.
    CHECK(rig.model.state() == "ready");
    CHECK_FALSE(rig.model.cameraCapturing());
    wjs::app::test::pause(150);
    CHECK(rig.model.state() == "ready");
    CHECK(rig.model.rows() == 0);
}

TEST_CASE("typing a board while a frame is being read keeps the typed board",
          "[app][scan][camera]") {
    Rig rig(wjs::app::test::synthetic_board_frame());
    rig.model.capture();
    REQUIRE(rig.model.state() == "reading");

    rig.model.typeBoard();
    CHECK(rig.model.state() == "check");
    CHECK_FALSE(rig.model.draftFromCamera());

    wjs::app::test::pause(150);
    // The late reading did not replace what the grown-up chose.
    CHECK(rig.model.state() == "check");
    CHECK_FALSE(rig.model.draftFromCamera());
    CHECK(rig.model.rows() == 4);
}

// --- the edges of the check screen ------------------------------------------------------

TEST_CASE("an index off the grid is refused by every correction", "[app][scan]") {
    ScanModel model;
    // No draft: nothing to correct.
    CHECK_FALSE(model.setTile(0, "A"));
    CHECK_FALSE(model.acceptTile(0));
    CHECK_FALSE(model.clearTile(0));
    CHECK(model.alternatives(0).isEmpty());
    CHECK_FALSE(model.resize(3, 3));
    CHECK_FALSE(model.confirm());
    model.turn();  // nothing to turn
    CHECK(model.rows() == 0);

    model.typeBoard();
    for (const int index : {-1, 16, 1000}) {
        CHECK_FALSE(model.setTile(index, "A"));
        CHECK_FALSE(model.acceptTile(index));
        CHECK_FALSE(model.clearTile(index));
        CHECK(model.alternatives(index).isEmpty());
    }
}

TEST_CASE("a size that is not a board is refused and the grid is kept", "[app][scan]") {
    ScanModel model;
    model.typeBoard();
    fill(model, "A");

    CHECK_FALSE(model.resize(0, 3));
    CHECK_FALSE(model.resize(3, 0));
    CHECK_FALSE(model.resize(-3, 3));
    CHECK_FALSE(model.resize(3, -3));
    CHECK_FALSE(model.resize(1000000, 1000000));
    CHECK(model.rows() == 4);
    CHECK(model.columns() == 4);
    CHECK(model.letters().size() == 16);
}

TEST_CASE("the camera is opened and closed only when it should be", "[app][scan][camera]") {
    ScanModel model;
    int camera_changes = 0;
    QObject::connect(&model, &ScanModel::cameraChanged, [&camera_changes] { ++camera_changes; });

    // Not in `ready`: open() and retryCamera() do nothing at all.
    model.typeBoard();
    REQUIRE(model.state() == "check");
    camera_changes = 0;
    model.open();
    model.retryCamera();
    CHECK(camera_changes == 0);
    CHECK(model.state() == "check");

    // close() is safe again and again, and with nothing running.
    model.close();
    model.close();
    CHECK(model.state() == "check");
}

TEST_CASE("replacing the camera source while it is capturing stops the old one",
          "[app][scan][camera]") {
    auto first_backend = std::make_unique<wjs::app::test::FakeCameraBackend>();
    first_backend->setAvailable(true);
    first_backend->setProblem(wjs::app::CameraProblem::none);
    wjs::app::CameraSource first(std::move(first_backend));
    auto second_backend = std::make_unique<wjs::app::test::FakeCameraBackend>();
    wjs::app::CameraSource second(std::move(second_backend));

    ScanModel model;
    model.setCameraSource(&first);
    model.open();
    REQUIRE(first.running());

    model.setCameraSource(&second);

    CHECK_FALSE(first.running());
    CHECK(model.cameraSource() == &second);
    CHECK_FALSE(model.cameraCapturing());
}
