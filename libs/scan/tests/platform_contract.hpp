// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What any implementation of the camera and board_reader interfaces has to keep true, as
// functions that return the ways it does not. A platform's implementation is run through
// them in scan_platform_test.cpp, and the iPad's, when it is written, is one more call:
// its AVFoundation camera and Vision reader are held to the same contract as the desktops'
// stubs and portable reader, which is what docs/design/scanner.md promises of these cases.
//
// They return violations and do not assert, so that a test can also show that a contract
// is not vacuous: that a camera which breaks a rule is found to (the fakes below do).
// Each violation is a sentence naming the rule.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "evaluate.hpp"
#include "scene.hpp"
#include "wjs/platform/board_reader.hpp"
#include "wjs/platform/camera.hpp"
#include "wjs/scan/confirmation.hpp"
#include "wjs/scan/frame.hpp"
#include "wjs/scan/reader.hpp"

namespace wjs::scan::contract {

namespace cam = wjs::platform::camera;

// --- the camera -----------------------------------------------------------------------

// The camera interface as a set of calls, so that the platform's free functions and a
// fake can be held to the one contract.
struct CameraOps {
    std::function<std::expected<std::vector<cam::DeviceInfo>, cam::CameraError>()> enumerate;
    std::function<std::expected<void, cam::CameraError>(const cam::DeviceInfo&)> start;
    std::function<void()> stop;
    std::function<bool()> is_capturing;
    std::function<std::optional<OwnedFrame>()> pop;
    // Lets frames arrive while a capture runs. A fake delivers `count` of them at once; a
    // device is waited on until one has arrived (false if none does in time).
    std::function<bool(std::size_t count)> frames_arrive;
    // The time of the newest frame that has arrived, if the implementation can say (a fake
    // can; a device cannot be asked). With it, the mailbox is held to latest-wins.
    std::function<std::int64_t()> newest_arrived;
};

[[nodiscard]] inline std::string describe_error(cam::CameraError error) {
    return std::string(cam::to_string(error));
}

// The rules of wjs/platform/camera.hpp that hold whether or not there is a camera, and,
// when the platform lists one, those that hold while it runs. `has_device` says which it
// was: false for a platform that answers `unavailable`, which has only the first.
[[nodiscard]] inline std::vector<std::string> camera_violations(const CameraOps& camera,
                                                                bool* has_device = nullptr) {
    std::vector<std::string> bad;
    if (has_device != nullptr) {
        *has_device = false;
    }

    // Idle: nothing running, nothing to take, and stopping is safe, twice.
    camera.stop();
    camera.stop();
    if (camera.is_capturing()) {
        bad.emplace_back("idle: is_capturing is true with nothing started");
    }
    if (camera.pop().has_value()) {
        bad.emplace_back("idle: a frame is waiting although nothing was started");
    }

    const auto listed = camera.enumerate();
    if (!listed) {
        const cam::CameraError error = listed.error();
        if (error == cam::CameraError::no_such_device ||
            error == cam::CameraError::already_running) {
            bad.push_back("enumerate: answered '" + describe_error(error) +
                          "', which is about starting and not about listing");
        }
    } else {
        std::set<std::string> seen;
        for (const cam::DeviceInfo& device : *listed) {
            if (device.identifier.empty()) {
                bad.emplace_back("enumerate: a device has no identifier");
            } else if (!seen.insert(device.identifier).second) {
                bad.push_back("enumerate: identifier '" + device.identifier + "' is listed twice");
            }
        }
    }

    // A device that is not listed cannot be started, and nothing is left running.
    cam::DeviceInfo unknown;
    unknown.identifier = "no-such-camera";
    unknown.display_name = "No such camera";
    const auto refused = camera.start(unknown);
    if (refused) {
        bad.emplace_back("start: a device that was never listed was started");
        camera.stop();
    } else if (refused.error() == cam::CameraError::already_running) {
        bad.emplace_back("start: 'already running' was answered with nothing running");
    } else if (listed && refused.error() != cam::CameraError::no_such_device) {
        bad.push_back("start: an unlisted device was refused with '" +
                      describe_error(refused.error()) +
                      "' and not no_such_device, though the platform lists devices");
    }
    if (camera.is_capturing()) {
        bad.emplace_back("start: a refused start left the camera running");
    }
    if (camera.pop().has_value()) {
        bad.emplace_back("start: a refused start left a frame to take");
    }

    if (!listed || listed->empty()) {
        return bad;  // nothing to run: the platform has no camera, which is its right
    }
    if (has_device != nullptr) {
        *has_device = true;
    }

    const cam::DeviceInfo& device = listed->front();
    const auto started = camera.start(device);
    if (!started) {
        if (started.error() != cam::CameraError::permission_denied) {
            bad.push_back("start: a listed device could not be started: " +
                          describe_error(started.error()));
        }
        return bad;  // a refusal by the person is not a fault of the platform
    }
    if (!camera.is_capturing()) {
        bad.emplace_back("start: is_capturing is false after a start that succeeded");
    }
    const auto again = camera.start(device);
    if (again) {
        bad.emplace_back("start: a second start while running was accepted");
    } else if (again.error() != cam::CameraError::already_running) {
        bad.push_back("start: a second start was refused with '" + describe_error(again.error()) +
                      "' and not already_running");
    }
    if (!camera.is_capturing()) {
        bad.emplace_back("start: a refused second start stopped the capture");
    }

    // Frames: a whole frame, and each one newer than the one before. None is given twice.
    if (!camera.frames_arrive(3)) {
        bad.emplace_back("frames: no frame arrived while capturing");
    } else {
        std::optional<OwnedFrame> first = camera.pop();
        if (!first) {
            bad.emplace_back("frames: frames arrived and the mailbox is empty");
        } else {
            if (!first->view().valid() || first->width == 0 || first->height == 0) {
                bad.emplace_back(
                    "frames: a frame is not valid() (its buffer does not hold its rows)");
            }
            if (camera.newest_arrived && first->t_ms != camera.newest_arrived()) {
                bad.emplace_back(
                    "frames: the mailbox gave a frame that was not the latest to arrive");
            }
            std::int64_t last = first->t_ms;
            for (std::optional<OwnedFrame> next = camera.pop(); next; next = camera.pop()) {
                if (next->t_ms <= last) {
                    bad.emplace_back(
                        "frames: a frame taken later is not newer than the one before it");
                    break;
                }
                last = next->t_ms;
            }
        }
    }

    // Stopping ends it, drops what was waiting, and may be done again. (A frame is left
    // waiting for it to drop.)
    (void)camera.frames_arrive(1);
    camera.stop();
    if (camera.is_capturing()) {
        bad.emplace_back("stop: is_capturing is still true");
    }
    if (camera.pop().has_value()) {
        bad.emplace_back("stop: a frame can be taken after the capture stopped");
    }
    camera.stop();

    // And it can be started again.
    const auto restarted = camera.start(device);
    if (!restarted) {
        bad.push_back("start: a camera could not be started again after stop: " +
                      describe_error(restarted.error()));
    } else {
        if (!camera.is_capturing()) {
            bad.emplace_back("start: is_capturing is false after a restart");
        }
        camera.stop();
    }
    return bad;
}

// A camera that keeps the contract, or breaks one rule of it on request. It is the contract's
// reference: the model of a single-slot, latest-wins mailbox in front of a device.
class FakeCamera {
public:
    enum Bug : unsigned {
        none = 0,
        starts_twice = 1U << 0U,      // a second start while running is accepted
        keeps_the_oldest = 1U << 1U,  // the mailbox holds the first frame, not the latest
        stop_leaves_running = 1U << 2U,
        frame_survives_stop = 1U << 3U,
        accepts_unlisted = 1U << 4U,  // start of a device that was not listed succeeds
        repeats_a_frame = 1U << 5U,   // a frame is given again after it was taken
        wrong_error = 1U << 6U,       // an unlisted device is refused as already_running
    };

