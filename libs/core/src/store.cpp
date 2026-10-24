// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/store.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/profiling.hpp"

namespace wjs {

namespace {

// ---- the files ------------------------------------------------------------

// A name beside `file` that nothing has, made from `file`'s name and a suffix:
// store.wjs.unreadable, then store.wjs.unreadable.1, and so on. Empty if the
// first hundred are taken, in which case the caller leaves the file where it is.
[[nodiscard]] std::filesystem::path unused_sibling(const std::filesystem::path& file,
                                                   const std::string& suffix) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        // concat appends to the path's own native characters. Going through
        // filename().string() would narrow them to the legacy code page on
        // Windows, and a name outside it would not be the same file.
        std::filesystem::path candidate = file;
        candidate.concat(suffix);
        if (attempt > 0) {
            candidate.concat('.' + std::to_string(attempt));
        }
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return std::nullopt;
    }
    return text;
}

// ---- the records ----------------------------------------------------------

[[nodiscard]] std::optional<std::string> stored_word(std::string_view word) {
    if (word.size() < store_min_word_letters || word.size() > store_max_word_letters) {
        return std::nullopt;
    }
    std::string lower;
    lower.reserve(word.size());
    for (const char c : word) {
        if (c >= 'A' && c <= 'Z') {
            lower += static_cast<char>(c - 'A' + 'a');
        } else if (c >= 'a' && c <= 'z') {
            lower += c;
        } else {
            return std::nullopt;
        }
    }
    return lower;
}

// The tiles as the store writes them: by commas, and the rows by slashes, which
// Board::parse reads back. Empty if the text is not a board of this size.
[[nodiscard]] std::string canonical_tiles(std::string_view text, int size) {
    const auto board = Board::parse(text);
    if (!board || size < 1 || board->rows() != static_cast<std::size_t>(size) ||
        board->columns() != static_cast<std::size_t>(size)) {
        return {};
    }
    std::string out;
    for (std::size_t row = 0; row < board->rows(); ++row) {
        if (row != 0) {
            out += '/';
        }
        for (std::size_t column = 0; column < board->columns(); ++column) {
            if (column != 0) {
                out += ',';
            }
            out += board->tile({row, column});
        }
    }
    return out;
}

// A snapshot checked and brought to the store's own form: tiles written one
// way, words lower case, stars within their bound. Nullopt if anything in it is
// not something the store can keep, so that a snapshot is taken whole or not at
// all.
struct CheckedBoard {
    std::string tiles;
    std::vector<std::string> viable;   // lower case; empty on a standard board and past the bound
    bool listed = false;               // the board's whole list is held
    int viable_total = 0;              // how many words the board held
    std::vector<FoundSnapshot> finds;  // words lower case
};

[[nodiscard]] std::optional<CheckedBoard> check_board(const BoardSnapshot& snapshot) {
    CheckedBoard checked;
    checked.tiles = canonical_tiles(snapshot.tiles, snapshot.size);
    if (checked.tiles.empty() || snapshot.viable_total < 0 || snapshot.viable_total > 100'000 ||
        (!snapshot.phase.empty() && !is_valid_name(snapshot.phase))) {
        return std::nullopt;
    }
    for (const std::string& word : snapshot.viable) {
        auto stored = stored_word(word);
        if (!stored) {
            return std::nullopt;
        }
        checked.viable.push_back(std::move(*stored));
    }
    checked.viable_total = snapshot.viable_total > 0 ? snapshot.viable_total
                                                     : static_cast<int>(snapshot.viable.size());
    // Only a phonics board lists its words, and only a handful: a board that
    // holds more than that, or is a standard board, holds hundreds.
    checked.listed = !snapshot.phase.empty() && !checked.viable.empty() &&
                     checked.viable.size() <= max_listed_viable_words &&
                     std::cmp_equal(checked.viable.size(), checked.viable_total);
    if (snapshot.phase.empty() || checked.viable.size() > max_listed_viable_words) {
        checked.viable.clear();
    }
    for (const FoundSnapshot& entry : snapshot.finds) {
        auto word = stored_word(entry.word);
        if (!word || entry.stars < 0) {
            return std::nullopt;
        }
        FoundSnapshot find = entry;
        find.word = std::move(*word);
        find.stars = std::min(find.stars, max_find_stars);
        checked.finds.push_back(std::move(find));
    }
    return checked;
}

