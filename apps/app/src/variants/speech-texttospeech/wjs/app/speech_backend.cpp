// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/app/speech_backend.hpp"

#include <QTextToSpeech>
#include <algorithm>
#include <memory>

namespace wjs::app {

class SpeechBackend::Impl {
public:
    [[nodiscard]] bool available() {
        ensure();
        return engine_ != nullptr;
    }

    void say(const QString& text, double volume) {
        ensure();
        if (engine_ == nullptr || text.isEmpty()) {
            return;
        }
        engine_->setVolume(std::clamp(volume, 0.0, 1.0));
        engine_->say(text);
    }

    void stop() {
        if (engine_ != nullptr) {
            engine_->stop();
        }
    }

private:
    // The engine is made when the first word is said, not when the program
    // starts: a child who never finds a word costs no speech engine, and a
    // device whose engine is missing is not asked until then.
    void ensure() {
        if (checked_) {
            return;
        }
        checked_ = true;
        if (QTextToSpeech::availableEngines().isEmpty()) {
            return;
        }
        engine_ = std::make_unique<QTextToSpeech>();
        // A slightly slower voice is easier for a beginning reader to follow,
        // and the platform clamps the value to what it offers.
        engine_->setRate(-0.15);
    }

    bool checked_ = false;
    std::unique_ptr<QTextToSpeech> engine_;
};

SpeechBackend::SpeechBackend() : impl_(std::make_unique<Impl>()) {}

SpeechBackend::~SpeechBackend() = default;

bool SpeechBackend::available() const {
    return impl_->available();
}

void SpeechBackend::say(const QString& text, double volume) {
    impl_->say(text, volume);
}

void SpeechBackend::stop() {
    impl_->stop();
}

}  // namespace wjs::app
