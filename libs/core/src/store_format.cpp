// SPDX-License-Identifier: GPL-3.0-or-later
// The store file: how StoreData is written as text and read back, how an older
// file is migrated, and how a damaged one is told from a good one. Nothing here
// touches the disk (store.cpp does), so all of it is held to tests on strings.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/profiling.hpp"
#include "wjs/store.hpp"

namespace wjs {

namespace {

constexpr std::string_view header_tag = "wjs-store";
constexpr std::string_view end_tag = "end";

// ---- small readers --------------------------------------------------------

template <typename Integer>
[[nodiscard]] std::optional<Integer> to_integer(std::string_view text) noexcept {
    Integer value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<bool> to_flag(std::string_view text) noexcept {
    if (text == "0") {
        return false;
    }
    if (text == "1") {
        return true;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
}

[[nodiscard]] std::string join(const std::vector<std::string>& parts, char separator) {
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            text += separator;
        }
        text += parts[i];
    }
    return text;
}

// A field that is empty in the data is written "-", so that every field of a
// line is a word.
[[nodiscard]] std::string field(std::string_view value) {
    return value.empty() ? std::string{"-"} : std::string{value};
}

[[nodiscard]] std::string unfield(std::string_view value) {
    return value == "-" ? std::string{} : std::string{value};
}

// FNV-1a, 64 bit: not a defence against anyone, a way to tell a file that was
// written whole from one that was cut, damaged or edited by hand.
[[nodiscard]] std::uint64_t checksum(std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string hex16(std::uint64_t value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text(16, '0');
    for (std::size_t i = 0; i < 16; ++i) {
        text[15 - i] = digits[value & 0xF];
        value >>= 4;
    }
    return text;
}

// A word as the store keeps it: lower case, letters only, within the bounds.
[[nodiscard]] bool is_stored_word(std::string_view word) noexcept {
    return word.size() >= store_min_word_letters && word.size() <= store_max_word_letters &&
           std::all_of(word.begin(), word.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

// ---- the writer -----------------------------------------------------------

void append_line(std::string& out, std::string_view tag, const std::vector<std::string>& fields) {
    out += tag;
    for (const std::string& value : fields) {
        out += ' ';
        out += value;
    }
    out += '\n';
}

[[nodiscard]] std::string flag(bool value) {
    return value ? "1" : "0";
}

[[nodiscard]] std::string settings_line(const Settings& raw) {
    const Settings s = raw.normalized();
    std::string out = "settings";
    const auto put = [&out](std::string_view key, std::string_view value) {
        out += ' ';
        out += key;
        out += '=';
        out += value;
    };
    put("help", to_string(s.help));
    put("board", to_string(s.board));
    put("phase", s.phase);
    put("focus", s.focus_sounds.empty() ? std::string{"-"} : join(s.focus_sounds, ','));
    put("size", std::to_string(s.board_size));
    put("sound_it_out", to_string(s.sound_it_out));
    put("celebration", to_string(s.celebration_sounds));
    put("sound", flag(s.sound));
    put("vibration", flag(s.vibration));
    put("shake", flag(s.shake));
    put("motion", to_string(s.motion));
    put("colour", to_string(s.colour_scheme));
    put("timer", to_string(s.solo_clock));
    put("timer_minutes", std::to_string(s.solo_clock_minutes));
    put("text", std::to_string(s.text_size_percent));
    put("font", field(s.school_font));
    put("language", field(s.language));
    out += '\n';
    return out;
}

// ---- the reader -----------------------------------------------------------

// The settings line: key=value words. A key or a value this program does not
// know leaves that setting at its default, because a setting is not worth
// refusing the whole file for; the checksum has already said the file is whole.
[[nodiscard]] Settings read_settings(const std::vector<std::string>& fields) {
    Settings s;
    for (const std::string& word : fields) {
        const std::size_t equals = word.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string_view key = std::string_view{word}.substr(0, equals);
        const std::string_view value = std::string_view{word}.substr(equals + 1);
        if (key == "help") {
            // A file from before the levels: 1 is the full (two-rung) help,
            // 0 is none.
            if (value == "0") {
                s.help = HelpLevel::off;
            } else if (value == "1") {
                s.help = HelpLevel::two_letters;
            } else {
                s.help = help_level_from_string(value).value_or(s.help);
            }
        } else if (key == "board") {
            s.board = board_mode_from_string(value).value_or(s.board);
        } else if (key == "phase") {
            s.phase = std::string{value};
        } else if (key == "focus") {
            s.focus_sounds = value == "-" ? std::vector<std::string>{} : split(value, ',');
        } else if (key == "size") {
            s.board_size = to_integer<int>(value).value_or(s.board_size);
        } else if (key == "sound_it_out") {
            s.sound_it_out = sound_it_out_from_string(value).value_or(s.sound_it_out);
        } else if (key == "celebration") {
            s.celebration_sounds =
                celebration_sounds_from_string(value).value_or(s.celebration_sounds);
        } else if (key == "sound") {
            s.sound = to_flag(value).value_or(s.sound);
        } else if (key == "vibration") {
            s.vibration = to_flag(value).value_or(s.vibration);
        } else if (key == "shake") {
            s.shake = to_flag(value).value_or(s.shake);
        } else if (key == "motion") {
            s.motion = motion_from_string(value).value_or(s.motion);
        } else if (key == "colour") {
            s.colour_scheme = colour_scheme_from_string(value).value_or(s.colour_scheme);
        } else if (key == "timer") {
            s.solo_clock = solo_clock_from_string(value).value_or(s.solo_clock);
        } else if (key == "timer_minutes") {
            s.solo_clock_minutes = to_integer<int>(value).value_or(s.solo_clock_minutes);
        } else if (key == "text") {
            s.text_size_percent = to_integer<int>(value).value_or(s.text_size_percent);
        } else if (key == "font") {
            s.school_font = unfield(value);
        } else if (key == "language") {
            s.language = unfield(value);
        }
    }
    return s.normalized();
}

[[nodiscard]] bool read_phase(std::string_view text, std::string& phase) {
    phase = unfield(text);
    return phase.empty() || is_valid_name(phase);
}

// One line of the interpreted file into `data`. False if it is not a line this
// version understands, or says something the bounds do not allow.
[[nodiscard]] bool read_line(const RawLine& line, StoreData& data, bool& saw_settings,
                             bool& saw_total) {
    const std::vector<std::string>& f = line.fields;

    if (line.tag == "settings") {
        if (saw_settings) {
            return false;
        }
        saw_settings = true;
        data.settings = read_settings(f);
        return true;
    }

    if (line.tag == "total") {
        if (saw_total || f.size() != 2) {
            return false;
        }
        saw_total = true;
        constexpr std::string_view stars_key = "stars=";
        constexpr std::string_view next_key = "next_board=";
        if (!std::string_view{f[0]}.starts_with(stars_key) ||
            !std::string_view{f[1]}.starts_with(next_key)) {
            return false;
        }
        const auto stars =
            to_integer<std::uint64_t>(std::string_view{f[0]}.substr(stars_key.size()));
        const auto next = to_integer<std::uint32_t>(std::string_view{f[1]}.substr(next_key.size()));
        if (!stars || !next || *next == 0) {
            return false;
        }
        data.stars = *stars;
        data.next_board = *next;
        return true;
    }

    if (line.tag == "best") {
        if (f.size() != 2) {
            return false;
        }
        const auto size = to_integer<int>(f[0]);
        const auto stars = to_integer<int>(f[1]);
        if (!size || !stars || *size < 1 || std::cmp_greater(*size, Board::max_dimension) ||
            *stars < 0 || *stars > 1'000'000) {
            return false;
        }
        data.best.push_back({*size, *stars});
        return true;
    }

    if (line.tag == "word") {
        if (f.size() != 5 || !is_stored_word(f[0])) {
            return false;
        }
        const auto first = to_integer<Day>(f[1]);
        const auto last = to_integer<Day>(f[2]);
        const auto found = to_integer<std::uint32_t>(f[3]);
        const auto unassisted = to_integer<std::uint32_t>(f[4]);
        if (!first || !last || !found || !unassisted || *last < *first || *unassisted > *found) {
            return false;
        }
        data.vault.push_back({f[0], *first, *last, *found, *unassisted});
        return true;
    }

    if (line.tag == "find") {
        if (f.size() != 7 || !is_stored_word(f[0])) {
            return false;
        }
        FindRecord find;
        find.word = f[0];
        const auto stars = to_integer<int>(f[1]);
        const auto size = to_integer<int>(f[2]);
        const auto day = to_integer<Day>(f[4]);
        const auto used = to_flag(f[5]);
        const auto board = to_integer<std::uint32_t>(f[6]);
        if (!stars || !size || !day || !used || !board || !read_phase(f[3], find.phase) ||
            *stars < 0 || *stars > max_find_stars || *size < 1 ||
            std::cmp_greater(*size, Board::max_dimension)) {
            return false;
        }
        find.stars = *stars;
        find.board_size = *size;
        find.day = *day;
        find.start_tile_used = *used;
        find.board = *board;
        data.finds.push_back(std::move(find));
        return true;
    }

    if (line.tag == "day") {
        if (f.size() != 3) {
            return false;
        }
        const auto day = to_integer<Day>(f[0]);
        const auto cleared = to_integer<std::uint32_t>(f[1]);
        const auto stars = to_integer<std::uint32_t>(f[2]);
        if (!day || !cleared || !stars) {
            return false;
        }
        data.days.push_back({*day, *cleared, *stars});
        return true;
    }

    if (line.tag == "board") {
        if (f.size() != 10) {
            return false;
        }
        StoredBoard board;
        const auto serial = to_integer<std::uint32_t>(f[0]);
        const auto day = to_integer<Day>(f[1]);
        const auto size = to_integer<int>(f[2]);
        const auto cleared = to_flag(f[4]);
        const auto found = to_integer<int>(f[5]);
        const auto stars = to_integer<int>(f[6]);
        const auto viable = to_integer<int>(f[7]);
        if (!serial || *serial == 0 || !day || !size || !cleared || !found || !stars || !viable ||
            !read_phase(f[3], board.phase) || *size < 1 ||
            std::cmp_greater(*size, Board::max_dimension) || *found < 0 || *found > 100'000 ||
            *stars < 0 || *stars > 1'000'000 || *viable < 0 || *viable > 100'000) {
            return false;
        }
        const auto parsed = Board::parse(f[8]);
        if (!parsed || parsed->rows() != static_cast<std::size_t>(*size) ||
            parsed->columns() != static_cast<std::size_t>(*size)) {
            return false;
        }
        if (f[9] != "-") {
            board.viable = split(f[9], ',');
            if (board.viable.size() > max_listed_viable_words ||
                !std::all_of(board.viable.begin(), board.viable.end(), is_stored_word)) {
                return false;
            }
        }
        board.serial = *serial;
        board.day = *day;
        board.size = *size;
        board.tiles = f[8];
        board.cleared = *cleared;
        board.found = *found;
        board.stars = *stars;
        board.viable_total = *viable;
        data.boards.push_back(std::move(board));
        return true;
    }

    // The game in progress: a header line, then its board and its finds.
    if (line.tag == "session") {
        if (data.session || f.size() != 4) {
            return false;
        }
        const auto field_after = [&f](std::size_t at,
                                      std::string_view key) -> std::optional<std::string_view> {
            const std::string_view text{f[at]};
            if (!text.starts_with(key)) {
                return std::nullopt;
            }
            return text.substr(key.size());
        };
        const auto state = field_after(0, "state=");
        const auto size = field_after(1, "size=");
        const auto seed = field_after(2, "seed=");
        const auto lit = field_after(3, "lit=");
        if (!state || !size || !seed || !lit) {
            return false;
        }
        SavedSession session;
        if (*state == "box") {
            session.state = SavedState::box;
        } else if (*state == "playing") {
            session.state = SavedState::playing;
        } else if (*state == "resting") {
            session.state = SavedState::resting;
        } else {
            return false;
        }
        const auto side = to_integer<int>(*size);
        const auto seed_value = to_integer<std::uint32_t>(*seed);
        if (!side || !seed_value || *side < 1 || std::cmp_greater(*side, Board::max_dimension) ||
            (*lit != "-" && !is_stored_word(*lit))) {
            return false;
        }
        session.size = *side;
        session.seed = *seed_value;
        if (*lit != "-") {
            session.lit = std::string{*lit};
        }
        data.session = std::move(session);
        return true;
    }

    if (line.tag == "sboard") {
        if (!data.session || data.session->board || f.size() != 5) {
            return false;
        }
        BoardSnapshot board;
        const auto size = to_integer<int>(f[1]);
        const auto total = to_integer<int>(f[3]);
        if (!size || !total || !read_phase(f[0], board.phase) || *size < 1 ||
            std::cmp_greater(*size, Board::max_dimension) || *total < 1 || *total > 100'000) {
            return false;
        }
        const auto parsed = Board::parse(f[2]);
        if (!parsed || parsed->rows() != static_cast<std::size_t>(*size) ||
            parsed->columns() != static_cast<std::size_t>(*size)) {
            return false;
        }
        board.size = *size;
        board.tiles = f[2];
        board.viable_total = *total;
        // A standard board (no phase) lists none of its words; a phonics board
        // lists every one, which data_is_consistent holds to the total.
        if (f[4] != "-") {
            board.viable = split(f[4], ',');
            if (board.viable.size() > max_listed_viable_words ||
                !std::all_of(board.viable.begin(), board.viable.end(), is_stored_word)) {
                return false;
            }
        }
        data.session->board = std::move(board);
        return true;
    }

    if (line.tag == "sfind") {
        if (!data.session || !data.session->board || f.size() != 4 || !is_stored_word(f[0])) {
            return false;
        }
        const auto assisted = to_flag(f[1]);
        const auto day = to_integer<Day>(f[2]);
        const auto stars = to_integer<int>(f[3]);
        if (!assisted || !day || !stars || *stars < 0 || *stars > max_find_stars) {
            return false;
        }
        data.session->board->finds.push_back({f[0], *assisted, *day, *stars});
        return true;
    }

    return false;
}

// The whole of the data, checked as a set: every list within its bound and in
// the order the writer keeps it.
[[nodiscard]] bool data_is_consistent(const StoreData& data) {
    if (data.boards.size() > max_stored_boards || data.finds.size() > max_stored_finds ||
        data.vault.size() > max_vault_words || data.days.size() > max_stored_days) {
        return false;
    }
    if (data.session) {
        const SavedSession& session = *data.session;
        if (session.state == SavedState::box) {
            if (session.board || session.lit) {
                return false;
            }
        } else {
            if (!session.board || session.board->size != session.size) {
                return false;
            }
            const BoardSnapshot& board = *session.board;
            // A standard board (no phase) lists none of its words and says how
            // many there were; a phonics board lists every one of them.
            const bool standard = board.phase.empty();
            if (standard ? (!board.viable.empty() || board.viable_total < 1)
                         : (board.viable.empty() ||
                            board.viable.size() != static_cast<std::size_t>(board.viable_total))) {
                return false;
            }
            // A lit word is one of the board's, which a phonics board's list
            // says and a standard board's count cannot; the session checks the
            // word against its own solve when it restores.
            if (session.lit && (session.state != SavedState::playing ||
                                (!standard && std::find(board.viable.begin(), board.viable.end(),
                                                        *session.lit) == board.viable.end()))) {
                return false;
            }
        }
    }
    const auto strictly_ascending = [](const auto& list, auto key) {
        return std::adjacent_find(list.begin(), list.end(), [&key](const auto& a, const auto& b) {
                   return !(key(a) < key(b));
               }) == list.end();
    };
    if (!strictly_ascending(data.best, [](const BestStars& b) { return b.size; }) ||
        !strictly_ascending(data.vault,
                            [](const VaultWord& w) { return std::string_view{w.word}; }) ||
        !strictly_ascending(data.days, [](const DayTally& d) { return d.day; }) ||
        !strictly_ascending(data.boards, [](const StoredBoard& b) { return b.serial; })) {
        return false;
    }
    return std::all_of(data.boards.begin(), data.boards.end(),
                       [&data](const StoredBoard& b) { return b.serial < data.next_board; }) &&
           std::all_of(data.finds.begin(), data.finds.end(),
                       [&data](const FindRecord& f) { return f.board < data.next_board; });
}

// The lines between the header and the checksum, as words.
[[nodiscard]] std::optional<RawLine> read_raw_line(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    RawLine line;
    const std::vector<std::string> words = split(text, ' ');
    if (std::any_of(words.begin(), words.end(),
                    [](const std::string& word) { return word.empty(); })) {
        return std::nullopt;
    }
    line.tag = words.front();
    line.fields.assign(words.begin() + 1, words.end());
    return line;
}

}  // namespace

std::string_view to_string(StoreError error) noexcept {
    switch (error) {
        case StoreError::too_large:
            return "too_large";
        case StoreError::corrupt:
            return "corrupt";
        case StoreError::newer_version:
            return "newer_version";
        case StoreError::no_migration:
            return "no_migration";
        case StoreError::migration_failed:
            return "migration_failed";
    }
    return "corrupt";
}

std::span<const MigrationStep> builtin_migrations() noexcept {
    // Version 2 lets the game in progress be a standard board: its sboard line
    // carries the count of the board's words and no list of them ("-"). A
    // version 1 file could not hold one (the sboard's list could not be "-", and
    // a board whose list was not the whole of it was refused as damage), so
    // every line a version 1 writer made is already a line version 2 reads: the
    // step rewrites nothing. It is still a step, so that a version 1 file is
    // kept beside the new one (store.cpp), and a program before this change
    // refuses a file after it as newer_version instead of misreading a standard
    // board. A test writes a version 1 file and reads it with this list.
    //
    // Version 3 names the interface language by its catalogue, so the Chinese
    // of version 2 ("zh") is "zh_CN". The step rewrites the language in the
    // settings line and nothing else; a program before this change refuses a
    // version 3 file as newer_version instead of reading "zh_CN" as no language.
    static const std::array<MigrationStep, 2> steps = {
        MigrationStep{1, [](RawStore&) { return true; }},
        MigrationStep{2,
                      [](RawStore& raw) {
                          for (RawLine& line : raw.lines) {
                              if (line.tag != "settings") {
                                  continue;
                              }
                              for (std::string& word : line.fields) {
                                  if (word == "language=zh") {
                                      word = "language=zh_CN";
                                  }
                              }
                          }
                          return true;
                      }},
    };
    return steps;
}

std::filesystem::path default_store_path(const std::filesystem::path& base) {
    return base / std::string{store_directory_name} / std::string{store_file_name};
}

std::string serialize_store(const StoreData& data, int version) {
    auto zone = profiling::zone<"store.serialize">();

    std::string out;
    out.reserve(4096);
    out += header_tag;
    out += ' ';
    out += std::to_string(version);
    out += '\n';

    out += settings_line(data.settings);
    append_line(
        out, "total",
        {"stars=" + std::to_string(data.stars), "next_board=" + std::to_string(data.next_board)});

    for (const BestStars& best : data.best) {
        append_line(out, "best", {std::to_string(best.size), std::to_string(best.stars)});
    }
    for (const VaultWord& word : data.vault) {
        append_line(out, "word",
                    {word.word, std::to_string(word.first_day), std::to_string(word.last_day),
                     std::to_string(word.found), std::to_string(word.unassisted)});
    }
    for (const FindRecord& find : data.finds) {
        append_line(out, "find",
                    {find.word, std::to_string(find.stars), std::to_string(find.board_size),
                     field(find.phase), std::to_string(find.day), flag(find.start_tile_used),
                     std::to_string(find.board)});
    }
    for (const DayTally& day : data.days) {
        append_line(out, "day",
                    {std::to_string(day.day), std::to_string(day.boards_cleared),
                     std::to_string(day.stars)});
    }
    for (const StoredBoard& board : data.boards) {
        append_line(
            out, "board",
            {std::to_string(board.serial), std::to_string(board.day), std::to_string(board.size),
             field(board.phase), flag(board.cleared), std::to_string(board.found),
             std::to_string(board.stars), std::to_string(board.viable_total), board.tiles,
             board.viable.empty() ? std::string{"-"} : join(board.viable, ',')});
    }
    if (data.session) {
        const SavedSession& session = *data.session;
        append_line(out, "session",
                    {"state=" + std::string{to_string(session.state)},
                     "size=" + std::to_string(session.size), "seed=" + std::to_string(session.seed),
                     "lit=" + field(session.lit.value_or(""))});
        if (session.board) {
            const BoardSnapshot& board = *session.board;
            append_line(out, "sboard",
                        {field(board.phase), std::to_string(board.size), board.tiles,
                         std::to_string(board.viable_total),
                         board.viable.empty() ? std::string{"-"} : join(board.viable, ',')});
            for (const FoundSnapshot& find : board.finds) {
                append_line(out, "sfind",
                            {find.word, flag(find.assisted), std::to_string(find.day),
                             std::to_string(find.stars)});
            }
        }
    }

    out += end_tag;
    out += ' ';
    out += hex16(checksum(out.substr(0, out.size() - end_tag.size() - 1)));
    out += '\n';
    zone.Value(out.size());
    return out;
}

std::expected<ParsedStore, StoreError> parse_store(std::string_view text,
                                                   std::span<const MigrationStep> steps,
                                                   int target_version) {
    auto zone = profiling::zone<"store.parse">();
    zone.Value(text.size());

    if (text.size() > max_store_bytes) {
        return std::unexpected(StoreError::too_large);
    }

    // The lines, with a CR before the LF taken off, as a text copied through a
    // system that changes line endings would have: the checksum is over the
    // lines, not over the bytes of the line endings.
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = text.find('\n', start);
        const bool last_has_no_newline = end == std::string_view::npos;
        if (last_has_no_newline) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        start = end + 1;
    }
    // A blank line at the end is the file's last newline, nothing more.
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    if (lines.size() < 2) {
        return std::unexpected(StoreError::corrupt);
    }

    // The header, and the one question that matters before any other: is this a
    // version this program can read.
    const std::optional<RawLine> header = read_raw_line(lines.front());
    if (!header || header->tag != header_tag || header->fields.size() != 1) {
        return std::unexpected(StoreError::corrupt);
    }
    const std::optional<int> version = to_integer<int>(header->fields.front());
    if (!version || *version < 1) {
        return std::unexpected(StoreError::corrupt);
    }
    if (*version > target_version) {
        return std::unexpected(StoreError::newer_version);
    }

    // The checksum: of every line before the last, with the line ending the
    // writer uses.
    const std::optional<RawLine> end = read_raw_line(lines.back());
    if (!end || end->tag != end_tag || end->fields.size() != 1 ||
        end->fields.front().size() != 16) {
        return std::unexpected(StoreError::corrupt);
    }
    std::string body;
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
        body += lines[i];
        body += '\n';
    }
    if (hex16(checksum(body)) != end->fields.front()) {
        return std::unexpected(StoreError::corrupt);
    }

    RawStore raw;
    raw.version = *version;
    for (std::size_t i = 1; i + 1 < lines.size(); ++i) {
        std::optional<RawLine> line = read_raw_line(lines[i]);
        if (!line) {
            return std::unexpected(StoreError::corrupt);
        }
        raw.lines.push_back(std::move(*line));
    }

    // Up to this version, a step at a time.
    while (raw.version < target_version) {
        const auto step = std::find_if(steps.begin(), steps.end(), [&raw](const MigrationStep& s) {
            return s.from == raw.version;
        });
        if (step == steps.end() || !step->apply) {
            return std::unexpected(StoreError::no_migration);
        }
        if (!step->apply(raw)) {
            return std::unexpected(StoreError::migration_failed);
        }
        ++raw.version;
    }

    ParsedStore parsed;
    parsed.from_version = *version;
    bool saw_settings = false;
    bool saw_total = false;
    for (const RawLine& line : raw.lines) {
        if (!read_line(line, parsed.data, saw_settings, saw_total)) {
            return std::unexpected(StoreError::corrupt);
        }
    }
    if (!data_is_consistent(parsed.data)) {
        return std::unexpected(StoreError::corrupt);
    }
    return parsed;
}

}  // namespace wjs