// Whether `snapshot` is a later picture of `last`, the board recorded before,
// and not a second game on the same tiles. The tiles, phase and size are the
// same, and so are the finds already recorded: the snapshot lists every find so
// far in order, so the first `last.found` of them are the ones the store has.
// The store holds the last max_stored_finds, which for the board in play are
// its newest, so those are the ones compared; a find has its word and its day.
// What cannot be told apart is a second game that finds the same words, in the
// same order, on the same day: it counts as the first sent again.
[[nodiscard]] bool continues(const StoreData& data, const StoredBoard& last,
                             const BoardSnapshot& snapshot, const CheckedBoard& checked) {
    if (last.phase != snapshot.phase || last.size != snapshot.size || last.tiles != checked.tiles ||
        static_cast<std::size_t>(last.found) > checked.finds.size()) {
        return false;
    }
    std::size_t held = 0;
    for (const FindRecord& find : data.finds) {
        if (find.board == last.serial) {
            ++held;
        }
    }
    // The finds the store dropped were the board's first.
    std::size_t next =
        static_cast<std::size_t>(last.found) - std::min(held, static_cast<std::size_t>(last.found));
    for (const FindRecord& find : data.finds) {
        if (find.board != last.serial) {
            continue;
        }
        if (next >= checked.finds.size() || checked.finds[next].word != find.word ||
            checked.finds[next].day != find.day) {
            return false;
        }
        ++next;
    }
    return true;
}

}  // namespace

std::string_view to_string(SavedState state) noexcept {
    switch (state) {
        case SavedState::box:
            return "box";
        case SavedState::playing:
            return "playing";
        case SavedState::resting:
            return "resting";
    }
    return "box";
}

std::vector<FindRecord> finds_on_board(const StoreData& data, std::uint32_t serial) {
    std::vector<FindRecord> found;
    std::copy_if(data.finds.begin(), data.finds.end(), std::back_inserter(found),
                 [serial](const FindRecord& f) { return f.board == serial; });
    return found;
}

// ---------------------------------------------------------------------------

Store Store::in_memory() {
    return Store{};
}

OpenResult Store::open(const std::filesystem::path& file, std::span<const MigrationStep> steps,
                       int target_version) {
    auto zone = profiling::zone<"data.load_store">();

    OpenResult result;
    result.store.file_ = file;
    result.store.format_version_ = target_version;
    result.status = LoadStatus::created;

    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (error) {
        // Not knowing whether there is a file is not a reason to write one.
        result.status = LoadStatus::cannot_read;
        return result;
    }
    if (!exists) {
        result.store.persistent_ = true;
        return result;
    }

    // Bigger than the store can be: damaged, and not worth reading into memory.
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    std::optional<std::string> text;
    if (!error && size <= max_store_bytes) {
        text = read_file(file);
    }
    if (error || (size <= max_store_bytes && !text)) {
        result.status = LoadStatus::cannot_read;
        return result;
    }

    std::expected<ParsedStore, StoreError> parsed = std::unexpected(StoreError::too_large);
    if (text) {
        parsed = parse_store(*text, steps, target_version);
    }
    zone.Value(text ? text->size() : 0);

    if (parsed && parsed->from_version == target_version) {
        result.store.data_ = std::move(parsed->data);
        result.store.persistent_ = true;
        result.status = LoadStatus::loaded;
        return result;
    }

    if (parsed) {
        // An older file, understood. Keep it as it was before the next save
        // replaces it; if it cannot be kept, do not replace it at all.
        result.store.data_ = std::move(parsed->data);
        result.status = LoadStatus::migrated;
        const std::filesystem::path copy =
            unused_sibling(file, ".v" + std::to_string(parsed->from_version));
        if (!copy.empty()) {
            std::filesystem::copy_file(file, copy, std::filesystem::copy_options::none, error);
            if (!error) {
                result.kept = copy;
                result.store.persistent_ = true;
            }
        }
        return result;
    }

    if (parsed.error() == StoreError::newer_version) {
        // A later program wrote this. Leave it exactly as it is.
        result.status = LoadStatus::newer_version;
        return result;
    }

    // Damaged. Move it aside (a rename keeps every byte), and begin again.
    result.status = LoadStatus::unreadable;
    const std::filesystem::path aside = unused_sibling(file, ".unreadable");
    if (!aside.empty()) {
        std::filesystem::rename(file, aside, error);
        if (!error) {
            result.kept = aside;
            result.store.persistent_ = true;
        }
    }
    return result;
}