    explicit FakeCamera(unsigned bugs = none, bool has_devices = true)
        : bugs_(bugs), has_devices_(has_devices) {}

    [[nodiscard]] std::expected<std::vector<cam::DeviceInfo>, cam::CameraError> enumerate() {
        if (!has_devices_) {
            return std::unexpected(cam::CameraError::unavailable);
        }
        return std::vector<cam::DeviceInfo>{{"front", "Front camera", cam::Facing::front},
                                            {"rear", "Rear camera", cam::Facing::rear}};
    }

    [[nodiscard]] std::expected<void, cam::CameraError> start(const cam::DeviceInfo& device) {
        if (!has_devices_) {
            return std::unexpected(cam::CameraError::unavailable);
        }
        const bool listed = device.identifier == "front" || device.identifier == "rear";
        if (!listed && (bugs_ & accepts_unlisted) == 0U) {
            return std::unexpected((bugs_ & wrong_error) != 0U ? cam::CameraError::already_running
                                                               : cam::CameraError::no_such_device);
        }
        if (capturing_ && (bugs_ & starts_twice) == 0U) {
            return std::unexpected(cam::CameraError::already_running);
        }
        capturing_ = true;
        return {};
    }

    void stop() {
        if ((bugs_ & stop_leaves_running) == 0U) {
            capturing_ = false;
        }
        if ((bugs_ & frame_survives_stop) == 0U) {
            slot_.reset();
        }
    }

    [[nodiscard]] bool is_capturing() const { return capturing_; }

    [[nodiscard]] std::optional<OwnedFrame> pop() {
        std::optional<OwnedFrame> taken = slot_;
        if ((bugs_ & repeats_a_frame) == 0U) {
            slot_.reset();
        }
        return taken;
    }

    // A frame arrives from the device. While nothing is capturing it is dropped.
    void deliver() {
        OwnedFrame frame;
        frame.width = 4;
        frame.height = 2;
        frame.pixels.assign(8, 100);
        frame.t_ms = clock_ += 33;
        if (!capturing_) {
            return;
        }
        if ((bugs_ & keeps_the_oldest) != 0U && slot_) {
            return;
        }
        slot_ = std::move(frame);
    }

