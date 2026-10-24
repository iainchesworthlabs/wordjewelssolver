// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/phase_data.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "ascii.hpp"
#include "wjs/profiling.hpp"

namespace wjs {
namespace {

constexpr std::string_view byte_order_mark = "\xEF\xBB\xBF";
constexpr std::string_view tricky_marker = "tricky";
constexpr std::string_view clip_prefix = "clip:";

// The keys a `#` line may carry, each as `key: value`. Several may share a line.
constexpr std::array<std::string_view, 4> header_keys = {"phase", "reviewed-by", "reviewed-on",
                                                         "source"};
constexpr std::size_t key_phase = 0;
constexpr std::size_t key_reviewed_by = 1;
constexpr std::size_t key_reviewed_on = 2;
constexpr std::size_t key_source = 3;

[[nodiscard]] constexpr bool is_blank(char c) noexcept {
    return c == ' ' || c == '\t';
}

using ascii::lower;
using ascii::to_lower;
using ascii::to_upper;
using ascii::upper;

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    return ascii::trim(text, " \t\r");
}

[[nodiscard]] std::vector<std::string_view> fields_of(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start < line.size()) {
        if (is_blank(line[start])) {
            ++start;
            continue;
        }
        std::size_t end = start;
        while (end < line.size() && !is_blank(line[end])) {
            ++end;
        }
        fields.push_back(line.substr(start, end - start));
        start = end;
    }
    return fields;
}

[[nodiscard]] bool is_lower_letters(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= 'a' && c <= 'z'; });
}

// Lowercase letters, or two runs of them with one underscore between (a split
// digraph: "a_e").
[[nodiscard]] bool is_grapheme_letters(std::string_view text) noexcept {
    const auto underscore = text.find('_');
    if (underscore == std::string_view::npos) {
        return is_lower_letters(text);
    }
    return is_lower_letters(text.substr(0, underscore)) &&
           is_lower_letters(text.substr(underscore + 1));
}

// What one token of a word line after the word is: a grapheme, the first half
// of a split digraph or its second half, with the sound it picks if it says.
struct Piece {
    enum class Kind : std::uint8_t { whole, open, close };

    Kind kind = Kind::whole;
    std::string letters;  // without the underscore or the number
    std::size_t sound = WordGrapheme::unspecified;
};

[[nodiscard]] std::optional<Piece> read_piece(std::string_view token) {
    Piece piece;
    const auto colon = token.find(':');
    if (colon != std::string_view::npos) {
        const auto number = token.substr(colon + 1);
        if (number.empty() || number.size() > 2 || number.front() == '0' ||
            !std::ranges::all_of(number, is_digit)) {
            return std::nullopt;
        }
        piece.sound = static_cast<std::size_t>(std::stoul(std::string(number))) - 1;
        token = token.substr(0, colon);
    }
    if (token.starts_with('_')) {
        // The second half of a split digraph picks nothing: the first half does.
        if (colon != std::string_view::npos || !is_lower_letters(token.substr(1))) {
            return std::nullopt;
        }
        piece.kind = Piece::Kind::close;
        piece.letters = std::string(token.substr(1));
        return piece;
    }
    if (token.ends_with('_')) {
        if (!is_lower_letters(token.substr(0, token.size() - 1))) {
            return std::nullopt;
        }
        piece.kind = Piece::Kind::open;
        piece.letters = std::string(token.substr(0, token.size() - 1));
        return piece;
    }
    if (!is_lower_letters(token)) {
        return std::nullopt;
    }
    piece.letters = std::string(token);
    return piece;
}

[[nodiscard]] bool is_phase_name(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c);
    });
}

[[nodiscard]] bool is_clip_id(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || is_digit(c) || c == '.' || c == '_' || c == '-';
    });
}

// `/.../`: slashes round at least one character, none inside.
[[nodiscard]] bool is_sound(std::string_view text) noexcept {
    return text.size() >= 3 && text.front() == '/' && text.back() == '/' &&
           !text.substr(1, text.size() - 2).contains('/');
}

// YYYY-MM-DD, and a day that the month has.
[[nodiscard]] bool is_iso_date(std::string_view text) noexcept {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }
    for (const std::size_t i : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U}) {
        if (!is_digit(text[i])) {
            return false;
        }
    }
    const auto number = [&](std::size_t at, std::size_t count) {
        int value = 0;
        for (std::size_t i = at; i < at + count; ++i) {
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };
    const int year = number(0, 4);
    const int month = number(5, 2);
    const int day = number(8, 2);
    if (year < 1 || month < 1 || month > 12 || day < 1) {
        return false;
    }
    constexpr std::array<int, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return day <= days[static_cast<std::size_t>(month - 1)] + ((leap && month == 2) ? 1 : 0);
}