std::expected<void, SaveError> Store::save() {
    auto zone = profiling::zone<"data.save_store">();

    if (!persistent_ || file_.empty()) {
        return std::unexpected(SaveError::read_only);
    }

    const std::string text = serialize_store(data_, format_version_);
    zone.Value(text.size());

    std::error_code error;
    if (file_.has_parent_path()) {
        std::filesystem::create_directories(file_.parent_path(), error);
        if (error) {
            return std::unexpected(SaveError::cannot_write);
        }
    }

    // Whole, then swapped in: a program stopped half way leaves the old file or
    // the new one, not half of either. The bytes are handed to the operating
    // system (flush, then close, whose failure is a failed write too) but not
    // forced to the disk: the library has no way to ask for that without an
    // operating system call, and it includes none (see the header's note on
    // power cuts). A file that does not come out whole fails its checksum and is
    // set aside, never read.
    std::filesystem::path temporary = file_;
    temporary.concat(".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, error);
            return std::unexpected(SaveError::cannot_write);
        }
    }
    std::filesystem::rename(temporary, file_, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return std::unexpected(SaveError::cannot_write);
    }
    return {};
}

void Store::persist() {
    if (!persistent_) {
        return;
    }
    const auto saved = save();
    last_save_error_ = saved ? std::nullopt : std::optional<SaveError>{saved.error()};
}

void Store::update_settings(const Settings& settings) {
    data_.settings = settings.normalized();
    persist();
}

int Store::best_stars(int board_size) const noexcept {
    const auto found =
        std::find_if(data_.best.begin(), data_.best.end(),
                     [board_size](const BestStars& b) { return b.size == board_size; });
    return found == data_.best.end() ? 0 : found->stars;
}

StoredBoard* Store::find_board(std::uint32_t serial) noexcept {
    const auto found = std::find_if(data_.boards.begin(), data_.boards.end(),
                                    [serial](const StoredBoard& b) { return b.serial == serial; });
    return found == data_.boards.end() ? nullptr : &*found;
}

DayTally* Store::tally_for(Day day) {
    auto position = std::lower_bound(data_.days.begin(), data_.days.end(), day,
                                     [](const DayTally& t, Day d) { return t.day < d; });
    std::size_t index = static_cast<std::size_t>(position - data_.days.begin());
    if (position != data_.days.end() && position->day == day) {
        return &data_.days[index];
    }
    if (data_.days.size() >= max_stored_days) {
        if (index == 0) {
            return nullptr;  // older than everything held, and no room
        }
        data_.days.erase(data_.days.begin());
        --index;
    }
    data_.days.insert(data_.days.begin() + static_cast<std::ptrdiff_t>(index), DayTally{day, 0, 0});
    return &data_.days[index];
}

