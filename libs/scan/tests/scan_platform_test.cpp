// SPDX-License-Identifier: GPL-3.0-or-later
// The camera and board_reader interfaces: what any implementation of them, on any
// platform, has to keep true. Nothing here names a platform: where a platform has no
// camera (the desktops today) the contract says so with `unavailable`, and when one
// gets a camera these same cases hold it.
//
// The cases come in two kinds. The first are the negative cases, which any platform's
// stub satisfies. The second is the positive contract (platform_contract.hpp): the rules
// of starting, the mailbox, stopping and restarting, and the shape of a reading, run on
// the platform's own implementation as far as the host has a camera, and on a fake that
// keeps the contract (to show that it can be kept) and on fakes that each break one rule
// (to show that each rule is found out). The iPad's implementation is one more call of
// `camera_violations` and `board_reader_violations`.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <thread>

#include "fixtures.hpp"
#include "platform_contract.hpp"
#include "wjs/platform/board_reader.hpp"
#include "wjs/platform/camera.hpp"

namespace {

namespace camera = wjs::platform::camera;
namespace board_reader = wjs::platform::board_reader;
using namespace wjs::scan;
using namespace wjs::scan::synth;

}  // namespace

TEST_CASE("a camera that was never started is not capturing and has no frame", "[scan][platform]") {
    camera::stop();  // safe with nothing running
    CHECK_FALSE(camera::is_capturing());
    CHECK_FALSE(camera::try_pop_latest_frame().has_value());
    camera::stop();  // and twice
    CHECK_FALSE(camera::is_capturing());
}

TEST_CASE("the cameras are listed or the platform says it cannot", "[scan][platform]") {
    const auto devices = camera::enumerate_devices();
    if (devices.has_value()) {
        for (const camera::DeviceInfo& device : *devices) {
            CHECK_FALSE(device.identifier.empty());
        }
    } else {
        CHECK((devices.error() == camera::CameraError::unavailable ||
               devices.error() == camera::CameraError::permission_denied ||
               devices.error() == camera::CameraError::failed));
    }
}

TEST_CASE("a camera that is not listed cannot be started", "[scan][platform]") {
    camera::DeviceInfo unknown;
    unknown.identifier = "no-such-camera";
    unknown.display_name = "No such camera";
    const auto started = camera::start(unknown);
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error() != camera::CameraError::already_running);
    // A failed start leaves nothing running and nothing to take.
    CHECK_FALSE(camera::is_capturing());
    CHECK_FALSE(camera::try_pop_latest_frame().has_value());
    camera::stop();
}

TEST_CASE("sending the person to the settings is always safe to ask", "[scan][platform]") {
    // Nothing to open on a desktop; it must not throw, and it is not a start.
    CHECK_NOTHROW(camera::open_permission_settings());
    CHECK_FALSE(camera::is_capturing());
}

TEST_CASE("each camera error says what is wrong", "[scan][platform]") {
    for (const camera::CameraError error :
         {camera::CameraError::unavailable, camera::CameraError::permission_denied,
          camera::CameraError::no_such_device, camera::CameraError::already_running,
          camera::CameraError::failed}) {
        CHECK_FALSE(camera::to_string(error).empty());
    }
}

TEST_CASE("the board reader has a name and a kind", "[scan][platform]") {
    CHECK_FALSE(board_reader::name().empty());
    const auto kind = board_reader::kind();
    CHECK((kind == board_reader::Kind::portable || kind == board_reader::Kind::native));
}

TEST_CASE("the portable board reader is the library's reader, unchanged", "[scan][platform]") {
    if (board_reader::kind() != board_reader::Kind::portable) {
        SKIP("this platform reads boards with its own recogniser, which has its own tests");
    }
    Spec spec;
    spec.condition = Condition::upright;
    spec.split = Split::tuning;
    spec.size = 4;
    spec.index = 2;
    const Rendered& frame = fixtures::frame(spec);
    const auto& direct = fixtures::reading_of(spec);
    REQUIRE(direct.has_value());

    const auto through = board_reader::read(frame.image.view());
    REQUIRE(through.has_value());
    REQUIRE(through->tiles.size() == direct->tiles.size());
    for (std::size_t i = 0; i < direct->tiles.size(); ++i) {
        CHECK(through->tiles[i].letters == direct->tiles[i].letters);
        CHECK(through->tiles[i].doubtful == direct->tiles[i].doubtful);
    }

    // Its errors are the library's.
    CHECK(board_reader::read(Frame{}).error() == ReadError::invalid_frame);
    const GrayImage blank(320, 240, 100);
    CHECK(board_reader::read(blank.view()).error() == ReadError::board_not_found);
}

