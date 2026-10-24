// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

// The silent speech backend: no Qt TextToSpeech in the build, so nothing is
// said and every request is harmless. It has the same header path and interface
// as the Qt TextToSpeech variant, chosen by CMake (apps/app/src/feedback/speech.cpp
// includes it).

namespace wjs::app {

class SpeechBackend {
public:
    SpeechBackend();
    ~SpeechBackend();

    SpeechBackend(const SpeechBackend&) = delete;
    SpeechBackend& operator=(const SpeechBackend&) = delete;

    [[nodiscard]] bool available() const;
    void say(const QString& text, double volume);
    void stop();
};

}  // namespace wjs::app