RecordOutcome Store::record(const BoardSnapshot& snapshot, Day today) {
    RecordOutcome outcome;

    // Everything is checked before anything is changed: a snapshot is taken
    // whole or not at all.
    const std::optional<CheckedBoard> checked = check_board(snapshot);
    if (!checked) {
        return outcome;
    }

    // The same board as the last one? Boards are played one at a time and a
    // snapshot comes for the one in play, so only the last is looked at: the
    // same phase, size and tiles, and the finds the store has among the ones now
    // sent (continues). The same tiles with other finds are another game.
    StoredBoard* board = nullptr;
    if (!data_.boards.empty() && continues(data_, data_.boards.back(), snapshot, *checked)) {
        board = &data_.boards.back();
    }
    if (board == nullptr) {
        StoredBoard fresh;
        fresh.serial = data_.next_board++;
        fresh.day = today;
        fresh.size = snapshot.size;
        fresh.phase = snapshot.phase;
        fresh.tiles = checked->tiles;
        fresh.viable_total = checked->viable_total;
        fresh.viable = checked->viable;
        data_.boards.push_back(std::move(fresh));
        if (data_.boards.size() > max_stored_boards) {
            data_.boards.erase(data_.boards.begin());
        }
        board = &data_.boards.back();
    }
    outcome.stored = true;
    outcome.board = board->serial;

    // The finds that are new: the snapshot lists every find so far, in order,
    // and the board remembers how many it has seen.
    for (auto i = static_cast<std::size_t>(board->found); i < checked->finds.size(); ++i) {
        const FoundSnapshot& entry = checked->finds[i];
        const std::string& word = entry.word;

        FindRecord find;
        find.word = word;
        find.stars = entry.stars;
        find.board_size = board->size;
        find.phase = board->phase;
        find.day = entry.day;
        find.start_tile_used = entry.assisted;
        find.board = board->serial;
        data_.finds.push_back(std::move(find));
        if (data_.finds.size() > max_stored_finds) {
            data_.finds.erase(data_.finds.begin());
        }

        data_.stars += static_cast<std::uint64_t>(entry.stars);
        board->stars += entry.stars;
        outcome.stars_added += entry.stars;
        if (DayTally* tally = tally_for(entry.day)) {
            tally->stars += static_cast<std::uint32_t>(entry.stars);
        }

        auto place =
            std::lower_bound(data_.vault.begin(), data_.vault.end(), word,
                             [](const VaultWord& v, const std::string& w) { return v.word < w; });
        if (place != data_.vault.end() && place->word == word) {
            ++place->found;
            if (!entry.assisted) {
                ++place->unassisted;
            }
            place->first_day = std::min(place->first_day, entry.day);
            place->last_day = std::max(place->last_day, entry.day);
        } else {
            place = data_.vault.insert(
                place, VaultWord{word, entry.day, entry.day, 1, entry.assisted ? 0U : 1U});
            outcome.new_words.push_back(word);
            // Room is made by letting go of the word met longest ago, never the
            // one just added.
            if (data_.vault.size() > max_vault_words) {
                const std::size_t added = static_cast<std::size_t>(place - data_.vault.begin());
                std::size_t victim = added == 0 ? 1 : 0;
                for (std::size_t j = 0; j < data_.vault.size(); ++j) {
                    if (j == added) {
                        continue;
                    }
                    const VaultWord& a = data_.vault[j];
                    const VaultWord& b = data_.vault[victim];
                    if (std::tie(a.last_day, a.found) < std::tie(b.last_day, b.found)) {
                        victim = j;
                    }
                }
                data_.vault.erase(data_.vault.begin() + static_cast<std::ptrdiff_t>(victim));
            }
        }
    }
    board->found = std::max(board->found, static_cast<int>(checked->finds.size()));

    // Cleared: every word the board held is found.
    const bool cleared = checked->viable_total > 0 &&
                         checked->finds.size() >= static_cast<std::size_t>(checked->viable_total);
    if (cleared && !board->cleared) {
        board->cleared = true;
        if (DayTally* tally = tally_for(today)) {
            ++tally->boards_cleared;
        }
        // A best is a cleared board's: an abandoned board does not set one, and
        // the first board has nothing to beat.
        const int before = best_stars(board->size);
        if (board->stars > before) {
            outcome.new_best = before > 0;
            const auto slot =
                std::lower_bound(data_.best.begin(), data_.best.end(), board->size,
                                 [](const BestStars& b, int size) { return b.size < size; });
            if (slot != data_.best.end() && slot->size == board->size) {
                slot->stars = board->stars;
            } else {
                data_.best.insert(slot, BestStars{board->size, board->stars});
            }
        }
    }

    persist();
    return outcome;
}

bool Store::save_session(const std::optional<SavedSession>& session) {
    if (!session) {
        data_.session.reset();
        persist();
        return !last_save_error_;
    }

    if (session->size < 1 || std::cmp_greater(session->size, Board::max_dimension)) {
        return false;
    }
    SavedSession kept = *session;
    if (kept.state == SavedState::box) {
        // The lid is on: there is no board, and nothing is lit.
        if (kept.board || kept.lit) {
            return false;
        }
    } else {
        if (!kept.board || kept.board->size != kept.size) {
            return false;
        }
        // Restoring solves the board again and compares: a phonics board has to
        // keep every word it held; a standard board keeps none of them, only
        // how many there were.
        const bool standard = kept.board->phase.empty();
        const std::optional<CheckedBoard> checked = check_board(*kept.board);
        if (!checked || (standard ? (checked->viable_total < 1 || !checked->viable.empty())
                                  : !checked->listed)) {
            return false;
        }
        kept.board->tiles = checked->tiles;
        kept.board->viable = checked->viable;
        kept.board->viable_total = checked->viable_total;
        kept.board->finds = checked->finds;
        if (kept.lit) {
            auto lit = stored_word(*kept.lit);
            if (!lit || kept.state != SavedState::playing) {
                return false;
            }
            // The lit word is one of the board's, which a phonics board's list
            // says and a standard board's count cannot; the session checks the
            // word against its own solve when it restores.
            const bool on_board =
                standard || std::find(checked->viable.begin(), checked->viable.end(), *lit) !=
                                checked->viable.end();
            if (!on_board) {
                return false;
            }
            kept.lit = std::move(lit);
        }
    }
    data_.session = std::move(kept);
    persist();
    return !last_save_error_;
}

void Store::reset_boards_and_finds() {
    data_.boards.clear();
    data_.finds.clear();
    data_.days.clear();
    data_.session.reset();
    persist();
}

void Store::reset_stars_and_stickers() {
    data_.stars = 0;
    data_.best.clear();
    data_.vault.clear();
    persist();
}

}  // namespace wjs