TEST_CASE("a frame the camera hands out is a frame the reader can be given", "[scan][platform]") {
    // The mailbox gives an owned frame; its view is what the reader takes.
    OwnedFrame owned;
    owned.width = 4;
    owned.height = 2;
    owned.format = PixelFormat::gray8;
    owned.pixels.assign(8, 90);
    CHECK(owned.view().valid());
    CHECK(owned.view().width == 4);
    CHECK(owned.view().pixels.size() == 8);
    CHECK(board_reader::read(owned.view(), {}).error() == ReadError::board_not_found);
}

namespace {

using namespace wjs::scan::contract;

// The platform's own camera, called through the free functions of the interface. A frame
// is waited on by polling the mailbox, so one that has arrived is kept for the next pop().
class PlatformCamera {
public:
    [[nodiscard]] CameraOps ops() {
        return {[] { return camera::enumerate_devices(); },
                [](const camera::DeviceInfo& device) { return camera::start(device); },
                [] { camera::stop(); },
                [] { return camera::is_capturing(); },
                [this] { return pop(); },
                [this](std::size_t) { return wait_for_a_frame(); },
                {}};
    }

private:
    std::optional<OwnedFrame> pop() {
        if (stash_) {
            return std::exchange(stash_, std::nullopt);
        }
        return camera::try_pop_latest_frame();
    }

    bool wait_for_a_frame() {
        using namespace std::chrono_literals;
        for (int poll = 0; poll < 250 && !stash_; ++poll) {
            stash_ = camera::try_pop_latest_frame();
            if (!stash_) {
                std::this_thread::sleep_for(20ms);
            }
        }
        return stash_.has_value();
    }

    std::optional<OwnedFrame> stash_;
};

std::string joined(const std::vector<std::string>& violations) {
    std::string text;
    for (const std::string& v : violations) {
        text += "\n  " + v;
    }
    return text;
}

// A reader that keeps the contract for the one frame it is given: it answers that board
// exactly, and refuses what it is not given.
ReadFn reading_fn(const synth::Rendered& board, unsigned bugs = 0) {
    return [&board, bugs, calls = std::make_shared<int>(0)](
               const Frame& frame, const ReadOptions&) -> std::expected<BoardReading, ReadError> {
        ++*calls;
        if (!frame.valid()) {
            return std::unexpected(ReadError::invalid_frame);
        }
        const bool is_the_board = frame.width == static_cast<std::size_t>(board.image.width) &&
                                  frame.height == static_cast<std::size_t>(board.image.height);
        if (!is_the_board && (bugs & 1U) == 0U) {
            return std::unexpected(ReadError::board_not_found);  // bug 1: reads anything
        }
        BoardReading reading;
        reading.rows = reading.columns = board.size;
        reading.corners = board.corners;
        reading.size_confidence = (bugs & 2U) != 0U ? 1.5 : 1.0;  // bug 2: not a share
        for (const std::string& letters : board.letters) {
            TileReading tile;
            tile.letters = (bugs & 4U) != 0U ? "a" : letters;  // bug 4: lower case
            tile.confidence = 1.0;
            tile.doubtful = false;
            reading.tiles.push_back(tile);
        }
        if ((bugs & 8U) != 0U) {
            reading.tiles.pop_back();  // bug 8: a tile short
        }
        if ((bugs & 16U) != 0U) {
            reading.tiles.front().letters.clear();  // bug 16: empty and not doubtful
        }
        if ((bugs & 32U) != 0U && *calls % 2 == 0) {
            reading.tiles.front().letters = "Z";  // bug 32: every other read differs
        }
        return reading;
    };
}

}  // namespace

TEST_CASE("the camera contract is kept by a camera that keeps it", "[scan][platform][contract]") {
    FakeCamera fake;
    bool has_device = false;
    const auto violations = camera_violations(fake.ops(), &has_device);
    INFO(joined(violations));
    CHECK(violations.empty());
    CHECK(has_device);  // and the positive half ran
}

TEST_CASE("a platform with no camera keeps the contract by saying so",
          "[scan][platform][contract]") {
    FakeCamera fake(FakeCamera::none, /*has_devices=*/false);
    bool has_device = true;
    const auto violations = camera_violations(fake.ops(), &has_device);
    INFO(joined(violations));
    CHECK(violations.empty());
    CHECK_FALSE(has_device);  // nothing to run: the positive half is not claimed
}

