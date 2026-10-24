// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

// The Qt Multimedia sound backend. apps/app/src/feedback/sound_engine.cpp includes
// this one header; CMake puts one of the variant directories on its include
// path (apps/app/CMakeLists.txt), exactly as cmake/Tracy.cmake chooses
// wjs/profiling.hpp. The two variants have the same interface, so the engine
// compiles unchanged either way and no source asks whether the module is there.
//
// A short WAV is played with QSoundEffect: the assets under assets/sounds are
// one-shot cues, never music, and QSoundEffect is the low-latency path for
// them. A device with no audio output, or its silent switch on, simply plays
// nothing.

namespace wjs::app {

class SoundBackend {
public:
    SoundBackend();
    ~SoundBackend();

    SoundBackend(const SoundBackend&) = delete;
    SoundBackend& operator=(const SoundBackend&) = delete;

    [[nodiscard]] bool available() const;
    // `asset` is a stem under assets/sounds; `volume` is 0 to 1.
    void play(std::string_view asset, double volume);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wjs::app
