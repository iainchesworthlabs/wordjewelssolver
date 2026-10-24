// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/calendar.hpp"
#include "wjs/settings.hpp"

// The store: what the application keeps between runs, on the device and
// nowhere else (docs/design/dashboard.md, "The store"; docs/design/path.md,
// "What the MVP must leave open").
//
//   - Small. A few kilobytes to begin with, and never more than max_store_bytes:
//     every list in it has a bound, so a file that large is damaged.
//   - Local. The library opens one file, named by the caller, and writes
//     nothing else. There is no function here that writes to another path, and
//     none that sends or shares: the file is read by this program only.
//   - Versioned and migrated. The first line says which version wrote the file.
//     An older file is migrated up a step at a time and the file it came from is
//     kept beside it; a newer file is left alone and read by nobody.
//   - Safe against damage. A file that cannot be read is set aside, never
//     overwritten and never deleted, and the program goes on with an empty
//     store; it is told which of these happened (LoadStatus).
//   - Written whole and swapped in (a temporary file, then a rename), so that
//     a program stopped half way through leaves the old file or the new one.
//     The limit: the bytes are flushed to the operating system, not forced to
//     the disk (that takes fsync or FlushFileBuffers, an operating system call
//     this library does not make). A power cut at the wrong moment can leave a
//     file that is not whole; the checksum finds it, it is set aside as
//     unreadable (never read, never deleted) and the program begins with an
//     empty store.
//
// What it holds, and, as important, what it does not: for each find the word,
// the board's size, the phase, the day, whether the start tile was shown for
// that word and which board it was on; for each of the last twenty boards its
// tiles and size, how many words it held, and, on a phonics board, which; the
// stars, the best board for each size, the words found at least once (the
// vault), a tally for each of the last days, the settings, and the game in
// progress (so that a restart is not a lost board). **Not** a duration, a time
// of day, an identity, a location, an image or a sound: a day is the finest
// time there is, and nothing in the file could make a figure for time on
// screen.
//
// The game session (wjs/game_session.hpp, a separate piece of work) hands the
// store plain values and takes plain values back, and every type here that it
// meets is a plain struct of strings, numbers and flags with the field names of
// its own (BoardSnapshot mirrors its BoardRecord, SavedSession its
// SessionSnapshot), so that a copy from one to the other is all an adapter is.
// A find's moment is a Day: the days since 1970-01-01 in the device's local
// calendar (wjs/calendar.hpp), which is the unit the caller gives the session
// as `Stamp`. The dashboard reads the data (data()) and computes from it
// (wjs/dashboard.hpp).
//
// Words are kept lower case, as the phase data and the tray write them; a word
// handed in in capitals is lower-cased. A phase is empty on a standard board.

namespace wjs {

inline constexpr int store_format_version = 3;

inline constexpr std::string_view store_directory_name = "WordJewelsSolver";
inline constexpr std::string_view store_file_name = "store.wjs";

// The bounds. Past a bound the oldest goes (the vault's least recently met), so
// the file cannot grow without limit.
inline constexpr std::size_t max_stored_boards = 20;  // "the last twenty boards"
inline constexpr std::size_t max_stored_finds = 300;
inline constexpr std::size_t max_vault_words = 1000;
inline constexpr std::size_t max_stored_days = 35;  // five weeks of tallies
// A phonics board holds three to five words and, where seeding fell back, a few
// more; a standard board holds hundreds and lists none.
inline constexpr std::size_t max_listed_viable_words = 24;
inline constexpr std::size_t store_min_word_letters = 3;
inline constexpr std::size_t store_max_word_letters = 20;
inline constexpr int max_find_stars = 20;

// The most a store file can be: bigger than the bounds above can make it, so a
// file past it is damaged, and refused before it is read. The very worst case
// (every list full, every word and name as long as it may be, a game in
// progress as large) is about 107 KiB of the 128; a test pins it and keeps at
// least 16 KiB of headroom, which is there so that a field added to a record
// does not turn a store a user legitimately has into "damaged". Raise the cap
// with the bounds, never to let a file of unknown size through.
inline constexpr std::size_t max_store_bytes = std::size_t{128} * 1024;

// Where the program's file is, under the base directory the platform layer
// names (wjs::platform::user_data_directory()).
[[nodiscard]] std::filesystem::path default_store_path(const std::filesystem::path& base);

// ---------------------------------------------------------------------------
// The records

struct FindRecord {
    std::string word;  // lower case, 3 to 20 letters
    int stars = 0;     // what the find earned
    int board_size = 0;
    std::string phase;  // empty on the standard board
    Day day = 0;
    // The start tile was shown for this word on this board ("assisted"). The
    // only help in the MVP, so "unassisted" is exact (dashboard.md).
    bool start_tile_used = false;
    std::uint32_t board = 0;  // the StoredBoard it was found on