// A value still written as the template's <placeholder> has not been given.
[[nodiscard]] bool is_placeholder(std::string_view value) noexcept {
    return value.size() >= 2 && value.front() == '<' && value.back() == '>';
}

class Report {
public:
    Report(std::string_view file, DataProblems& problems) : file_(file), problems_(problems) {}

    void operator()(std::size_t line, std::string message) const {
        problems_.push_back(DataProblem{file_, line, std::move(message)});
    }

private:
    std::string file_;
    DataProblems& problems_;
};

struct HeaderSeen {
    std::array<bool, header_keys.size()> seen{};
};

// One `#` line, with the leading `#` already removed.
void read_header_line(std::string_view comment, std::size_t line, PhaseHeader& header,
                      HeaderSeen& seen, const Report& report) {
    struct Hit {
        std::size_t at;
        std::size_t key;
    };
    std::vector<Hit> hits;
    for (std::size_t i = 0; i < comment.size(); ++i) {
        if (i > 0 && !is_blank(comment[i - 1])) {
            continue;
        }
        for (std::size_t key = 0; key < header_keys.size(); ++key) {
            const auto name = header_keys[key];
            if (comment.substr(i).starts_with(name) && comment.size() > i + name.size() &&
                comment[i + name.size()] == ':') {
                hits.push_back(Hit{i, key});
                break;
            }
        }
    }

    for (std::size_t h = 0; h < hits.size(); ++h) {
        const auto name = header_keys[hits[h].key];
        const std::size_t start = hits[h].at + name.size() + 1;
        const std::size_t end = h + 1 < hits.size() ? hits[h + 1].at : comment.size();
        auto value = trim(comment.substr(start, end - start));

        if (seen.seen[hits[h].key]) {
            report(line, "'" + std::string(name) + ":' is given twice");
            continue;
        }
        seen.seen[hits[h].key] = true;
        if (is_placeholder(value)) {
            value = {};
        }

        switch (hits[h].key) {
            case key_phase:
                if (!is_phase_name(value)) {
                    report(line, "the phase must be letters and digits, as in '# phase: A'");
                } else {
                    header.phase = upper(value);
                }
                break;
            case key_reviewed_by:
                header.reviewed_by = value;
                break;
            case key_reviewed_on:
                header.reviewed_on = value;
                break;
            case key_source:
                header.source = value;
                break;
            default:
                break;
        }
    }
}

// What the header as a whole has to say, once every line has been read.
void check_header(const PhaseHeader& header, const HeaderSeen& seen, const Report& report) {
    if (!seen.seen[key_phase]) {
        report(0, "no '# phase: <name>' header");
    }
    const bool by = !header.reviewed_by.empty();
    const bool on = !header.reviewed_on.empty();
    if (by != on) {
        report(0, "give both reviewed-by and reviewed-on, or neither (neither is test data)");
    }
    if (on && !is_iso_date(header.reviewed_on)) {
        report(0, "reviewed-on must be a date written YYYY-MM-DD");
    }
    // A reviewed list says where it came from, as data/README.md asks of every
    // list. Test data need not.
    if (by && on && header.source.empty()) {
        report(0, "a reviewed file says where its list came from: add 'source: <where>'");
    }
}

// The lines of a file, with the byte-order mark and the carriage returns of a
// Windows editor taken off. `#` lines go to the header; the rest, trimmed and
// not blank, go to `visit(line_number, text)`.
template <typename Visit>
void read_file(std::istream& in, PhaseHeader& header, const Report& report, Visit visit) {
    HeaderSeen seen;
    std::string raw;
    std::size_t number = 0;
    while (std::getline(in, raw)) {
        ++number;
        std::string_view text = raw;
        if (number == 1 && text.starts_with(byte_order_mark)) {
            text.remove_prefix(byte_order_mark.size());
        }
        text = trim(text);
        if (text.empty()) {
            continue;
        }
        if (text.front() == '#') {
            read_header_line(text.substr(1), number, header, seen, report);
            continue;
        }
        visit(number, text);
    }
    check_header(header, seen, report);
}

[[nodiscard]] std::string in_quotes(std::string_view text) {
    return "'" + std::string(text) + "'";
}

