// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/dashboard.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/profiling.hpp"

namespace wjs {

namespace {

[[nodiscard]] bool in_week(Day day, const WeekSummary& week) noexcept {
    return day >= week.first && day <= week.last;
}

// Whether a word of the phase is spelled with this grapheme. A tricky word is
// spoken whole and has no graphemes, so it has no sound to be met.
[[nodiscard]] bool uses(const PhaseIndex& index, std::string_view word, std::string_view letters) {
    const PhaseWord* found = index.find_word(word);
    if (found == nullptr) {
        return false;
    }
    return std::any_of(found->graphemes.begin(), found->graphemes.end(),
                       [letters](const WordGrapheme& g) { return g.letters == letters; });
}

[[nodiscard]] SoundRow sound_row(const StoreData& data, const PhaseIndex& index,
                                 const std::string& letters) {
    SoundRow row;
    row.letters = letters;

    for (const PhaseWord& word : index.words()) {
        if (uses(index, word.word, letters)) {
            ++row.words_in_phase;
        }
    }

    // Boards: only those of a phonics board hold sounds, and only the words the
    // store listed for them can be said to have been on one.
    for (const StoredBoard& board : data.boards) {
        if (board.phase.empty()) {
            continue;
        }
        const bool met = std::any_of(board.viable.begin(), board.viable.end(),
                                     [&](const std::string& w) { return uses(index, w, letters); });
        if (!met) {
            continue;
        }
        ++row.boards_met;
        const bool found =
            std::any_of(data.finds.begin(), data.finds.end(), [&](const FindRecord& f) {
                return f.board == board.serial && uses(index, f.word, letters);
            });
        if (found) {
            ++row.boards_found;
        }
    }

    // Words: found at least once, ever, and found once without the start tile.
    for (const VaultWord& word : data.vault) {
        if (!uses(index, word.word, letters)) {
            continue;
        }
        ++row.words_found;
        if (word.unassisted > 0) {
            ++row.words_unassisted;
        }
    }
    row.often_found = row.words_unassisted >= often_found_words;
    return row;
}

}  // namespace

std::vector<std::string> focus_sound_choices(const PhaseIndex& index) {
    std::vector<std::string> choices;
    for (const Grapheme& grapheme : index.graphemes()) {
        // A grapheme with several sounds is several lines; the parent ticks the
        // letters once.
        if (std::find(choices.begin(), choices.end(), grapheme.letters) == choices.end()) {
            choices.push_back(grapheme.letters);
        }
    }
    return choices;
}

Settings restrict_focus_sounds(Settings settings, const PhaseIndex& index) {
    const std::vector<std::string> choices = focus_sound_choices(index);
    std::erase_if(settings.focus_sounds, [&choices](const std::string& sound) {
        return std::find(choices.begin(), choices.end(), sound) == choices.end();
    });
    return settings.normalized();
}

Dashboard build_dashboard(const StoreData& data, const PhaseIndex* index, Day today,
                          int first_day_of_week) {
    auto zone = profiling::zone<"data.build_dashboard">();

    Dashboard dashboard;
    dashboard.empty =
        data.boards.empty() && data.finds.empty() && data.stars == 0 && data.vault.empty();

    // This week: from the first day of the week the calendar locale names,
    // through the seventh. Boards and stars from the tallies of the days (so
    // that a busy week is not cut short by the finds the store has dropped);
    // words from the vault.
    WeekSummary& week = dashboard.week;
    week.first = week_start(today, first_day_of_week);
    week.last = week.first + 6;
    for (const DayTally& tally : data.days) {
        if (in_week(tally.day, week)) {
            week.boards_cleared += static_cast<int>(tally.boards_cleared);
            week.stars += static_cast<int>(tally.stars);
        }
    }
    for (const VaultWord& word : data.vault) {
        if (in_week(word.first_day, week)) {
            ++week.new_words;
        }
        if (in_week(word.last_day, week) &&
            (word.word.size() > week.longest_word.size() ||
             (word.word.size() == week.longest_word.size() && word.word < week.longest_word))) {
            week.longest_word = word.word;
        }
    }

    for (const VaultWord& word : data.vault) {
        dashboard.vault.push_back({word.word, word.first_day});
    }
    for (const BestStars& best : data.best) {
        if (best.stars > 0) {
            dashboard.best.push_back({best.size, best.stars});
        }
    }

    if (index != nullptr) {
        dashboard.has_phase_data = true;
        dashboard.phase = index->phase();
        for (const std::string& letters : focus_sound_choices(*index)) {
            SoundRow row = sound_row(data, *index, letters);
            row.focus =
                std::find(data.settings.focus_sounds.begin(), data.settings.focus_sounds.end(),
                          letters) != data.settings.focus_sounds.end();
            row.few_words = row.focus && row.words_in_phase < few_words_threshold;
            dashboard.sounds.push_back(std::move(row));
        }
    }
    zone.Value(dashboard.sounds.size());
    return dashboard;
}

}  // namespace wjs
