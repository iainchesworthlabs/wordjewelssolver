// SPDX-License-Identifier: GPL-3.0-or-later
// The performance tier's metrics for what the screens wait on besides the
// solver: the store that is rewritten after every find, the dashboard built when
// a parent opens it, a phase directory loaded at start-up, and a path handed in
// by a finger. Declared in metrics.hpp, asserted in data_work_test.cpp and
// timing_test.cpp, printed by report_test.cpp.
//
// The library keeps no counter for these as it does for the solver, so the work
// is what can be counted from outside and does not depend on the compiler or the
// standard library: the bytes a store comes to, the bytes written to disk by the
// saves a play makes, the records read back, the rows a dashboard has, the words
// and bytes of a phase. The time of each is a ratio to the calibration workload,
// as the solver's is, and runs only on request.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "budgets.hpp"
#include "corpus.hpp"
#include "data_corpus.hpp"
#include "measure.hpp"
#include "metric_builders.hpp"
#include "metrics.hpp"
#include "wjs/dashboard.hpp"
#include "wjs/game_session.hpp"
#include "wjs/path.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/session_store.hpp"
#include "wjs/store.hpp"

namespace wjs::perf {
namespace {

[[nodiscard]] std::string mode_label(BoardMode mode) {
    return std::string(to_string(mode));
}

[[nodiscard]] std::size_t index_of(BoardMode mode) {
    return mode == BoardMode::standard ? 0 : 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

TempDirectory::TempDirectory() {
    static unsigned counter = 0;
    const auto stamp = Clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            std::format("wjs-perf-{}-{}", static_cast<long long>(stamp), counter++);
    std::filesystem::create_directories(path_);
}

TempDirectory::~TempDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

namespace {

// The sessions a kept store is made of, played to the resting screen and recorded
// the way the application records them (snapshot_of, then Store::record).
[[nodiscard]] std::vector<wjs::BoardSnapshot> kept_snapshots(BoardMode mode) {
    std::vector<wjs::BoardSnapshot> snapshots;
    for (unsigned index = 0; index < kKeptBoards; ++index) {
        std::optional<wjs::BoardRecord> record;
        if (mode == BoardMode::standard) {
            const std::size_t size = kSizes[index % kSizes.size()];
            record = measure_standard_session(size, corpus_seed(size, index)).at_rest.board;
        } else {
            const std::size_t size = kSessionSizes[index % kSessionSizes.size()];
            record = measure_session(phonics_phase(), size, corpus_seed(size, index)).at_rest.board;
        }
        if (!record) {
            std::abort();
        }
        const std::optional<wjs::BoardSnapshot> snapshot = wjs::snapshot_of(*record);
        if (!snapshot) {
            std::abort();
        }
        snapshots.push_back(*snapshot);
    }
    return snapshots;
}

[[nodiscard]] KeptStore make_kept_store(BoardMode mode) {
    KeptStore kept;
    kept.snapshots = kept_snapshots(mode);
    wjs::Store store = wjs::Store::in_memory();
    for (std::size_t which = 0; which < kept.snapshots.size(); ++which) {
        // Three boards a day, so that the days and the week are not one.
        const wjs::Day day = kKeptFirstDay + static_cast<wjs::Day>(which / 3);
        if (!store.record(kept.snapshots[which], day).stored) {
            std::abort();
        }
    }
    kept.data = store.data();
    kept.text = wjs::serialize_store(kept.data);
    return kept;
}

}  // namespace

const KeptStore& kept_store(BoardMode mode) {
    static const std::array<KeptStore, 2> stores = {make_kept_store(BoardMode::standard),
                                                    make_kept_store(BoardMode::phonics)};
    return stores[index_of(mode)];
}

wjs::StoreData heaviest_store() {
    wjs::StoreData data;
    data.stars = 123'456;
    data.next_board = 1'000'000;
    for (const int size : {3, 4, 5, 6}) {
        data.best.push_back({size, 18});
    }
    // Words of three to twenty letters, each one different: base 26, "aaa", "aab", ...
    const auto word_of = [](std::size_t number, std::size_t letters) {
        std::string word(letters, 'a');
        for (std::size_t at = letters; at-- > 0;) {
            word[at] = static_cast<char>('a' + number % 26);
            number /= 26;
        }
        return word;
    };
    // The longest words the store keeps, as they take the most room.
    for (std::size_t number = 0; number < wjs::max_vault_words; ++number) {
        data.vault.push_back({word_of(number, wjs::store_max_word_letters), kKeptFirstDay,
                              kKeptFirstDay + 30, 4'000'000'000U, 3'999'999'999U});
    }
    for (std::size_t which = 0; which < wjs::max_stored_boards; ++which) {
        wjs::StoredBoard board;
        board.serial = static_cast<std::uint32_t>(999'000 + which);
        board.day = kKeptFirstDay + static_cast<wjs::Day>(which);
        board.size = 6;
        board.phase = "A";
        board.tiles =
            "QU,QU,QU,QU,QU,QU/QU,QU,QU,QU,QU,QU/QU,QU,QU,QU,QU,QU/"
            "QU,QU,QU,QU,QU,QU/QU,QU,QU,QU,QU,QU/QU,QU,QU,QU,QU,QU";
        board.viable_total = 99'999;
        for (std::size_t listed = 0; listed < wjs::max_listed_viable_words; ++listed) {
            board.viable.push_back(word_of(which * 100 + listed, wjs::store_max_word_letters));
        }
        board.found = 99;
        board.stars = 99'999;
        board.cleared = true;
        data.boards.push_back(std::move(board));
    }
    for (std::size_t which = 0; which < wjs::max_stored_finds; ++which) {
        wjs::FindRecord find;
        find.word = word_of(which, wjs::store_max_word_letters);
        find.stars = wjs::max_find_stars;
        find.board_size = 6;
        find.phase = "A";
        find.day = kKeptFirstDay + static_cast<wjs::Day>(which % wjs::max_stored_days);
        find.start_tile_used = (which % 2) == 0;
        find.board = data.boards[which % data.boards.size()].serial;
        data.finds.push_back(std::move(find));
    }
    for (std::size_t which = 0; which < wjs::max_stored_days; ++which) {
        data.days.push_back({kKeptFirstDay + static_cast<wjs::Day>(which), 99'999U, 99'999U});
    }
    return data;
}

PhaseDirectory::PhaseDirectory() {
    const PhaseTexts& texts = phonics_phase_texts();
    const auto write = [this](const char* name, const std::string& text) {
        std::ofstream out(directory_.path() / name, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    };
    write("phase-a.graphemes.txt", texts.graphemes);
    write("phase-a.words.txt", texts.words);
    bytes_ = texts.graphemes.size() + texts.words.size();
}

PlayingSession::PlayingSession(BoardMode mode, std::size_t size, unsigned seed_index) {
    wjs::SessionOptions options;
    options.mode = mode == BoardMode::standard ? wjs::BoardMode::standard : wjs::BoardMode::phonics;
    const auto board_size = wjs::board_size_for(size);
    if (!board_size) {
        std::abort();
    }
    options.size = *board_size;
    options.seed = corpus_seed(size, seed_index);
    const wjs::SessionData data =
        mode == BoardMode::standard
            ? wjs::SessionData{.dictionary = english(), .dice = &dice_sets()}
            : wjs::SessionData{.dictionary = english(), .phase = &phonics_phase()};
    auto created = wjs::GameSession::create(data, options);
    if (!created) {
        std::abort();
    }
    session_ = std::make_unique<wjs::GameSession>(*std::move(created));
    if (!session_->shake() || !session_->still() || !session_->landed()) {
        std::abort();
    }
    const wjs::Dictionary& active =
        mode == BoardMode::standard ? english() : phonics_phase().dictionary();
    const std::vector<wjs::FoundWord> words = wjs::solve(*session_->board(), active);
    if (words.empty()) {
        std::abort();
    }
    found_path_ = words.front().path;
    if (session_->trace(found_path_, 1).kind != wjs::TraceKind::found) {
        std::abort();
    }
    // A path along the first row that is no word of the board: tracing it makes
    // nothing, however often it is traced.
    const wjs::Board& board = *session_->board();
    for (std::size_t length = board.columns(); length >= 3 && miss_path_.empty(); --length) {
        std::vector<wjs::Position> candidate;
        for (std::size_t column = 0; column < length; ++column) {
            candidate.push_back({0, column});
        }
        const auto spelled = wjs::validate_path(board, candidate);
        const bool is_viable =
            spelled && std::any_of(words.begin(), words.end(), [&](const wjs::FoundWord& word) {
                return word.word == *spelled;
            });
        if (spelled && !is_viable) {
            miss_path_ = candidate;
        }
    }
    if (miss_path_.empty()) {
        std::abort();
    }
    // Every path the board can be played with, to see the kinds that come back.
    miss_kind_ = session_->trace(miss_path_, 2).kind;
}

wjs::TraceKind PlayingSession::submit_miss(wjs::Stamp when) {
    return session_->trace(miss_path_, when).kind;
}

wjs::TraceKind PlayingSession::submit_again(wjs::Stamp when) {
    return session_->trace(found_path_, when).kind;
}

// ---------------------------------------------------------------------------
// Work
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::uint64_t file_bytes(const std::filesystem::path& file) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    return error ? 0 : static_cast<std::uint64_t>(size);
}

}  // namespace

StoreWrites write_through(const KeptStore& kept) {
    TempDirectory directory;
    auto opened = wjs::Store::open(directory.path() / "store.wjs");
    StoreWrites writes;
    for (std::size_t which = 0; which < kept.snapshots.size(); ++which) {
        const wjs::Day day = kKeptFirstDay + static_cast<wjs::Day>(which / 3);
        (void)opened.store.record(kept.snapshots[which], day);
        // The file is the whole store, rewritten: its size after a change is the
        // bytes that change wrote.
        writes.bytes += file_bytes(opened.store.file());
        ++writes.rewrites;
    }
    writes.final_bytes = file_bytes(opened.store.file());
    writes.final_equals_text = [&] {
        std::ifstream in(opened.store.file(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        return text == wjs::serialize_store(opened.store.data());
    }();
    return writes;
}

std::vector<Metric> store_metrics() {
    std::vector<Metric> metrics;
    for (const BoardMode mode : {BoardMode::standard, BoardMode::phonics}) {
        const KeptStore& kept = kept_store(mode);
        const budget::StoreBudget& allowed = budget::kStoreBudgets[index_of(mode)];
        const std::string label = mode_label(mode);

        // The records the twenty boards left: exact, they pin the corpus, the
        // dice or the phase, the deal and the store's bounds together.
        metrics.push_back(exact("store.boards." + label, mode, kept.data.boards.size(),
                                allowed.boards, "boards"));
        metrics.push_back(
            exact("store.finds." + label, mode, kept.data.finds.size(), allowed.finds, "finds"));
        metrics.push_back(exact("store.vault." + label, mode, kept.data.vault.size(),
                                allowed.vault_words, "words"));
        // What one save writes, whole: the file after the twenty boards.
        metrics.push_back(
            at_most("store.save.bytes." + label, mode, kept.text.size(), allowed.bytes, "bytes"));

        // What the saves of a play write: every change rewrites the file, on the
        // thread that made it.
        const StoreWrites writes = write_through(kept);
        metrics.push_back(at_most("store.write.bytes." + label, mode, writes.bytes,
                                  allowed.written_bytes, "bytes"));
        metrics.push_back(exact("store.write.rewrites." + label, mode, writes.rewrites,
                                kept.snapshots.size(), "files"));
    }

    // The heaviest store the format allows, which lists words: the phonics board's.
    const wjs::StoreData heaviest = heaviest_store();
    const std::string text = wjs::serialize_store(heaviest);
    metrics.push_back(at_most("store.heaviest.bytes", BoardMode::phonics, text.size(),
                              budget::kStoreHeaviestBytes, "bytes"));
    metrics.push_back(at_most("store.heaviest.of_cap", BoardMode::phonics, text.size(),
                              wjs::max_store_bytes, "bytes"));
    return metrics;
}

std::vector<Metric> dashboard_metrics() {
    constexpr BoardMode mode = BoardMode::phonics;
    const KeptStore& kept = kept_store(mode);
    const wjs::Dashboard dashboard =
        wjs::build_dashboard(kept.data, &phonics_phase(), kKeptFirstDay + 7, 1);

    std::uint64_t met = 0;
    std::uint64_t found = 0;
    for (const wjs::SoundRow& row : dashboard.sounds) {
        met += static_cast<std::uint64_t>(row.boards_met);
        found += static_cast<std::uint64_t>(row.words_found);
    }
    const budget::DashboardBudget& allowed = budget::kDashboardBudget;
    return {
        // A row for each grapheme of the phase, in the graphemes file's order.
        exact("dashboard.sounds", mode, dashboard.sounds.size(), allowed.sounds, "rows"),
        // What the rows add up to over the twenty boards the store keeps: exact,
        // the dashboard read against the store it was built from.
        exact("dashboard.boards_met", mode, met, allowed.boards_met, "boards"),
        exact("dashboard.words_found", mode, found, allowed.words_found, "words"),
        exact("dashboard.vault", mode, dashboard.vault.size(), allowed.vault, "stickers"),
        // What it is built from: the store's records, not the days of play.
        at_most("dashboard.inputs", mode,
                kept.data.boards.size() + kept.data.finds.size() + kept.data.vault.size(),
                allowed.inputs, "records"),
    };
}

std::vector<Metric> phase_load_metrics() {
    constexpr BoardMode mode = BoardMode::phonics;
    const PhaseDirectory directory;
    auto loaded = wjs::PhaseSet::load_directory(directory.path(), english());
    if (!loaded) {
        std::abort();
    }
    const wjs::PhaseIndex* index = loaded->find("A");
    if (index == nullptr) {
        std::abort();
    }
    const budget::PhaseLoadBudget& allowed = budget::kPhaseLoadBudget;
    return {
        exact("phase.load.phases", mode, loaded->phases().size(), allowed.phases, "phases"),
        // Exact: the same 249 words the seeding is measured on.
        exact("phase.load.words", mode, index->words().size(), allowed.words, "words"),
        exact("phase.load.graphemes", mode, index->graphemes().size(), allowed.graphemes,
              "graphemes"),
        at_most("phase.load.bytes", mode, directory.bytes(), allowed.bytes, "bytes"),
    };
}

std::vector<Metric> submit_metrics() {
    std::vector<Metric> metrics;
    for (const BoardMode mode : {BoardMode::standard, BoardMode::phonics}) {
        PlayingSession playing(mode, 4, 0);
        const std::string label = mode_label(mode);
        // What a submit does to the session, however many are made: a path that is
        // not a word is not recorded, and a word again is not a second find. The
        // outcome kinds are the session's own, so this holds what the trace
        // reports and not only what it costs.
        std::uint64_t finds_before = playing.session().finds().size();
        std::uint64_t misses_not_ignored = 0;
        std::uint64_t again_not_found_again = 0;
        for (wjs::Stamp when = 10; when < 110; ++when) {
            misses_not_ignored += playing.submit_miss(when) == wjs::TraceKind::found ? 1U : 0U;
            again_not_found_again +=
                playing.submit_again(when) == wjs::TraceKind::found_again ? 0U : 1U;
        }
        metrics.push_back(
            exact("submit.found_by_a_miss." + label, mode, misses_not_ignored, 0, "finds"));
        metrics.push_back(
            exact("submit.again_not_again." + label, mode, again_not_found_again, 0, "traces"));
        metrics.push_back(exact("submit.finds_added." + label, mode,
                                playing.session().finds().size() - finds_before, 0, "finds"));
    }
    return metrics;
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

std::vector<Metric> data_timing_metrics() {
    std::vector<Metric> metrics;
    const wjs::StoreData heaviest = heaviest_store();
    const std::string heaviest_text = wjs::serialize_store(heaviest);

    // The save a find waits for, at its heaviest, and the parse that start-up does.
    metrics.push_back(
        ratio("time.store.serialize", BoardMode::phonics,
              time_against_calibration([&] { return wjs::serialize_store(heaviest).size(); },
                                       budget::kDataRuns, Weight::light),
              budget::kStoreSerializeTime));
    metrics.push_back(ratio("time.store.parse", BoardMode::phonics,
                            time_against_calibration(
                                [&] {
                                    const auto parsed = wjs::parse_store(heaviest_text);
                                    return parsed ? parsed->data.finds.size() : 0U;
                                },
                                budget::kDataRuns, Weight::light),
                            budget::kStoreParseTime));
    {
        // The whole save to a file: the serialised text to a temporary file, flushed
        // and moved over the store.
        TempDirectory directory;
        const std::filesystem::path file = directory.path() / "store.wjs";
        {
            std::ofstream out(file, std::ios::binary);
            out << heaviest_text;
        }
        auto opened = wjs::Store::open(file);
        if (opened.status != wjs::LoadStatus::loaded) {
            std::abort();
        }
        metrics.push_back(
            ratio("time.store.save", BoardMode::phonics,
                  time_against_calibration(
                      [&] { return opened.store.save().has_value() ? file_bytes(file) : 0U; },
                      budget::kDataRuns, Weight::heavy),
                  budget::kStoreSaveTime));
    }

    const KeptStore& kept = kept_store(BoardMode::phonics);
    metrics.push_back(ratio("time.dashboard.build", BoardMode::phonics,
                            time_against_calibration(
                                [&] {
                                    const wjs::Dashboard dashboard = wjs::build_dashboard(
                                        kept.data, &phonics_phase(), kKeptFirstDay + 7, 1);
                                    return dashboard.sounds.size() + dashboard.vault.size();
                                },
                                budget::kDataRuns, Weight::light),
                            budget::kDashboardTime));

    {
        const PhaseDirectory directory;
        metrics.push_back(ratio("time.phase.load", BoardMode::phonics,
                                time_against_calibration(
                                    [&] {
                                        auto loaded = wjs::PhaseSet::load_directory(
                                            directory.path(), english());
                                        return loaded ? loaded->phases().size() : 0U;
                                    },
                                    budget::kDataRuns, Weight::heavy),
                                budget::kPhaseLoadTime));
    }

    for (const BoardMode mode : {BoardMode::standard, BoardMode::phonics}) {
        PlayingSession playing(mode, 4, 0);
        wjs::Stamp when = 100;
        metrics.push_back(
            ratio("time.submit." + mode_label(mode), mode,
                  time_against_calibration(
                      [&] {
                          std::size_t sum = 0;
                          for (int submit = 0; submit < budget::kSubmitsPerRun; ++submit) {
                              sum += static_cast<std::size_t>(playing.submit_miss(++when));
                              sum += static_cast<std::size_t>(playing.submit_again(++when));
                          }
                          return sum;
                      },
                      budget::kDataRuns, Weight::light),
                  budget::kSubmitTimes[index_of(mode)]));
    }

    {
        // A finger's path over the largest board the game plays, as the trace layer
        // hands it: every tile of the first row and the one below it, in turn.
        const wjs::Board board = random_board(6, 0);
        std::vector<wjs::Position> path;
        for (std::size_t row = 0; row < 2; ++row) {
            for (std::size_t column = 0; column < board.columns(); ++column) {
                path.push_back({row, row == 0 ? column : board.columns() - 1 - column});
            }
        }
        metrics.push_back(ratio("time.path.validate", BoardMode::standard,
                                time_against_calibration(
                                    [&] {
                                        std::size_t sum = 0;
                                        for (int trace = 0; trace < budget::kPathsPerRun; ++trace) {
                                            const auto spelled = wjs::validate_path(board, path);
                                            sum += spelled ? spelled->size() : 0U;
                                        }
                                        return sum;
                                    },
                                    budget::kDataRuns, Weight::light),
                                budget::kPathValidateTime));
    }
    return metrics;
}

}  // namespace wjs::perf
