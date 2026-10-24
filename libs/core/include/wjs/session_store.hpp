// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "wjs/game_session.hpp"
#include "wjs/store.hpp"

// The copy between the game session's plain values (wjs/game_session.hpp) and the
// store's (wjs/store.hpp). They are two sets of strings, numbers and flags with the
// same fields on purpose, so that neither header needs the other; this is the one
// place that knows both.
//
//   BoardRecord     <->  BoardSnapshot   a board as the history and the dashboard keep it
//   SessionSnapshot <->  SavedSession    the game in progress, to be brought back
//
// What changes on the way, and why:
//
//   - **Words and tiles.** The session writes words in upper case and its tiles as
//     "S A T/P I N/B B B"; the store keeps words in lower case and its tiles as
//     "S,A,T/P,I,N/B,B,B". Going to the store needs no care (the store brings what it
//     is given to its own form). Coming back, `GameSession::restore()` compares the
//     recorded board with its own re-solve **exactly**, so the words go back in upper
//     case and in alphabetical order and the tiles back with a space between tiles, or
//     it answers `board_changed`.
//   - **Stars.** The session never stores them (they follow from a word's length); the
//     store keeps the stars each find earned, so the history is not rewritten if the
//     reward table changes. They are taken from each find's `reward()` going in and are
//     not read coming back.
//   - **When.** A find's `Stamp` is a `Day` (days since 1970-01-01 in the device's
//     calendar, wjs/calendar.hpp): the session never reads it and the store keeps a
//     day and nothing finer. A stamp that is not a day (outside 32 bits) cannot be
//     stored, and the conversion says so.
//   - **The list of words a board held.** The store lists a phonics board's words (it
//     needs them to say which sounds a board had) and the session needs every one of
//     them to restore; a record whose list was not kept (more words than the store
//     lists) cannot come back as a session.
//   - **The board mode.** The store keeps none: a board with a phase is a phonics
//     board, and a board with an empty phase is a standard one. A record whose mode
//     and phase disagree cannot be stored (`snapshot_of` answers nullopt). A standard
//     board holds tens to hundreds of words and neither side lists them: the record
//     and the store's board both say how many (`viable_total`), which is what
//     `restore()` checks the board against. A saved game in the box has no board to
//     tell the mode from, and comes back with the default; `restore()` plays the mode
//     it is given.
//
// Each function answers nullopt, and changes nothing, for what the other side cannot
// hold. A state of the session that is not saved (shaking, dropping, cleared) has no
// SavedSession: the session itself saves those as the box and resting.

namespace wjs {

// A board as the store takes it (Store::record).
[[nodiscard]] std::optional<BoardSnapshot> snapshot_of(const BoardRecord& record);

// A board as the session takes it back (SessionSnapshot::board). nullopt for a size
// the session has no name for, a board with no list of its words, or a list that is
// not the whole of what the board held.
[[nodiscard]] std::optional<BoardRecord> record_of(const BoardSnapshot& snapshot);

// The game in progress as the store takes it (Store::save_session).
[[nodiscard]] std::optional<SavedSession> saved_session_of(const SessionSnapshot& snapshot);

// The game in progress as the session takes it back (GameSession::restore).
[[nodiscard]] std::optional<SessionSnapshot> session_snapshot_of(const SavedSession& saved);

}  // namespace wjs