    friend bool operator==(const FindRecord&, const FindRecord&) = default;
};

// A word found at least once on this device: its sticker in the vault.
struct VaultWord {
    std::string word;
    Day first_day = 0;
    Day last_day = 0;
    std::uint32_t found = 0;       // finds
    std::uint32_t unassisted = 0;  // of them, without the start tile

    friend bool operator==(const VaultWord&, const VaultWord&) = default;
};

// What a day came to. Kept for the last max_stored_days, because "this week"
// must not depend on how many boards or finds are still held individually.
struct DayTally {
    Day day = 0;
    std::uint32_t boards_cleared = 0;
    std::uint32_t stars = 0;

    friend bool operator==(const DayTally&, const DayTally&) = default;
};

// A board of the history, as the store keeps it. The finds on it are in
// StoreData::finds (finds_on_board).
struct StoredBoard {
    std::uint32_t serial = 0;
    Day day = 0;  // the day it was first recorded
    int size = 0;
    std::string phase;  // empty on the standard board
    // "A,B,QU/C,D,E": the tiles by commas and the rows by slashes, which
    // Board::parse reads back.
    std::string tiles;
    int viable_total = 0;  // how many words the board held
    // A phonics board lists its words (at most max_listed_viable_words): the
    // dashboard's "a word with this sound was on a board" needs to know which. A
    // standard board lists none.
    std::vector<std::string> viable;
    int found = 0;  // finds recorded on it
    int stars = 0;
    bool cleared = false;

    friend bool operator==(const StoredBoard&, const StoredBoard&) = default;
};

struct BestStars {
    int size = 0;
    int stars = 0;  // the most stars on one cleared board of this size

    friend bool operator==(const BestStars&, const BestStars&) = default;
};

// ---------------------------------------------------------------------------
// What the game session hands over, and takes back

// A find on a board in play.
struct FoundSnapshot {
    std::string word;       // any case, letters a-z
    bool assisted = false;  // the start tile had been shown for this word
    Day day = 0;            // the day it was found (the session's Stamp)
    int stars = 0;          // what it earned: the game's own reward for its tier

    friend bool operator==(const FoundSnapshot&, const FoundSnapshot&) = default;
};

// One board at a moment: its tiles, what it held, and every word found so far
// in the order found. Sent after each find and when the board ends, however it
// ends, and the same board's snapshot can be sent again and again: only what is
// new is recorded (a find once, however often it is sent).
struct BoardSnapshot {
    std::string phase;  // empty on a standard board
    int size = 0;       // the side
    // As Board::parse reads them: "S A T/P I N/B B B". A dead jewel carries the
    // phase's dead letter.
    std::string tiles;
    // Every word of the phase on the board, in the order the caller keeps them.
    // A board that holds more than max_listed_viable_words is not listed in the
    // history and cannot be resumed.
    std::vector<std::string> viable;
    // How many words the board held, when the list above is not all of them
    // (a standard board lists none). 0 means viable.size().
    int viable_total = 0;
    std::vector<FoundSnapshot> finds;

    friend bool operator==(const BoardSnapshot&, const BoardSnapshot&) = default;
};

// Where a saved game was, in the session's own states.
enum class SavedState {
    box,
    playing,
    resting,
};

[[nodiscard]] std::string_view to_string(SavedState state) noexcept;

// The game in progress, so that a restart brings it back: the state, the size
// chosen on the box, the seed the next board is dealt from, the board in play
// and the word whose start tile is lit.
struct SavedSession {
    SavedState state = SavedState::box;
    int size = 3;
    std::uint32_t seed = 1;
    std::optional<BoardSnapshot> board;  // set in playing and resting
    std::optional<std::string> lit;      // lower case; only while playing