TEST_CASE("each rule of the camera contract is found out when it is broken",
          "[scan][platform][contract]") {
    struct Broken {
        unsigned bug;
        const char* rule;  // a phrase the violation has to contain
    };
    for (const Broken& broken :
         {Broken{FakeCamera::starts_twice, "a second start while running was accepted"},
          Broken{FakeCamera::keeps_the_oldest, "not the latest to arrive"},
          Broken{FakeCamera::stop_leaves_running, "is_capturing is still true"},
          Broken{FakeCamera::frame_survives_stop, "a frame can be taken after the capture stopped"},
          Broken{FakeCamera::accepts_unlisted, "a device that was never listed was started"},
          Broken{FakeCamera::repeats_a_frame, "not newer than the one before it"},
          Broken{FakeCamera::wrong_error, "'already running' was answered with nothing running"}}) {
        FakeCamera fake(broken.bug);
        const auto violations = camera_violations(fake.ops());
        INFO("bug " << broken.bug << ", expecting: " << broken.rule << joined(violations));
        REQUIRE_FALSE(violations.empty());
        bool named = false;
        for (const std::string& v : violations) {
            named = named || v.find(broken.rule) != std::string::npos;
        }
        CHECK(named);
    }
}

TEST_CASE("the mailbox holds the latest frame, once, and only while capturing",
          "[scan][platform][contract]") {
    FakeCamera fake;
    const camera::DeviceInfo front{"front", "Front camera", camera::Facing::front};
    fake.deliver();  // nothing is capturing: the frame is dropped
    REQUIRE(fake.start(front).has_value());
    CHECK_FALSE(fake.pop().has_value());
    fake.deliver();
    fake.deliver();
    fake.deliver();
    const auto latest = fake.pop();
    REQUIRE(latest.has_value());
    CHECK(latest->t_ms == fake.clock());
    CHECK_FALSE(fake.pop().has_value());  // taken: not given twice
    fake.deliver();
    fake.stop();
    CHECK_FALSE(fake.pop().has_value());  // dropped by stop
}

TEST_CASE("the platform's camera keeps the camera contract", "[scan][platform][contract]") {
    PlatformCamera platform;
    bool has_device = false;
    const auto violations = camera_violations(platform.ops(), &has_device);
    INFO(joined(violations));
    CHECK(violations.empty());
    if (!has_device) {
        // The desktops' stub, or a host with no camera or no permission: the rules that
        // hold without a device held, and the ones that need one ran on the fake above.
        INFO(
            "this platform lists no camera to start, so the contract's running half did not "
            "run on it");
    }
    camera::stop();
}

TEST_CASE("the board reader contract is kept by a reader that keeps it",
          "[scan][platform][contract]") {
    const synth::Rendered& board = fixtures::tuning_frame(synth::Condition::upright, 4, 2);
    const auto violations = board_reader_violations(reading_fn(board), board, 16);
    INFO(joined(violations));
    CHECK(violations.empty());
}

TEST_CASE("each rule of the board reader contract is found out when it is broken",
          "[scan][platform][contract]") {
    struct Broken {
        unsigned bug;
        const char* rule;
    };
    const synth::Rendered& board = fixtures::tuning_frame(synth::Condition::upright, 4, 2);
    for (const Broken& broken :
         {Broken{1, "a picture with no board in it was read as a board"},
          Broken{2, "size_confidence is not from 0 to 1"}, Broken{4, "are not 1 to 3 of A to Z"},
          Broken{8, "tiles are not rows * columns of them"},
          Broken{16, "a tile with no letters is not doubtful"},
          Broken{32, "two reads of the same frame disagree"}}) {
        const auto violations = board_reader_violations(reading_fn(board, broken.bug), board);
        INFO("bug " << broken.bug << ", expecting: " << broken.rule << joined(violations));
        REQUIRE_FALSE(violations.empty());
        bool named = false;
        for (const std::string& v : violations) {
            named = named || v.find(broken.rule) != std::string::npos;
        }
        CHECK(named);
    }
}

TEST_CASE("the platform's board reader keeps the board reader contract",
          "[scan][platform][contract]") {
    const synth::Rendered& board = fixtures::tuning_frame(synth::Condition::upright, 4, 2);
    const std::size_t required = board_reader::kind() == board_reader::Kind::portable ? 14 : 0;
    const auto violations = board_reader_violations(
        [](const Frame& frame, const ReadOptions& options) {
            return board_reader::read(frame, options);
        },
        board, required);
    INFO(joined(violations));
    CHECK(violations.empty());
}
