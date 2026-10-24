// SPDX-License-Identifier: GPL-3.0-or-later
// The camera a kit without Qt Multimedia has: none. The source and the screens
// are the same ones the multimedia build compiles, and every question the
// interface asks is answered with the truth: no device, no preview and no frame.
// Mode A offers "Type in your board" beside it (docs/design/game.md, "No
// camera"), exactly as it does on a desktop with no webcam.
//
// This is a variant, not a special case in a source file: CMake compiles either
// this file or apps/app/src/variants/camera-multimedia/wjs/app/camera_backend.cpp
// (apps/app/CMakeLists.txt).

#include "camera_backend.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace wjs::app {
namespace {

class NoCameraBackend final : public CameraBackend {
public:
    void setChanged(Changed /*changed*/) override {}

    [[nodiscard]] bool available() const override { return false; }

    [[nodiscard]] QStringList devices() const override { return {}; }

    [[nodiscard]] CameraProblem problem() const override { return CameraProblem::no_camera; }

    [[nodiscard]] bool running() const override { return false; }

    bool start() override { return false; }

    void stop() override {}

    [[nodiscard]] std::optional<wjs::scan::OwnedFrame> capture() override { return std::nullopt; }

    void attachPreview(QObject* /*videoOutput*/) override {}

    void detachPreview() override {}
};

}  // namespace

std::unique_ptr<CameraBackend> make_camera_backend() {
    return std::make_unique<NoCameraBackend>();
}

}  // namespace wjs::app