    friend bool operator==(const SavedSession&, const SavedSession&) = default;
};

// ---------------------------------------------------------------------------
// All of it

struct StoreData {
    Settings settings;
    std::uint64_t stars = 0;
    std::uint32_t next_board = 1;
    std::vector<BestStars> best;      // by size
    std::vector<VaultWord> vault;     // by word
    std::vector<FindRecord> finds;    // oldest first
    std::vector<DayTally> days;       // by day
    std::vector<StoredBoard> boards;  // oldest first
    std::optional<SavedSession> session;

    friend bool operator==(const StoreData&, const StoreData&) = default;
};

// The finds recorded on one board, in the order found. The store keeps the last
// max_stored_finds, so a board of long ago may show fewer than it had.
[[nodiscard]] std::vector<FindRecord> finds_on_board(const StoreData& data, std::uint32_t serial);

// ---------------------------------------------------------------------------
// The file: text, one record to a line, ended by a checksum

// A file read as lines of words, before it is understood. A migration works on
// this, so that an old layout can be rewritten without the program still
// carrying the code that understood it.
struct RawLine {
    std::string tag;
    std::vector<std::string> fields;
};

struct RawStore {
    int version = 0;
    std::vector<RawLine> lines;  // between the header and the checksum
};

// Rewrites a file of version `from` as one of version from + 1. False when it
// cannot (the file is then treated as damaged). Added to the list below with
// the change of format that needs it; the list is checked to run without a gap
// from 1 to store_format_version.
struct MigrationStep {
    int from = 0;
    std::function<bool(RawStore&)> apply;
};

// The migrations of this program: from version 1, which let the saved game in
// progress be a standard board, and from version 2, which renamed the language
// "zh" to "zh_CN" (store_format.cpp says what each step does). A test supplies
// its own to hold the mechanism down.
[[nodiscard]] std::span<const MigrationStep> builtin_migrations() noexcept;

enum class StoreError {
    too_large,        // more than max_store_bytes
    corrupt,          // not a store file, a line it cannot read, or a checksum that does not match
    newer_version,    // written by a later version of the program
    no_migration,     // an older version with no migration up to this one
    migration_failed  // a migration refused the file
};

[[nodiscard]] std::string_view to_string(StoreError error) noexcept;

struct ParsedStore {
    StoreData data;
    int from_version = store_format_version;  // the version the text was written by
};

// Understands a file's text: checks the size, the header and the checksum,
// migrates it up to `target_version` with `steps`, and reads every record,
// which is checked and brought within its bounds. Never throws and never reads
// past the text. The target is this program's version; a test names another to
// hold the mechanism down while there is only one real version.
[[nodiscard]] std::expected<ParsedStore, StoreError> parse_store(
    std::string_view text, std::span<const MigrationStep> steps = builtin_migrations(),
    int target_version = store_format_version);

// The text of a store, headed with `version`. A pure function of the data, so
// that the same data is the same bytes.
[[nodiscard]] std::string serialize_store(const StoreData& data,
                                          int version = store_format_version);

// ---------------------------------------------------------------------------
// The store, in memory and (optionally) on disk

struct RecordOutcome {
    // False if the snapshot was refused (a board that is not size by size, a
    // word that is not a word of the store's bounds): nothing was recorded.
    bool stored = false;
    std::uint32_t board = 0;
    int stars_added = 0;
    // Words found for the first time on this device, in the order found.
    std::vector<std::string> new_words;
    // The board has just been cleared with more stars than the best of its size,
    // and there was a best to beat. Meant for one sparkle (game.md); never shown
    // as a number to the child.
    bool new_best = false;
};

enum class LoadStatus {
    created,        // there was no file: a new, empty store
    loaded,         // the file was read as it is
    migrated,       // an older file was read; `kept` is the copy of it
    unreadable,     // the file was damaged: set aside as `kept`, and the store is new
    newer_version,  // written by a later version: left alone, and this store is not saved
    cannot_read,    // the file could not be opened or read: left alone, and not saved
};

enum class SaveError {
    read_only,     // the store was not read from, or does not own, its file
    cannot_write,  // the disk refused
};

struct OpenResult;

class Store {
    friend struct OpenResult;

public:
    // A store that is never saved: for tests and for a program with nowhere to
    // keep one.
    [[nodiscard]] static Store in_memory();

