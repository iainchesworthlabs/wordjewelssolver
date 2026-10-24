// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedback_engine.hpp"

#include <QByteArray>
#include <QString>
#include <string>
#include <string_view>
#include <utility>

#include "qt_text.hpp"
#include "wjs/platform/haptics.hpp"
#include "wjs/profiling.hpp"
#include "wjs/settings.hpp"

namespace wjs::app {
namespace {

using wjs::app::to_qstring;
using wjs::app::to_utf8;

// The map's value for `key`, or `fallback` when the parent area has not filled
// it or it is not the type the screens write.
template <typename Value>
[[nodiscard]] Value value_or(const QVariantMap& map, const QString& key, Value fallback) {
    const QVariant value = map.value(key);
    if (!value.isValid()) {
        return fallback;
    }
    return value.value<Value>();
}

[[nodiscard]] wjs::FeedbackSettings policy_settings(const QVariantMap& map) {
    wjs::FeedbackSettings settings;
    settings.sound = value_or<bool>(map, QStringLiteral("sound"), settings.sound);
    settings.vibration = value_or<bool>(map, QStringLiteral("vibration"), settings.vibration);

    const auto celebration = wjs::celebration_sounds_from_string(to_utf8(
        value_or<QString>(map, QStringLiteral("celebrationSounds"), QStringLiteral("full"))));
    if (celebration) {
        settings.celebration_sounds = *celebration;
    }
    const auto sound_it_out = wjs::sound_it_out_from_string(
        to_utf8(value_or<QString>(map, QStringLiteral("soundItOut"), QStringLiteral("full"))));
    if (sound_it_out) {
        settings.sound_it_out = *sound_it_out;
    }
    return settings;
}

[[nodiscard]] wjs::platform::HapticPattern platform_pattern(wjs::HapticCue cue) {
    switch (cue) {
        case wjs::HapticCue::light:
            return wjs::platform::HapticPattern::light;
        case wjs::HapticCue::medium:
            return wjs::platform::HapticPattern::medium;
        case wjs::HapticCue::double_tap:
            return wjs::platform::HapticPattern::double_tap;
        case wjs::HapticCue::none:
            break;
    }
    return wjs::platform::HapticPattern::light;
}

}  // namespace

FeedbackEngine::FeedbackEngine(QObject* parent)
    : QObject(parent),
      sequence_([this](const QString& word, double volume) { speech_.say_word(word, volume); }),
      settings_map_{{QStringLiteral("sound"), true},
                    {QStringLiteral("celebrationSounds"), QStringLiteral("full")},
                    {QStringLiteral("soundItOut"), QStringLiteral("full")},
                    {QStringLiteral("vibration"), true}} {}

FeedbackEngine::~FeedbackEngine() = default;

void FeedbackEngine::setSettings(const QVariantMap& settings) {
    if (settings == settings_map_) {
        return;
    }
    settings_map_ = settings;
    settings_ = policy_settings(settings_map_);
    phonics_board_ =
        settings_map_.value(QStringLiteral("board")).toString() == QStringLiteral("phonics");
    emit settingsChanged();
}

int FeedbackEngine::soundsPlayed() const {
    return sound_.plays();
}

bool FeedbackEngine::sayFoundWord(const QString& word, double volume) {
    // A word of the phonics board is sounded out by the library's steps, and the
    // last of them says the word; with no clips recorded yet that is at once, so
    // the child hears the word as before. Anything else is said whole.
    if (phase_ != nullptr && phonics_board_ && !word.isEmpty()) {
        auto sequence = wjs::sound_it_out_sequence(*phase_, to_utf8(word), settings_.sound_it_out);
        if (sequence && !sequence->steps.empty()) {
            // Asked for: the word is said by the sequence's say step, now or when
            // the clips before it have played.
            sequence_.start(*std::move(sequence), volume);
            return true;
        }
    }
    return speech_.say_word(word, volume);
}

int FeedbackEngine::utterances() const noexcept {
    // The voice's own count: the one place that knows whether anything was said
    // (it says nothing for an empty word).
    return speech_.utterances();
}

void FeedbackEngine::cue(const QString& kind, const QString& detail) {
    // A moment's whole answer: the policy, the sound, the voice and the buzz.
    const auto zone = wjs::profiling::zone<"feedback.play">();
    const auto parsed = wjs::feedback_cue_from_string(to_utf8(kind));
    if (!parsed) {
        return;
    }

    const wjs::CuePlan plan = wjs::plan_cue(*parsed, to_utf8(detail), settings_);
    bool asked = false;

    if (plan.sound) {
        sound_.play(to_qstring(plan.asset), plan.volume);
        asked = true;
    }
    if (plan.speak_word) {
        asked = sayFoundWord(detail, plan.volume) || asked;
    } else if (plan.speak_text) {
        asked = speech_.say_text(detail, plan.volume) || asked;
    }
    if (plan.haptic != wjs::HapticCue::none) {
        ++haptics_requested_;
        // The desktops answer false; a tablet will answer with its own engine.
        // The answer is deliberately not read: every haptic is paired with the
        // sound and the picture that were already asked for.
        static_cast<void>(wjs::platform::vibrate(platform_pattern(plan.haptic)));
        asked = true;
    }

    if (asked) {
        emit requestsChanged();
    }
}

}  // namespace wjs::app
