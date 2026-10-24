// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/feedback.hpp"

#include <array>
#include <utility>

#include "wjs/profiling.hpp"

namespace wjs {
namespace {

// One asset name per cue that has a sound. They are the stems of the files in
// assets/sounds; the engine adds the directory and ".wav".
constexpr std::string_view tap_asset = "tap";
constexpr std::string_view rattle_asset = "rattle";
constexpr std::string_view tile_asset = "letter";
constexpr std::string_view landed_asset = "land";
constexpr std::string_view cheer_asset = "cheer";
constexpr std::string_view tick_asset = "tick";
constexpr std::string_view chime_asset = "chime";
constexpr std::string_view cleared_asset = "cleared";

struct CueName {
    FeedbackCue cue;
    std::string_view name;
};

constexpr std::array<CueName, 9> cue_names = {{
    {FeedbackCue::speak, "speak"},
    {FeedbackCue::tap, "tap"},
    {FeedbackCue::rattle, "rattle"},
    {FeedbackCue::tile, "tile"},
    {FeedbackCue::tile_landed, "tile-landed"},
    {FeedbackCue::word, "word"},
    {FeedbackCue::tick, "tick"},
    {FeedbackCue::chime, "chime"},
    {FeedbackCue::cleared, "cleared"},
}};

// A celebration asset's share of the master volume.
[[nodiscard]] double celebration_volume(const FeedbackSettings& settings) noexcept {
    return settings.celebration_sounds == CelebrationSounds::quiet ? quiet_volume : 1.0;
}

}  // namespace

std::string_view to_string(FeedbackCue cue) noexcept {
    for (const CueName& entry : cue_names) {
        if (entry.cue == cue) {
            return entry.name;
        }
    }
    return "speak";
}

std::optional<FeedbackCue> feedback_cue_from_string(std::string_view name) noexcept {
    for (const CueName& entry : cue_names) {
        if (entry.name == name) {
            return entry.cue;
        }
    }
    return std::nullopt;
}

CuePlan plan_cue(FeedbackCue cue, std::string_view detail, const FeedbackSettings& settings) {
    auto zone = profiling::zone<"feedback.plan">();

    CuePlan plan;
    const bool audible = settings.sound;

    switch (cue) {
        case FeedbackCue::speak:
            // A picture's or a found word's name: the platform's voice, never a
            // short asset (there is no recording of every word).
            plan.speak_text = audible && !detail.empty();
            return plan;

        case FeedbackCue::tap:
            if (audible) {
                plan.sound = true;
                plan.asset = tap_asset;
                plan.volume = 0.5;
            }
            return plan;

        case FeedbackCue::rattle:
            if (audible) {
                plan.sound = true;
                plan.asset = rattle_asset;
                plan.volume = 0.75;
            }
            plan.haptic = HapticCue::light;
            break;

        case FeedbackCue::tile:
            // The letter's phoneme is a recorded clip the reviewer owns; until
            // it exists this is a short neutral tone for the join.
            if (audible) {
                plan.sound = true;
                plan.asset = tile_asset;
                plan.volume = 0.8;
            }
            plan.haptic = HapticCue::light;
            break;

        case FeedbackCue::tile_landed:
            if (audible) {
                plan.sound = true;
                plan.asset = landed_asset;
                plan.volume = 0.55;
            }
            break;

        case FeedbackCue::word:
            // The whole word through the voice, governed by Sound it out; the
            // cheer, governed by Celebration sounds. Both are lowered by quiet.
            plan.volume = celebration_volume(settings);
            if (audible && settings.celebration_sounds != CelebrationSounds::off) {
                plan.sound = true;
                plan.asset = cheer_asset;
            }
            if (audible && settings.sound_it_out != SoundItOut::off && !detail.empty()) {
                plan.speak_word = true;
                plan.speak_sequence = settings.sound_it_out == SoundItOut::full;
            }
            plan.haptic = HapticCue::medium;
            break;

        case FeedbackCue::tick:
            // A real word that is not today's: a soft neutral sound, no word.
            if (audible) {
                plan.sound = true;
                plan.asset = tick_asset;
                plan.volume = 0.45;
            }
            break;

        case FeedbackCue::chime:
            plan.volume = celebration_volume(settings);
            if (audible && settings.celebration_sounds != CelebrationSounds::off) {
                plan.sound = true;
                plan.asset = chime_asset;
            }
            plan.haptic = HapticCue::medium;
            break;

        case FeedbackCue::cleared:
            plan.volume = celebration_volume(settings);
            if (audible && settings.celebration_sounds != CelebrationSounds::off) {
                plan.sound = true;
                plan.asset = cleared_asset;
            }
            plan.haptic = HapticCue::double_tap;
            break;
    }

    if (!settings.vibration) {
        plan.haptic = HapticCue::none;
    }
    return plan;
}

}  // namespace wjs