    // The store in `file`. Never throws, and never loses the file: see the
    // header comment and LoadStatus. A missing directory is made when the first
    // save needs it.
    [[nodiscard]] static OpenResult open(
        const std::filesystem::path& file,
        std::span<const MigrationStep> steps = builtin_migrations(),
        int target_version = store_format_version);

    [[nodiscard]] const StoreData& data() const noexcept { return data_; }
    [[nodiscard]] const Settings& settings() const noexcept { return data_.settings; }

    // Replaces the settings (brought to values the table allows) and saves.
    void update_settings(const Settings& settings);

    // Records what a snapshot shows that is new, and saves. A board is the same
    // board as the one before it when its phase, size and tiles are, and the
    // finds it has are the ones now sent, in the same order and on the same days
    // (the newest the store keeps are compared) with perhaps more after them;
    // any other is a new board, so a small phase that deals the same tiles again
    // loses none of the second game's finds. A second game that finds the same
    // words in the same order on the same day cannot be told from the first sent
    // again. `today` is the day a board is first seen and the day it is cleared;
    // each find has its own. `stored` in the outcome is false if the snapshot
    // was refused; whether the disk took it is last_save_error().
    [[nodiscard]] RecordOutcome record(const BoardSnapshot& snapshot, Day today);

    // The game in progress, to be brought back after a restart, or none. Refused
    // (false, nothing changed) if the board cannot be kept whole: a board of
    // another shape than size by size, a word that is not a word, a phonics
    // board of more words than max_listed_viable_words (restoring needs every
    // one), or a standard board that says no count of its words. A standard
    // board is kept by the count and none of its words. False too when the
    // store is on a file and the disk refused it (last_save_error()): the game
    // is then held in memory (session()), not on disk, and the next change
    // writes it again.
    [[nodiscard]] bool save_session(const std::optional<SavedSession>& session);
    [[nodiscard]] const std::optional<SavedSession>& session() const noexcept {
        return data_.session;
    }

    // The two steps of Reset (dashboard.md): the first clears the boards and
    // finds (and the day tallies, which are made of them, and any game in
    // progress); the second, kept apart so that it needs its own deliberate step,
    // clears the stars, the best and the stickers. Neither touches the settings.
    void reset_boards_and_finds();
    void reset_stars_and_stickers();

    // The best stars on one cleared board of this size; 0 if none.
    [[nodiscard]] int best_stars(int board_size) const noexcept;

    // Whether this store is written to a file at all: false for in_memory(),
    // and for a store opened from a file it must not overwrite.
    [[nodiscard]] bool persistent() const noexcept { return persistent_; }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

    // Writes the file now (every change above already does). The reason it did
    // not, if it did not: a full disk is the program's to ignore, not to stop for.
    [[nodiscard]] std::expected<void, SaveError> save();
    [[nodiscard]] std::optional<SaveError> last_save_error() const noexcept {
        return last_save_error_;
    }

private:
    Store() = default;

    void persist();
    StoredBoard* find_board(std::uint32_t serial) noexcept;
    // The tally of a day, made if there is none; null if the day is older than
    // every one held and there is no room for it.
    DayTally* tally_for(Day day);

    StoreData data_;
    std::filesystem::path file_;
    int format_version_ = store_format_version;
    bool persistent_ = false;
    std::optional<SaveError> last_save_error_;
};

struct OpenResult {
    Store store;
    LoadStatus status = LoadStatus::created;
    // The file that was kept: the old version's copy when migrated, the damaged
    // file's new name when unreadable. Empty otherwise.
    std::filesystem::path kept;
};

}  // namespace wjs
