// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>

#include "wjs/motion.hpp"

// The Qt Sensors backend. apps/app/src/feedback/motion_source.cpp includes this one
// header; CMake puts one of the variant directories on its include path
// (apps/app/CMakeLists.txt), as cmake/Tracy.cmake chooses wjs/profiling.hpp.
//
// QAccelerometer and QGyroscope are started together at the highest data rate
// the platform lists. Each accelerometer reading is one sample: it carries the
// latest gyroscope reading with it, because the scene and the shake detector
// want both together and an accelerometer is the stream that keeps time. A
// platform with no accelerometer answers available() false and start() false,
// and the drag that stands in for one stays.
//
// Nothing is stored: the sample goes straight to the sink.

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
