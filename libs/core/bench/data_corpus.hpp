// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the data metrics (data_metrics.cpp) are measured on: a store that twenty
// played boards left, the heaviest store the format allows, the test phase as a
// directory, and a session in `playing` to submit paths to. All of it built the
// same way on every machine, from the corpus the other rows use.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "metrics.hpp"
#include "wjs/calendar.hpp"
#include "wjs/game_session.hpp"
#include "wjs/store.hpp"

namespace wjs::perf {

// A directory of its own under the system's temporary one, removed with it.
class TempDirectory {
public:
    TempDirectory();
    ~TempDirectory();
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

// The store keeps the last twenty boards (wjs::max_stored_boards): this many are
// played, to the resting screen, and recorded.
inline constexpr unsigned kKeptBoards = 20;

// The first of the days they are recorded on, three boards to a day.
inline constexpr wjs::Day kKeptFirstDay = wjs::day_from_civil(2026, 10, 12);

// The store a mode's twenty boards left. The standard mode plays the four sizes in
// turn (3x3 to 6x6) and the phonics mode its two (3x3 and 4x4); the seeds are the
// corpus's.
struct KeptStore {
    std::vector<wjs::BoardSnapshot> snapshots;  // in the order recorded
    wjs::StoreData data;                        // what Store::record made of them
    std::string text;                           // serialize_store(data)
};
[[nodiscard]] const KeptStore& kept_store(BoardMode mode);

// A store with every record the format keeps at its largest: twenty boards of 6x6
// each listing 24 words of twenty letters, 300 finds, 1,000 vault words, 35 days.
// The heaviest file a program can be asked to write or read (the bound is
// wjs::max_store_bytes).
[[nodiscard]] wjs::StoreData heaviest_store();

// What recording the kept boards one at a time into a store with a file writes: the
// store rewrites its file after every change.
struct StoreWrites {
    std::uint64_t bytes = 0;         // the sizes of the file after each change, added
    std::uint64_t rewrites = 0;      // the changes
    std::uint64_t final_bytes = 0;   // the file at the end
    bool final_equals_text = false;  // and it is exactly serialize_store of the data
};
[[nodiscard]] StoreWrites write_through(const KeptStore& kept);

// The test phase (phonics_phase) as the two files of a data directory.
class PhaseDirectory {
public:
    PhaseDirectory();

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return directory_.path(); }
    // The size of the two files together.
    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }

private:
    TempDirectory directory_;
    std::uint64_t bytes_ = 0;
};

// A session of one mode in `playing`, with one word found, and two paths to
// submit again and again without changing it: the found word (a find again) and a
// run of tiles that spells nothing the board holds.
class PlayingSession {
public:
    PlayingSession(BoardMode mode, std::size_t size, unsigned seed_index);

    [[nodiscard]] wjs::TraceKind submit_miss(wjs::Stamp when);
    [[nodiscard]] wjs::TraceKind submit_again(wjs::Stamp when);
    [[nodiscard]] const wjs::GameSession& session() const noexcept { return *session_; }
    [[nodiscard]] wjs::TraceKind miss_kind() const noexcept { return miss_kind_; }

private:
    std::unique_ptr<wjs::GameSession> session_;
    std::vector<wjs::Position> found_path_;
    std::vector<wjs::Position> miss_path_;
    wjs::TraceKind miss_kind_ = wjs::TraceKind::ignored;
};

}  // namespace wjs::perf
