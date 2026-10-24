// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/phase_data.hpp"
#include "wjs/settings.hpp"

// "Sound it out": what is said and shown when a word of the phonics board has
// been found (docs/design/phonics.md, "Sound it out"). This is the list of
// steps, a pure function of the phase, the word and the parent's setting. It
// plays nothing and times nothing: the application walks the list, lights a
// block when its clip starts, and waits the pauses and the blend the steps ask
// for. The sequence is data, not a timeline in the code.
//
//   1. Segment: each grapheme of the word in turn, its sound played. One
//      `sound` step a grapheme, the next beginning `segment_gap` after the
//      clip before it ends.
//   2. Blend: the blocks slide together and the sounds are played again,
//      quicker: one `blend` step of `blend_duration`.
//   3. Say it: the whole word, by recording or the device's own speech.
//   4. Cheer: a short phrase that names the sound where that helps, which is
//      when the word has a grapheme of more than one letter. The wording is the
//      reviewer's; the step says which sound it names.
//
// The setting decides how much: Sound it out *full* is all four, *the word
// only* is the third, *off* is none. A tricky word does not decode, so it is
// said whole whatever the setting says short of off.

namespace wjs {

// The silence between the end of one sound of the segment and the start of the
// next, and the time the blend takes (the design system's `turn` token).
inline constexpr std::chrono::milliseconds segment_gap{150};
inline constexpr std::chrono::milliseconds blend_duration{380};

enum class SoundStepKind {
    sound,  // one grapheme of the word, sounded
    blend,  // the blocks together, the sounds again, quicker
    say,    // the whole word
    cheer,  // the phrase after the word
};

[[nodiscard]] std::string_view to_string(SoundStepKind kind) noexcept;

struct SoundStep {
    SoundStepKind kind = SoundStepKind::say;

    // Silence before the step begins, counted from the end of the step before.
    std::chrono::milliseconds gap_before{0};

    // How long the step takes when the clips do not say: `blend_duration` for a
    // blend, zero for the others, which last as long as their clip or speech.
    std::chrono::milliseconds duration{0};

    // sound: the index of the grapheme in the word (the block to light), in
    // order of the first letters. The other kinds: zero.
    std::size_t block = 0;

    // sound: the grapheme's letters ("ai", "a_e"). blend and say: the word,
    // lower case. cheer: the letters of the grapheme it names, or none.
    std::string letters;

    // sound and cheer: the sound as the graphemes file writes it, slashes
    // included ("/ay/"); empty for the other kinds and for a cheer that names
    // none.
    std::string sound;

    // The clip ids to play (without the "clip:"): one for a sound, one for each
    // grapheme in order for a blend, none for say and cheer.
    std::vector<std::string> clips;

    friend bool operator==(const SoundStep&, const SoundStep&) = default;
};

enum class SoundSequenceError {
    not_in_phase,     // the word is not a word of the phase
    unknown_grapheme  // the word names a grapheme or sound the phase does not define
};

[[nodiscard]] std::string_view to_string(SoundSequenceError error) noexcept;

struct SoundSequence {
    std::string word;  // lower case
    // The word is said whole and nothing is sounded: it is tricky, or the
    // setting is the word only. Empty steps under off do not set this.
    bool whole_word_only = false;
    std::vector<SoundStep> steps;  // empty when the setting is off

    friend bool operator==(const SoundSequence&, const SoundSequence&) = default;
};

// The steps for `word` (any case) of `phase` under `setting`. `off` is a
// sequence with no steps, not an error. A word the phase does not hold is
// `not_in_phase` and the caller says it whole through the speech it has.
[[nodiscard]] std::expected<SoundSequence, SoundSequenceError> sound_it_out_sequence(
    const PhaseIndex& phase, std::string_view word, SoundItOut setting);

// The time the steps themselves fix: every gap and every blend, not the clips
// and speech, whose lengths are not known here.
[[nodiscard]] std::chrono::milliseconds fixed_time(const SoundSequence& sequence) noexcept;

}  // namespace wjs
