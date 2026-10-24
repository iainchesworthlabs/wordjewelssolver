// SPDX-License-Identifier: GPL-3.0-or-later
#include "sound_engine.hpp"

#include <utility>

#include "qt_text.hpp"
#include "wjs/app/sound_backend.hpp"
#include "wjs/profiling.hpp"

namespace wjs::app {
namespace {

using wjs::app::to_utf8;

}  // namespace

class SoundEngine::Impl {
public:
    SoundBackend backend;
    SoundEngine::Output output;
    int plays = 0;
};

SoundEngine::SoundEngine(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}

SoundEngine::~SoundEngine() = default;

bool SoundEngine::available() const {
    return impl_->backend.available();
}

int SoundEngine::plays() const {
    return impl_->plays;
}

void SoundEngine::setOutput(Output output) {
    impl_->output = std::move(output);
}

void SoundEngine::play(const QString& asset, double volume) {
    // The first play of an asset loads it, which is the long one.
    const auto zone = wjs::profiling::zone<"sound.play">();
    ++impl_->plays;
    if (impl_->output) {
        impl_->output(asset, volume);
    } else {
        impl_->backend.play(to_utf8(asset), volume);
    }
    emit played(asset, volume);
}

}  // namespace wjs::app
