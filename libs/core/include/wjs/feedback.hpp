// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "wjs/settings.hpp"

// What a moment of the game should do about sound, speech and a tap of the
// hand: a pure function from the cue the screens already emit, the settings a
// parent chose and the cue's detail. It decodes nothing and plays nothing, so
// the mapping is tested without an audio device or a sensor.
//
// The table is docs/design/application.md, "Audio and touch feedback". The
// application's FeedbackEngine carries the answer out: a short asset through
// Qt Multimedia, a whole word or sentence through the device's own speech, and
// a pattern through the platform's haptics interface. Every part can be
// absent: a build without Qt Multimedia still answers the plan, and the
// engine simply has nothing to play it with.

namespace wjs {

// The cue kinds the screens emit (`Feedback.cue(kind, detail)` and the models'
// `cue` signals). "tile-landed" is the scene's die hitting the floor, which
// carries a pitch in `detail`; "speak" is a picture's or a word's name, said
// through the platform's voice.
enum class FeedbackCue {
    speak,
    tap,
    rattle,
    tile,
    tile_landed,
    word,
    tick,
    chime,
    cleared,
};

[[nodiscard]] std::string_view to_string(FeedbackCue cue) noexcept;
[[nodiscard]] std::optional<FeedbackCue> feedback_cue_from_string(std::string_view name) noexcept;

// The one tap of the hand a plan asks for. The platform interface
// (libs/platforms/interfaces/wjs/platform/haptics.hpp) has the same shape; this
// is the library's name for it so the policy stays free of the platform layer.
enum class HapticCue {
    none,
    light,       // a tile joining, the box rattling
    medium,      // a word found, a new best
    double_tap,  // the board cleared
};

// The subset of Settings that decides feedback, with the settings' defaults.
// It is separate from Settings so a caller does not have to hold a whole store
// to ask what a cue does.
struct FeedbackSettings {
    bool sound = true;
    CelebrationSounds celebration_sounds = CelebrationSounds::full;
    SoundItOut sound_it_out = SoundItOut::full;
    bool vibration = true;
};

// The loudness of a celebration set to quiet, as a fraction of full. A
// parent's quiet is quieter, not silent: the sound still says the word was
// found.
inline constexpr double quiet_volume = 0.5;

// What one cue comes to.
struct CuePlan {
    // A short asset under assets/sounds, played through Qt Multimedia.
    bool sound = false;
    std::string_view asset;
    // The volume of both the asset and any speech, 0 to 1. quiet_volume for a
    // celebration the parent set to quiet.
    double volume = 1.0;
    // The whole word through the device's own speech, and the sound-it-out
    // sequence that precedes it. The sequence is a seam: its phoneme clips are
    // the reviewer's recording job and are not played yet (docs/design/
    // phonics.md), so `speak_sequence` is what the eventual player will see.
    bool speak_word = false;
    bool speak_sequence = false;
    // The cue's detail said as a whole word or sentence (a picture's name).
    bool speak_text = false;
    HapticCue haptic = HapticCue::none;
};

// The plan for a cue. `detail` is the word for "word", the letters for "tile",
// the pitch for "tile-landed" and the name for "speak"; the others ignore it.
//
// The rules, all from the design's table:
//   - the master Sound switch silences every asset and every spoken word;
//   - Celebration sounds off silences the cheer, the chime and the cleared
//     notes but not the platform's speech, which Sound it out governs;
//   - quiet lowers both the celebration asset and the spoken word;
//   - Sound it out full and word both say the word; off says nothing;
//   - Vibration off drops every haptic and changes no sound;
//   - a real word that is not today's is a soft tick and nothing else;
//   - not a word has no cue at all (the model does not emit one).
[[nodiscard]] CuePlan plan_cue(FeedbackCue cue, std::string_view detail,
                               const FeedbackSettings& settings);

}  // namespace wjs
