// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "wjs/calendar.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/settings.hpp"
#include "wjs/store.hpp"

// What a parent sees (docs/design/dashboard.md, "What the dashboard shows"):
// numbers computed from the store when the dashboard is opened, on the device
// and for nobody else. Four parts: the sounds, this week, the vault and the
// best. There is no time on screen, no streak, no comparison and no grade in
// it, because the store holds nothing from which they could be made.
//
// This file computes and does not word: it returns counts, and the screens
// turn them into the sentences of the design (ParentWording.qml), so that every
// sentence a parent reads is in one reviewable place and none is typed into the
// library. What each count means is the **proposal for the literacy review**
// (gate G7) of dashboard.md's "Definitions", restated at each field below. A
// count is an observation: it is never a finding about a child who has not
// played ("a gap in play is not a deficit"), and a sound that has not come up
// is `met == 0`, not a failure.

namespace wjs {

// Three or more distinct words with a sound, each found without the start tile
// at least once, is "often found" (dashboard.md, "Often found"). **To confirm
// with the reviewer.**
inline constexpr int often_found_words = 3;

// A ticked sound that fewer than this many words of the phase use is said to
// have few words yet, and seeding falls back to the phase generally
// (dashboard.md, "Focus sounds"). Three is the number of targets a board
// takes. **To confirm with the author.**
inline constexpr std::size_t few_words_threshold = 3;

// One sound (a grapheme of the active phase, written as in the graphemes file)
// and what the store says of it. Counted over the last twenty boards the store
// holds and the words ever found, the two things it keeps for the purpose; a
// board of the standard mode has no sounds in it and is not counted.
struct SoundRow {
    std::string letters;  // "ai", "a_e"

    // Words of the active phase that use the sound.
    std::size_t words_in_phase = 0;

    // Boards on which a word with the sound was on the board ("met": a sound
    // that never came up is not a blind spot).
    int boards_met = 0;

    // Of those boards, those on which a word with the sound was found. Never
    // more than boards_met, so "on 6 boards; found on 1" always reads.
    int boards_found = 0;

    // Distinct words with the sound that have been found at least once, and,
    // of them, found at least once without the start tile. "Unassisted" is
    // exact: the start tile is the only help in the MVP.
    int words_found = 0;
    int words_unassisted = 0;

    // words_unassisted >= often_found_words.
    bool often_found = false;

    // The parent has ticked it this week, and the few words that use it.
    bool focus = false;
    bool few_words = false;
};

struct WeekSummary {
    Day first = 0;  // the week's first day, by the device's calendar locale
    Day last = 0;   // and its seventh

    int boards_cleared = 0;
    int stars = 0;
    int new_words = 0;  // met for the first time on this device this week

    // The longest word found this week (the first alphabetically of equal
    // length), or empty.
    std::string longest_word;

    // Something happened this week. Without it the screen says nothing of the
    // week: a gap in play is not a deficit.
    [[nodiscard]] bool played() const noexcept {
        return boards_cleared > 0 || stars > 0 || new_words > 0 || !longest_word.empty();
    }
};

struct StickerRow {
    std::string word;  // lower case, as the child sees it
    Day first_day = 0;
};

struct BestRow {
    int size = 0;
    int stars = 0;
};

struct Dashboard {
    WeekSummary week;

    // The sounds of the active phase, in the order of the graphemes file.
    // Empty when no phase data is loaded: the repository holds none until the
    // review has written it, and the screen then has no sounds to show.
    std::vector<SoundRow> sounds;
    bool has_phase_data = false;
    std::string phase;  // the index's phase, upper case

    // The words found at least once, by word.
    std::vector<StickerRow> vault;

    // The best cleared board of each size played, by size.
    std::vector<BestRow> best;

    // Nothing has been kept yet: no board, find, star or sticker.
    bool empty = true;
};

// The dashboard as of `today`. `index` is the active phase's index, or null if
// none is loaded. `first_day_of_week` is the calendar locale's, ISO numbered
// (1 Monday to 7 Sunday).
[[nodiscard]] Dashboard build_dashboard(const StoreData& data, const PhaseIndex* index, Day today,
                                        int first_day_of_week);

// The sounds a parent can tick: every grapheme of the phase, each once, in the
// order of the graphemes file. A sound outside the phase cannot be ticked.
[[nodiscard]] std::vector<std::string> focus_sound_choices(const PhaseIndex& index);

// The settings with the ticked sounds held to the phase's: any that the phase
// does not have is dropped. Done whenever the phase changes or a sound is
// ticked.
[[nodiscard]] Settings restrict_focus_sounds(Settings settings, const PhaseIndex& index);

}  // namespace wjs
