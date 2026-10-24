// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <memory>

// The Qt TextToSpeech backend. apps/app/src/feedback/speech.cpp includes this one
// header; CMake puts one of the variant directories on its include path
// (apps/app/CMakeLists.txt), as cmake/Tracy.cmake chooses wjs/profiling.hpp.
//
// TextToSpeech is output only: the class has no audio input and asks the
// platform to say text. A platform with no engine (the class asks
// QTextToSpeech::availableEngines() first) answers false here and the caller's
// say() is a harmless no-op.

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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wjs::app
