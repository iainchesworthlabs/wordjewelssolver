// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/app/sound_backend.hpp"

#include <QSoundEffect>
#include <QString>
#include <QUrl>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wjs::app {
namespace {

// Where the application embeds assets/sounds (apps/app/CMakeLists.txt), by the
// stem the cue policy names.
[[nodiscard]] QUrl source_for(std::string_view asset) {
    const QString stem = QString::fromUtf8(asset.data(), static_cast<qsizetype>(asset.size()));
    return QUrl(QStringLiteral("qrc:/sounds/") + stem + QStringLiteral(".wav"));
}

}  // namespace

class SoundBackend::Impl {
public:
    // One QSoundEffect per asset, made when it is first wanted: a cue the
    // program never reaches costs no audio object, and the second rattle does
    // not reload the file.
    void play(std::string_view asset, double volume) {
        const QString stem = QString::fromUtf8(asset.data(), static_cast<qsizetype>(asset.size()));
        auto& effect = effects_[stem.toStdString()];
        if (!effect) {
            effect = std::make_unique<QSoundEffect>();
            effect->setSource(source_for(asset));
            effect->setLoopCount(1);
        }
        effect->setVolume(static_cast<float>(std::clamp(volume, 0.0, 1.0)));
        // A device with no audio output, or the sound switched off, leaves the
        // effect in Error state; play() is still safe to call.
        effect->play();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<QSoundEffect>> effects_;
};

SoundBackend::SoundBackend() : impl_(std::make_unique<Impl>()) {}

SoundBackend::~SoundBackend() = default;

bool SoundBackend::available() const {
    // The module is in the build. Whether a speaker is there at this moment is
    // the operating system's business, and a missing one is not an error.
    return true;
}

void SoundBackend::play(std::string_view asset, double volume) {
    impl_->play(asset, volume);
}

}  // namespace wjs::app
