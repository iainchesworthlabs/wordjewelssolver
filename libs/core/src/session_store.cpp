// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/session_store.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ascii.hpp"
#include "wjs/profiling.hpp"

namespace wjs {

namespace {

using ascii::lower;
using ascii::upper;

[[nodiscard]] std::optional<Day> day_of(Stamp stamp) {
    if (stamp < std::numeric_limits<Day>::min() || stamp > std::numeric_limits<Day>::max()) {
        return std::nullopt;
    }
    return static_cast<Day>(stamp);
}

// The session's own text for tiles: a space between tiles, '/' between rows. The
// store writes commas between tiles; text that already has spaces is unchanged.
[[nodiscard]] std::string session_tiles(std::string_view stored) {
    std::string tiles{stored};
    std::replace(tiles.begin(), tiles.end(), ',', ' ');
    return tiles;
}

[[nodiscard]] std::optional<SavedState> saved_state_of(SessionState state) {
    switch (state) {
        case SessionState::box:
            return SavedState::box;
        case SessionState::playing:
            return SavedState::playing;
        case SessionState::resting:
            return SavedState::resting;
        case SessionState::shaking:
        case SessionState::dropping:
        case SessionState::cleared:
            return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] SessionState session_state_of(SavedState state) {
    switch (state) {
        case SavedState::box:
            return SessionState::box;
        case SavedState::playing:
            return SessionState::playing;
        case SavedState::resting:
            return SessionState::resting;
    }
    return SessionState::box;
}

}  // namespace

std::optional<BoardSnapshot> snapshot_of(const BoardRecord& record) {
    auto zone = profiling::zone<"store.session_snapshot">();

    // The store tells a standard board by its empty phase, so a record whose mode
    // and phase disagree would be kept as the other board.
    if ((record.mode == BoardMode::standard) != record.phase.empty()) {
        return std::nullopt;
    }
    BoardSnapshot snapshot;
    snapshot.phase = record.phase;
    snapshot.size = static_cast<int>(side(record.size));
    snapshot.tiles = record.tiles;
    snapshot.viable = record.viable;
    snapshot.viable_total = static_cast<int>(record.viable_count());
    for (const Find& find : record.finds) {
        const std::optional<Day> day = day_of(find.when);
        if (!day) {
            return std::nullopt;
        }
        snapshot.finds.push_back({find.word, find.assisted, *day, find.reward().stars});
    }
    zone.Value(snapshot.finds.size());
    return snapshot;
}

std::optional<BoardRecord> record_of(const BoardSnapshot& snapshot) {
    auto zone = profiling::zone<"store.session_record">();

    if (snapshot.size < 0) {
        return std::nullopt;
    }
    const std::optional<BoardSize> size = board_size_for(static_cast<std::size_t>(snapshot.size));
    if (!size || snapshot.viable_total < 0) {
        return std::nullopt;
    }

    BoardRecord record;
    record.size = *size;
    record.tiles = session_tiles(snapshot.tiles);
    if (snapshot.phase.empty()) {
        // A standard board: the store lists none of its words and keeps how many
        // there were, which is what the session checks the board against.
        const std::size_t total = snapshot.viable_total != 0
                                      ? static_cast<std::size_t>(snapshot.viable_total)
                                      : snapshot.viable.size();
        if (total == 0) {
            return std::nullopt;
        }
        record.mode = BoardMode::standard;
        record.viable_total = total;
    } else {
        // A phonics board: the session needs every word the board held, and the
        // store lists them only up to a bound: a board whose list is not the whole
        // of it cannot be brought back.
        if (snapshot.viable.empty() ||
            (snapshot.viable_total != 0 &&
             static_cast<std::size_t>(snapshot.viable_total) != snapshot.viable.size())) {
            return std::nullopt;
        }
        record.mode = BoardMode::phonics;
        record.phase = snapshot.phase;
        for (const std::string& word : snapshot.viable) {
            record.viable.push_back(upper(word));
        }
        std::sort(record.viable.begin(), record.viable.end());
    }
    for (const FoundSnapshot& find : snapshot.finds) {
        record.finds.push_back({upper(find.word), find.assisted, static_cast<Stamp>(find.day)});
    }
    zone.Value(record.finds.size());
    return record;
}

std::optional<SavedSession> saved_session_of(const SessionSnapshot& snapshot) {
    const std::optional<SavedState> state = saved_state_of(snapshot.state);
    if (!state) {
        return std::nullopt;
    }
    SavedSession saved;
    saved.state = *state;
    saved.size = static_cast<int>(side(snapshot.size));
    saved.seed = snapshot.seed;
    if (snapshot.board) {
        saved.board = snapshot_of(*snapshot.board);
        if (!saved.board) {
            return std::nullopt;
        }
    }
    if (snapshot.lit) {
        saved.lit = lower(*snapshot.lit);
    }
    return saved;
}

std::optional<SessionSnapshot> session_snapshot_of(const SavedSession& saved) {
    if (saved.size < 0) {
        return std::nullopt;
    }
    const std::optional<BoardSize> size = board_size_for(static_cast<std::size_t>(saved.size));
    if (!size) {
        return std::nullopt;
    }
    SessionSnapshot snapshot;
    snapshot.state = session_state_of(saved.state);
    snapshot.size = *size;
    snapshot.seed = saved.seed;
    if (saved.board) {
        snapshot.board = record_of(*saved.board);
        if (!snapshot.board) {
            return std::nullopt;
        }
        snapshot.mode = snapshot.board->mode;
    }
    if (saved.lit) {
        snapshot.lit = upper(*saved.lit);
    }
    return snapshot;
}

}  // namespace wjs
