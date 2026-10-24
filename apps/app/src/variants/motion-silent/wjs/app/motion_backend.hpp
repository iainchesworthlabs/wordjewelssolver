// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "wjs/motion.hpp"

// The unavailable motion backend: no Qt Sensors in the build, so there is no
// accelerometer and no sample. It has the same header path and interface as the
// Qt Sensors variant, chosen by CMake (apps/app/src/feedback/motion_source.cpp
// includes it). The desktop drag stands in for a shake (SoloScreen.qml).

namespace wjs::app {

class MotionBackend {
public:
    using Sink = std::function<void(const wjs::DeviceMotion&)>;

    explicit MotionBackend(Sink sink);
    ~MotionBackend();

    MotionBackend(const MotionBackend&) = delete;
    MotionBackend& operator=(const MotionBackend&) = delete;

    [[nodiscard]] bool available();
    bool start();
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wjs::app