// What a file name says about the phase and the kind of file it is.
struct FileName {
    std::string phase;
    bool graphemes = false;
};

[[nodiscard]] std::optional<FileName> read_file_name(std::string_view name) {
    constexpr std::string_view prefix = "phase-";
    constexpr std::string_view graphemes_suffix = ".graphemes.txt";
    constexpr std::string_view words_suffix = ".words.txt";
    if (!name.starts_with(prefix)) {
        return std::nullopt;
    }
    name.remove_prefix(prefix.size());
    FileName result;
    if (name.ends_with(graphemes_suffix)) {
        name.remove_suffix(graphemes_suffix.size());
        result.graphemes = true;
    } else if (name.ends_with(words_suffix)) {
        name.remove_suffix(words_suffix.size());
    } else {
        return std::nullopt;
    }
    if (!is_phase_name(name)) {
        return std::nullopt;
    }
    result.phase = upper(name);
    return result;
}

// Where a word or a grapheme was first seen, for "already listed in ...".
struct Place {
    std::string file;
    std::size_t line = 0;
};

[[nodiscard]] std::string described(const Place& place) {
    return place.file + ":" + std::to_string(place.line);
}

}  // namespace

bool PhaseHeader::reviewed() const noexcept {
    return !reviewed_by.empty() && is_iso_date(reviewed_on) && !source.empty();
}

std::string to_string(const DataProblem& problem) {
    if (problem.line == 0) {
        return problem.file + ": " + problem.message;
    }
    return problem.file + ":" + std::to_string(problem.line) + ": " + problem.message;
}

std::expected<GraphemesFile, DataProblems> parse_graphemes(std::istream& in,
                                                           std::string_view name) {
    GraphemesFile file;
    file.name = std::string(name);
    DataProblems problems;
    const Report report(name, problems);

    // A grapheme may be listed again for another sound, so a line is known by its
    // letters and its sound together.
    std::map<std::string, std::size_t> first_line;
    std::string previous;

    read_file(in, file.header, report, [&](std::size_t line, std::string_view text) {
        const auto fields = fields_of(text);
        if (fields.size() != 3) {
            report(line,
                   "a grapheme line is the letters, the sound and the clip: "
                   "'ai  /sound/  clip:ai'");
            return;
        }
        const auto letters = fields[0];
        const auto sound = fields[1];
        const auto clip = fields[2];

        bool good = true;
        if (!is_grapheme_letters(letters)) {
            report(line, in_quotes(letters) +
                             " is not lowercase a-z letters (or two runs of them with an "
                             "underscore between, for a split digraph)");
            good = false;
        }
        if (!is_sound(sound)) {
            report(line, in_quotes(sound) + " is not a sound written between slashes");
            good = false;
        }
        if (!clip.starts_with(clip_prefix) || !is_clip_id(clip.substr(clip_prefix.size()))) {
            report(line, in_quotes(clip) + " is not a clip id written 'clip:<id>'");
            good = false;
        }
        if (!good) {
            return;
        }

        const std::string key(letters);
        const std::string pair = key + " " + std::string(sound);
        if (const auto found = first_line.find(pair); found != first_line.end()) {
            report(line, in_quotes(letters) + " with the sound " + std::string(sound) +
                             " is listed twice (first on line " + std::to_string(found->second) +
                             ")");
            return;
        }
        if (!previous.empty() && previous > key) {
            report(line,
                   in_quotes(letters) + " is out of order (after " + in_quotes(previous) + ")");
        }
        previous = key;
        first_line.emplace(pair, line);
        file.graphemes.push_back(
            Grapheme{key, std::string(sound), std::string(clip.substr(clip_prefix.size())), line});
    });

    if (!problems.empty()) {
        return std::unexpected(std::move(problems));
    }
    return file;
}

