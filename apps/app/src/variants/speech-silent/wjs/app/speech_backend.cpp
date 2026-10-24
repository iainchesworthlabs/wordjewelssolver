// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/app/speech_backend.hpp"

// Nothing to do: this variant is compiled when Qt TextToSpeech is not in the
// kit. The application still accepts a speak cue and its word is simply not
// said; the word is also on screen, so nothing is lost.

namespace wjs::app {

SpeechBackend::SpeechBackend() = default;

SpeechBackend::~SpeechBackend() = default;

bool SpeechBackend::available() const {
    return false;
}

void SpeechBackend::say(const QString&, double) {}

void SpeechBackend::stop() {}

}  // namespace wjs::app
