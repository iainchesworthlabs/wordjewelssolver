// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

// The silent sound backend: the build has no Qt Multimedia, so there is
// nothing to play and every cue is a harmless no-op. It is the same header path
// and interface as the Qt Multimedia variant, chosen by CMake
// (apps/app/src/feedback/sound_engine.cpp includes it).

namespace wjs::app {

class SoundBackend {
public:
    SoundBackend();
    ~SoundBackend();

    SoundBackend(const SoundBackend&) = delete;
    SoundBackend& operator=(const SoundBackend&) = delete;

    [[nodiscard]] bool available() const;
    void play(std::string_view asset, double volume);
};

}  // namespace wjs::app