    [[nodiscard]] CameraOps ops() {
        return {[this] { return enumerate(); },
                [this](const cam::DeviceInfo& device) { return start(device); },
                [this] { stop(); },
                [this] { return is_capturing(); },
                [this] { return pop(); },
                [this](std::size_t count) {
                    for (std::size_t i = 0; i < count; ++i) {
                        deliver();
                    }
                    return capturing_;
                },
                [this] { return clock_; }};
    }

    [[nodiscard]] std::int64_t clock() const { return clock_; }

private:
    unsigned bugs_;
    bool has_devices_;
    bool capturing_ = false;
    std::int64_t clock_ = 1000;
    std::optional<OwnedFrame> slot_;
};

// --- the board reader -----------------------------------------------------------------

using ReadFn =
    std::function<std::expected<BoardReading, ReadError>(const Frame&, const ReadOptions&)>;

[[nodiscard]] inline bool is_tile_letters(const std::string& letters) {
    return !letters.empty() && letters.size() <= 3 &&
           std::ranges::all_of(letters, [](char c) { return c >= 'A' && c <= 'Z'; });
}

// The rules of wjs/platform/board_reader.hpp, given a clear frame of a board it can read
// (`board`) and the reader's own answer for it to be compared with. `min_correct` is how
// many of its tiles the reader must get right: a platform's own recogniser has its own
// accuracy tests, and the contract is about the shape of the answer.
[[nodiscard]] inline std::vector<std::string> board_reader_violations(const ReadFn& read,
                                                                      const synth::Rendered& board,
                                                                      std::size_t min_correct = 0) {
    std::vector<std::string> bad;
    const ReadOptions options;

    // A frame that is not a frame, and a picture with no board in it.
    const auto empty = read(Frame{}, options);
    if (empty.has_value() || empty.error() != ReadError::invalid_frame) {
        bad.emplace_back("errors: an empty frame is not refused with invalid_frame");
    }
    const GrayImage blank(320, 240, 120);
    if (read(blank.view(), options).has_value()) {
        bad.emplace_back("errors: a picture with no board in it was read as a board");
    }

    const std::vector<std::uint8_t> before = board.image.pixels;
    const auto first = read(board.image.view(), options);
    if (board.image.pixels != before) {
        bad.emplace_back("frame: the reader changed the frame it was given");
    }
    if (!first) {
        bad.push_back("reading: a clear board was not read: " +
                      std::string(to_string(first.error())));
        return bad;
    }
    const BoardReading& reading = *first;

    if (reading.rows != board.size || reading.columns != board.size) {
        bad.emplace_back("reading: the board is not counted at its size");
    }
    if (reading.tiles.size() != reading.rows * reading.columns) {
        bad.emplace_back("reading: tiles are not rows * columns of them");
    }
    for (const TileReading& tile : reading.tiles) {
        if (!tile.letters.empty() && !is_tile_letters(tile.letters)) {
            bad.push_back("reading: tile letters '" + tile.letters + "' are not 1 to 3 of A to Z");
            break;
        }
    }
    for (const TileReading& tile : reading.tiles) {
        if (!(tile.confidence >= 0.0 && tile.confidence <= 1.0)) {
            bad.emplace_back("reading: a confidence is not from 0 to 1");
            break;
        }
    }
    for (const TileReading& tile : reading.tiles) {
        if (tile.letters.empty() && !tile.doubtful) {
            bad.emplace_back("reading: a tile with no letters is not doubtful");
            break;
        }
    }
    if (!(reading.size_confidence >= 0.0 && reading.size_confidence <= 1.0)) {
        bad.emplace_back("reading: size_confidence is not from 0 to 1");
    }
    const double width = static_cast<double>(board.image.width);
    const double height = static_cast<double>(board.image.height);
    for (const Point& corner : reading.corners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || corner.x < -0.1 * width ||
            corner.x > 1.1 * width || corner.y < -0.1 * height || corner.y > 1.1 * height) {
            bad.emplace_back("reading: a corner is not a place in (or at the edge of) the frame");
            break;
        }
    }

    // What the application does with a reading must work on it: it is the one way in.
    if (!BoardDraft::try_from_reading(reading).has_value()) {
        bad.emplace_back("reading: BoardDraft cannot be made from it");
    }
    if (min_correct > 0 && synth::judge(board, first).correct < min_correct) {
        bad.emplace_back("reading: fewer than the required tiles of a clear board are right");
    }

    // A pure function of the frame and the options.
    const auto second = read(board.image.view(), options);
    if (!second || second->tiles.size() != reading.tiles.size()) {
        bad.emplace_back("reading: a second read of the same frame did not give a board");
    } else {
        for (std::size_t i = 0; i < reading.tiles.size(); ++i) {
            if (second->tiles[i].letters != reading.tiles[i].letters) {
                bad.emplace_back("reading: two reads of the same frame disagree");
                break;
            }
        }
    }
    return bad;
}

}  // namespace wjs::scan::contract
