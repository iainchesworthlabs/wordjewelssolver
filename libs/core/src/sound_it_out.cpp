// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/sound_it_out.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

#include "ascii.hpp"
#include "wjs/profiling.hpp"

namespace wjs {

std::string_view to_string(SoundStepKind kind) noexcept {
    switch (kind) {
        case SoundStepKind::sound:
            return "sound";
        case SoundStepKind::blend:
            return "blend";
        case SoundStepKind::say:
            return "say";
        case SoundStepKind::cheer:
            return "cheer";
    }
    return "say";
}

std::string_view to_string(SoundSequenceError error) noexcept {
    switch (error) {
        case SoundSequenceError::not_in_phase:
            return "the word is not a word of the phase";
        case SoundSequenceError::unknown_grapheme:
            return "the word uses a grapheme or sound the phase does not define";
    }
    return "unknown sound-it-out error";
}

std::expected<SoundSequence, SoundSequenceError> sound_it_out_sequence(const PhaseIndex& phase,
                                                                       std::string_view word,
                                                                       SoundItOut setting) {
    auto zone = profiling::zone<"phonics.sound_it_out">();

    const PhaseWord* entry = phase.find_word(word);
    if (entry == nullptr) {
        return std::unexpected(SoundSequenceError::not_in_phase);
    }

    SoundSequence sequence;
    sequence.word = ascii::lower(entry->word);
    if (setting == SoundItOut::off) {
        return sequence;
    }

    sequence.whole_word_only = entry->tricky || setting == SoundItOut::word_only;
    SoundStep say;
    say.kind = SoundStepKind::say;
    say.letters = sequence.word;
    if (sequence.whole_word_only) {
        sequence.steps.push_back(std::move(say));
        return sequence;
    }

    // The graphemes of the word, each with the line of its sound.
    std::vector<const Grapheme*> sounds;
    sounds.reserve(entry->graphemes.size());
    for (const WordGrapheme& grapheme : entry->graphemes) {
        const Grapheme* known = phase.sound_of(grapheme);
        if (known == nullptr) {
            return std::unexpected(SoundSequenceError::unknown_grapheme);
        }
        sounds.push_back(known);
    }

    for (std::size_t block = 0; block < sounds.size(); ++block) {
        SoundStep step;
        step.kind = SoundStepKind::sound;
        step.gap_before = block == 0 ? std::chrono::milliseconds{0} : segment_gap;
        step.block = block;
        step.letters = sounds[block]->letters;
        step.sound = sounds[block]->sound;
        step.clips = {sounds[block]->clip};
        sequence.steps.push_back(std::move(step));
    }

    SoundStep blend;
    blend.kind = SoundStepKind::blend;
    blend.gap_before = segment_gap;
    blend.duration = blend_duration;
    blend.letters = sequence.word;
    for (const Grapheme* grapheme : sounds) {
        blend.clips.push_back(grapheme->clip);
    }
    sequence.steps.push_back(std::move(blend));

    sequence.steps.push_back(std::move(say));

    // The cheer names the sound of the first grapheme of more than one letter
    // (a vowel team, a digraph, a split digraph): that is where naming it
    // helps. A word of single letters has nothing to name.
    SoundStep cheer;
    cheer.kind = SoundStepKind::cheer;
    const auto named = std::ranges::find_if(
        sounds, [](const Grapheme* grapheme) { return grapheme->letters.size() > 1; });
    if (named != sounds.end()) {
        cheer.letters = (*named)->letters;
        cheer.sound = (*named)->sound;
    }
    sequence.steps.push_back(std::move(cheer));
    return sequence;
}

std::chrono::milliseconds fixed_time(const SoundSequence& sequence) noexcept {
    return std::accumulate(sequence.steps.begin(), sequence.steps.end(),
                           std::chrono::milliseconds{0},
                           [](std::chrono::milliseconds total, const SoundStep& step) {
                               return total + step.gap_before + step.duration;
                           });
}

}  // namespace wjs