std::expected<WordsFile, DataProblems> parse_words(std::istream& in, std::string_view name) {
    WordsFile file;
    file.name = std::string(name);
    DataProblems problems;
    const Report report(name, problems);

    std::map<std::string, std::size_t> first_line;
    std::string previous;

    read_file(in, file.header, report, [&](std::size_t line, std::string_view text) {
        const auto fields = fields_of(text);
        const auto word = fields.front();

        if (!is_lower_letters(word)) {
            report(line, in_quotes(word) + " is not a lowercase a-z word");
            return;
        }
        if (word.size() < min_phase_word_letters) {
            report(line, in_quotes(word) + " has fewer than " +
                             std::to_string(min_phase_word_letters) +
                             " letters, so no board can count it");
            return;
        }

        PhaseWord entry;
        entry.word = std::string(word);
        entry.line = line;

        if (fields.size() == 1) {
            report(line, in_quotes(word) +
                             " has no graphemes: write them after the word, "
                             "or mark the word 'tricky'");
            return;
        }
        if (fields.size() == 2 && fields[1] == tricky_marker) {
            entry.tricky = true;
        } else {
            // The pieces after the word, in order: each is a grapheme, or one
            // half of a split digraph. The letters of the pieces, in the order
            // written, have to spell the word.
            std::string spelt;
            std::size_t at = 0;
            std::optional<std::size_t> open;  // a split digraph's first half, not yet closed
            bool between = false;             // a grapheme has come since it
            bool good = true;
            for (const auto token : std::span(fields).subspan(1)) {
                if (token == tricky_marker) {
                    report(line, in_quotes(word) + ": 'tricky' stands alone, with no split");
                    good = false;
                    break;
                }
                const auto piece = read_piece(token);
                if (!piece) {
                    report(line, in_quotes(token) + " in " + in_quotes(word) +
                                     " is not a grapheme: letters, with ':2' to pick a second "
                                     "sound, or the halves of a split digraph as in 'a_' ... '_e'");
                    good = false;
                    break;
                }

                if (piece->kind == Piece::Kind::close) {
                    if (!open) {
                        report(line, in_quotes(token) + " in " + in_quotes(word) +
                                         " closes a split digraph that was not opened");
                        good = false;
                        break;
                    }
                    if (!between) {
                        report(line, "the halves of a split digraph in " + in_quotes(word) +
                                         " have nothing between them");
                        good = false;
                        break;
                    }
                    WordGrapheme& split = entry.graphemes[*open];
                    split.letters += "_" + piece->letters;
                    split.second = at;
                    open.reset();
                } else {
                    if (open && piece->kind == Piece::Kind::open) {
                        report(line, in_quotes(token) + " in " + in_quotes(word) +
                                         " opens a split digraph inside another");
                        good = false;
                        break;
                    }
                    WordGrapheme grapheme;
                    grapheme.letters = piece->letters;
                    grapheme.sound = piece->sound;
                    grapheme.first = at;
                    entry.graphemes.push_back(std::move(grapheme));
                    if (piece->kind == Piece::Kind::open) {
                        open = entry.graphemes.size() - 1;
                        between = false;
                    } else if (open) {
                        between = true;
                    }
                }
                spelt += piece->letters;
                at += piece->letters.size();
            }
            if (good && open) {
                report(line, "a split digraph in " + in_quotes(word) + " is opened and not closed");
                good = false;
            }
            if (good && spelt != word) {
                report(line, "the graphemes of " + in_quotes(word) + " spell " + in_quotes(spelt));
                good = false;
            }
            if (!good) {
                return;
            }
        }

        if (const auto found = first_line.find(entry.word); found != first_line.end()) {
            report(line, in_quotes(word) + " is listed twice (first on line " +
                             std::to_string(found->second) + ")");
            return;
        }
        if (!previous.empty() && previous > entry.word) {
            report(line, in_quotes(word) + " is out of order (after " + in_quotes(previous) + ")");
        }
        previous = entry.word;
        first_line.emplace(entry.word, line);
        file.words.push_back(std::move(entry));
    });

    if (!problems.empty()) {
        return std::unexpected(std::move(problems));
    }
    return file;
}

const PhaseWord* PhaseIndex::find_word(std::string_view word) const {
    const std::string key = lower(word);
    const auto found = std::ranges::lower_bound(words_, key, {}, &PhaseWord::word);
    return (found != words_.end() && found->word == key) ? &*found : nullptr;
}

const Grapheme* PhaseIndex::find_grapheme(std::string_view letters) const {
    const std::string key = lower(letters);
    const auto found = std::ranges::lower_bound(graphemes_, key, {}, &Grapheme::letters);
    return (found != graphemes_.end() && found->letters == key) ? &*found : nullptr;
}

std::span<const Grapheme> PhaseIndex::sounds_of(std::string_view letters) const {
    const std::string key = lower(letters);
    const auto range = std::ranges::equal_range(graphemes_, key, {}, &Grapheme::letters);
    // The iterators point into graphemes_, a member of this object, so the span
    // is valid for as long as the PhaseIndex is.
    // NOLINTNEXTLINE(bugprone-dangling-handle)
    return {range.begin(), range.end()};
}

const Grapheme* PhaseIndex::sound_of(const WordGrapheme& grapheme) const {
    const auto sounds = sounds_of(grapheme.letters);
    const std::size_t which =
        grapheme.sound == WordGrapheme::unspecified ? std::size_t{0} : grapheme.sound;
    return which < sounds.size() ? &sounds[which] : nullptr;
}

const PhaseIndex* PhaseSet::find(std::string_view phase) const {
    const std::string key = upper(phase);
    const auto found = std::ranges::find_if(
        phases_, [&](const PhaseIndex& index) { return index.phase() == key; });
    return found == phases_.end() ? nullptr : &*found;
}

std::expected<PhaseSet, DataProblems> PhaseSet::build(const std::vector<GraphemesFile>& graphemes,
                                                      const std::vector<WordsFile>& words,
                                                      const Dictionary& english) {
    DataProblems problems;

    // At most one file of each kind for a phase.
    std::map<std::string, const GraphemesFile*> grapheme_files;
    std::map<std::string, const WordsFile*> word_files;
    // Shorter names first, then alphabetical: A before B, and 2 before 10.
    const auto by_length_then_name = [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    };
    std::set<std::string, decltype(by_length_then_name)> names(by_length_then_name);
    for (const auto& file : graphemes) {
        names.insert(file.header.phase);
        if (!grapheme_files.emplace(file.header.phase, &file).second) {
            problems.push_back({file.name, 0,
                                "phase " + file.header.phase +
                                    " has a second graphemes file (the first is " +
                                    grapheme_files.at(file.header.phase)->name + ")"});
        }
    }
    for (const auto& file : words) {
        names.insert(file.header.phase);
        if (!word_files.emplace(file.header.phase, &file).second) {
            problems.push_back({file.name, 0,
                                "phase " + file.header.phase +
                                    " has a second words file (the first is " +
                                    word_files.at(file.header.phase)->name + ")"});
        }
    }

    PhaseSet result;
    // By letters, each with its sounds in the order they were defined.
    std::map<std::string, std::vector<std::pair<Grapheme, Place>>> known_graphemes;
    std::map<std::string, std::pair<PhaseWord, Place>> known_words;  // by word

    for (const auto& phase : names) {
        if (const auto found = grapheme_files.find(phase); found != grapheme_files.end()) {
            const auto& file = *found->second;
            for (const auto& grapheme : file.graphemes) {
                const Place here{file.name, grapheme.line};
                auto& sounds = known_graphemes[grapheme.letters];
                const auto same = std::ranges::find_if(
                    sounds, [&](const auto& known) { return known.first.sound == grapheme.sound; });
                if (same != sounds.end()) {
                    problems.push_back({file.name, grapheme.line,
                                        in_quotes(grapheme.letters) + " with the sound " +
                                            grapheme.sound + " is already defined (" +
                                            described(same->second) + ")"});
                    continue;
                }
                sounds.emplace_back(grapheme, here);
            }
        }

        if (const auto found = word_files.find(phase); found != word_files.end()) {
            const auto& file = *found->second;
            for (const auto& entry : file.words) {
                const auto where = [&](std::string message) {
                    problems.push_back({file.name, entry.line, std::move(message)});
                };
                if (!english.contains(entry.word)) {
                    where(in_quotes(entry.word) + " is not in the dictionary");
                }
                for (const WordGrapheme& grapheme : entry.graphemes) {
                    const auto known = known_graphemes.find(grapheme.letters);
                    if (known == known_graphemes.end()) {
                        where("grapheme " + in_quotes(grapheme.letters) + " of " +
                              in_quotes(entry.word) + " is not in phase " + phase +
                              " or an earlier phase");
                        continue;
                    }
                    // Which sound, where there is a choice.
                    const std::size_t count = known->second.size();
                    if (grapheme.sound == WordGrapheme::unspecified) {
                        if (count > 1) {
                            where("grapheme " + in_quotes(grapheme.letters) + " of " +
                                  in_quotes(entry.word) + " has " + std::to_string(count) +
                                  " sounds in phase " + phase + ": say which, as '" +
                                  grapheme.letters + ":1' to '" + grapheme.letters + ":" +
                                  std::to_string(count) + "'");
                        }
                    } else if (grapheme.sound >= count) {
                        where("grapheme " + in_quotes(grapheme.letters) + " of " +
                              in_quotes(entry.word) + " is numbered " +
                              std::to_string(grapheme.sound + 1) + " and has only " +
                              std::to_string(count) + " sound(s) in phase " + phase);
                    }
                }
                const Place here{file.name, entry.line};
                const auto [at, added] = known_words.emplace(entry.word, std::pair{entry, here});
                if (!added) {
                    where(in_quotes(entry.word) + " is already listed (" +
                          described(at->second.second) + ")");
                }
            }
        }

        // The index of this phase: everything known so far, which is every
        // earlier phase and this one. std::map keeps both sorted by key.
        PhaseIndex index;
        index.phase_ = phase;
        std::set<char> letters;
        for (const auto& [key, sounds] : known_graphemes) {
            for (const auto& sound : sounds) {
                index.graphemes_.push_back(sound.first);
            }
            for (const char c : key) {
                if (c >= 'a' && c <= 'z') {
                    letters.insert(to_upper(c));
                }
            }
        }
        for (const auto& [key, entry] : known_words) {
            index.words_.push_back(entry.first);
            // Every word of a words file was checked against the English
            // dictionary above, and is letters only.
            static_cast<void>(index.dictionary_.insert(key));
            for (const char c : key) {
                letters.insert(to_upper(c));
            }
        }
        index.alphabet_.assign(letters.begin(), letters.end());
        result.phases_.push_back(std::move(index));
    }

    if (!problems.empty()) {
        return std::unexpected(std::move(problems));
    }

    for (const auto& file : graphemes) {
        ++result.file_count_;
        if (!file.header.reviewed()) {
            result.unreviewed_.push_back(file.name);
        }
    }
    for (const auto& file : words) {
        ++result.file_count_;
        if (!file.header.reviewed()) {
            result.unreviewed_.push_back(file.name);
        }
    }
    return result;
}

std::expected<PhaseSet, DataProblems> PhaseSet::load_directory(
    const std::filesystem::path& directory, const Dictionary& english) {
    auto zone = profiling::zone<"data.load_phase">();

    DataProblems problems;
    const auto fail = [&](const std::string& file, std::string message) {
        problems.push_back({file, 0, std::move(message)});
    };

    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        fail(directory.generic_string(), "no such directory");
        return std::unexpected(std::move(problems));
    }

    std::vector<std::filesystem::path> paths;
    std::filesystem::directory_iterator entry(directory, error);
    for (; !error && entry != std::filesystem::directory_iterator(); entry.increment(error)) {
        if (entry->is_regular_file(error) && entry->path().extension() == ".txt") {
            paths.push_back(entry->path());
        }
    }
    if (error) {
        fail(directory.generic_string(), "the directory could not be read");
        return std::unexpected(std::move(problems));
    }
    std::ranges::sort(paths);

    std::vector<GraphemesFile> grapheme_files;
    std::vector<WordsFile> word_files;
    for (const auto& path : paths) {
        const std::string name = path.filename().string();
        const auto kind = read_file_name(name);
        if (!kind) {
            fail(name, "not named phase-<x>.graphemes.txt or phase-<x>.words.txt");
            continue;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            fail(name, "could not be opened");
            continue;
        }
        if (kind->graphemes) {
            auto parsed = parse_graphemes(stream, name);
            if (!parsed) {
                problems.insert(problems.end(), parsed.error().begin(), parsed.error().end());
                continue;
            }
            if (parsed->header.phase != kind->phase) {
                fail(name, "its header says phase " + parsed->header.phase +
                               ", its name says phase " + kind->phase);
                continue;
            }
            grapheme_files.push_back(std::move(*parsed));
        } else {
            auto parsed = parse_words(stream, name);
            if (!parsed) {
                problems.insert(problems.end(), parsed.error().begin(), parsed.error().end());
                continue;
            }
            if (parsed->header.phase != kind->phase) {
                fail(name, "its header says phase " + parsed->header.phase +
                               ", its name says phase " + kind->phase);
                continue;
            }
            word_files.push_back(std::move(*parsed));
        }
    }
    if (!problems.empty()) {
        return std::unexpected(std::move(problems));
    }
    auto phases = build(grapheme_files, word_files, english);
    if (phases) {
        // The words the phases hold, every one of them: the figure a trace
        // reads next to the time the directory took.
        std::uint64_t words = 0;
        for (const PhaseIndex& phase : phases->phases()) {
            words += static_cast<std::uint64_t>(phase.words().size());
        }
        zone.Value(words);
    }
    return phases;
}

}  // namespace wjs
