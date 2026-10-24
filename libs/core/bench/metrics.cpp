// SPDX-License-Identifier: GPL-3.0-or-later
#include "metrics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "budgets.hpp"
#include "corpus.hpp"
#include "measure.hpp"
#include "metric_builders.hpp"
#include "perf_build.hpp"
#include "scene.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/scan/reader.hpp"
#include "wjs/scan/work.hpp"
#include "wjs/seeding.hpp"

namespace wjs::perf {
namespace {

[[nodiscard]] std::string size_label(std::size_t size) {
    return std::format("{}x{}", size, size);
}

// The frames the scanner's metrics read: fixed specs of the synthetic corpus
// (libs/scan/tests/synth), drawn from the tuning split so that the test split is not
// read for anything but the target. A 4x4, a 5x5, a 6x6 and a 4x4 under glare.
struct ScanFrame {
    scan::synth::Condition condition;
    std::size_t size;
    unsigned index;
};
constexpr std::array<ScanFrame, 4> kScanFrames = {{
    {scan::synth::Condition::upright, 4, 2},
    {scan::synth::Condition::angled, 5, 14},
    {scan::synth::Condition::loose, 6, 20},
    {scan::synth::Condition::glare, 4, 3},
}};

// The exact pins of the optimised reader's work live in budgets.hpp with the
// other thresholds (kScanBuffersAllocated, kScanCellsReadExact,
// kScanComparisonsExact, kScanBufferReuses): the scratch pool
// (libs/scan/src/scratch.hpp) is warmed by the read before the counted ones, and
// the four frames are all the same size, so no counted read allocates a pixel
// buffer, the height search reads its sample cells once at the winning height,
// and the pool's hand-outs are bounded. The numbers were measured with
// wjs_perf_tests "[.perf-report]" and moved only with that run's figures.

[[nodiscard]] scan::synth::Rendered draw_scan_frame(const ScanFrame& which) {
    scan::synth::Spec spec;
    spec.condition = which.condition;
    spec.split = scan::synth::Split::tuning;
    spec.size = which.size;
    spec.index = which.index;
    return scan::synth::render(spec);
}

}  // namespace

std::vector<Metric> dictionary_metrics() {
    const wjs::Dictionary& dictionary = english();
    return {
        exact("dictionary.words", BoardMode::standard, dictionary.size(), budget::kDictionaryWords,
              "words"),
        exact("dictionary.nodes", BoardMode::standard, trie_nodes(dictionary),
              budget::kDictionaryNodes, "nodes"),
    };
}

std::vector<Metric> random_board_metrics() {
    const wjs::Dictionary& dictionary = english();
    std::vector<Metric> metrics;
    for (std::size_t which = 0; which < kSizes.size(); ++which) {
        const std::size_t size = kSizes[which];
        std::uint64_t most = 0;
        unsigned seed = kNoSeed;
        for (unsigned index = 0; index < kBoardsPerSize; ++index) {
            const SolveWork work = measure_solve(random_board(size, index), dictionary);
            if (work.visits > most) {
                most = work.visits;
                seed = corpus_seed(size, index);
            }
        }
        metrics.push_back(at_most("solve.visits.max." + size_label(size), BoardMode::standard, most,
                                  budget::kMaxVisitsPerSize[which], "visits", seed));
    }
    return metrics;
}

std::vector<Metric> special_board_metrics() {
    const wjs::Dictionary& dictionary = english();
    std::vector<Metric> metrics;
    for (std::size_t i = 0; i < kRichBoards.size(); ++i) {
        const RichBoard& rich = kRichBoards[i];
        const SolveWork work = measure_solve(rich_board(rich), dictionary);
        metrics.push_back(at_most("solve.visits.rich." + std::string(rich.name),
                                  BoardMode::standard, work.visits, budget::kRichVisits[i],
                                  "visits"));
        metrics.push_back(exact("solve.words.rich." + std::string(rich.name), BoardMode::standard,
                                work.words, budget::kRichWords[i], "words"));
    }
    for (std::size_t i = 0; i < kEmptyBoards.size(); ++i) {
        const RichBoard& empty = kEmptyBoards[i];
        const SolveWork work = measure_solve(rich_board(empty), dictionary);
        metrics.push_back(exact("solve.visits.empty." + std::string(empty.name),
                                BoardMode::standard, work.visits, budget::kEmptyVisits[i],
                                "visits"));
        metrics.push_back(exact("solve.words.empty." + std::string(empty.name), BoardMode::standard,
                                work.words, 0, "words"));
    }
    return metrics;
}

std::vector<Metric> scan_metrics() {
    // Draw the frames first, then count only the reads: drawing allocates buffers too.
    std::vector<scan::synth::Rendered> frames;
    for (const ScanFrame& which : kScanFrames) {
        frames.push_back(draw_scan_frame(which));
    }
    // The template bank is built on first use; build it outside the count.
    (void)scan::read_board(frames.front().image.view());

    scan::ScanWork total;
    std::uint64_t answered = 0;
    for (const scan::synth::Rendered& frame : frames) {
        scan::reset_scan_work();
        const auto reading = scan::read_board(frame.image.view());
        answered += reading.has_value() ? 1U : 0U;
        const scan::ScanWork work = scan::scan_work();
        total.reads += work.reads;
        total.frame_pixels += work.frame_pixels;
        total.working_pixels += work.working_pixels;
        total.warp_pixels += work.warp_pixels;
        total.blobs += work.blobs;
        total.dice_candidates += work.dice_candidates;
        total.cells_read += work.cells_read;
        total.comparisons += work.comparisons;
        total.buffers += work.buffers;
        total.buffer_reuses += work.buffer_reuses;
        total.largest_buffers = std::max(total.largest_buffers, work.largest_buffers);
    }
    // Reading a frame seeds and solves nothing, so it does the same work whichever board
    // mode the board is played in; the rows carry the default mode, as the dictionary's do.
    return {
        exact("scan.reads", BoardMode::standard, total.reads, budget::kScanReads, "reads"),
        exact("scan.answered", BoardMode::standard, answered, budget::kScanReads, "boards"),
        exact("scan.frame_pixels", BoardMode::standard, total.frame_pixels,
              budget::kScanFramePixels, "pixels"),
        exact("scan.working_pixels", BoardMode::standard, total.working_pixels,
              budget::kScanWorkingPixels, "pixels"),
        at_most("scan.warp_pixels", BoardMode::standard, total.warp_pixels, budget::kScanWarpPixels,
                "pixels"),
        at_most("scan.blobs", BoardMode::standard, total.blobs, budget::kScanBlobs, "blobs"),
        at_most("scan.dice_candidates", BoardMode::standard, total.dice_candidates,
                budget::kScanDiceCandidates, "blobs"),
        at_most("scan.cells_read", BoardMode::standard, total.cells_read, budget::kScanCellsRead,
                "cells"),
        at_most("scan.comparisons", BoardMode::standard, total.comparisons,
                budget::kScanComparisons, "comparisons"),
        at_most("scan.buffers", BoardMode::standard, total.buffers, budget::kScanBuffers,
                "buffers"),
        at_most("scan.buffers.max", BoardMode::standard, total.largest_buffers,
                budget::kScanBuffersMax, "buffers"),
        // The scratch pool's exact promises: a warm read allocates nothing, the
        // height search reads its sample cells once at the winning height, and
        // the reuses are bounded (budgets.hpp holds the pins).
        exact("scan.buffers.allocated", BoardMode::standard, total.buffers,
              budget::kScanBuffersAllocated, "buffers"),
        exact("scan.cells_read.exact", BoardMode::standard, total.cells_read,
              budget::kScanCellsReadExact, "cells"),
        exact("scan.comparisons.exact", BoardMode::standard, total.comparisons,
              budget::kScanComparisonsExact, "comparisons"),
        at_most("scan.buffer.reuses", BoardMode::standard, total.buffer_reuses,
                budget::kScanBufferReuses, "reuses"),
    };
}

std::uint64_t corpus_digest() {
    const wjs::Dictionary& dictionary = english();
    Digest digest;
    for (const std::size_t size : kSizes) {
        for (unsigned index = 0; index < kBoardsPerSize; ++index) {
            digest.add(wjs::solve(random_board(size, index), dictionary));
        }
    }
    for (const RichBoard& rich : kRichBoards) {
        digest.add(wjs::solve(rich_board(rich), dictionary));
    }
    for (const RichBoard& empty : kEmptyBoards) {
        digest.add(wjs::solve(rich_board(empty), dictionary));
    }
    return digest.value();
}

std::vector<Metric> dice_board_metrics() {
    const wjs::Dictionary& dictionary = english();
    std::vector<Metric> metrics;
    for (std::size_t which = 0; which < kSizes.size(); ++which) {
        const std::size_t size = kSizes[which];
        std::uint64_t most = 0;
        unsigned seed = kNoSeed;
        for (unsigned index = 0; index < kBoardsPerSize; ++index) {
            const SolveWork work = measure_solve(dice_board(size, index), dictionary);
            if (work.visits > most) {
                most = work.visits;
                seed = corpus_seed(size, index);
            }
        }
        metrics.push_back(at_most("roll.visits.max." + size_label(size), BoardMode::standard, most,
                                  budget::kDiceMaxVisitsPerSize[which], "visits", seed));
    }
    return metrics;
}

std::uint64_t dice_digest() {
    const wjs::Dictionary& dictionary = english();
    Digest digest;
    for (const std::size_t size : kSizes) {
        for (unsigned index = 0; index < kBoardsPerSize; ++index) {
            digest.add(wjs::solve(dice_board(size, index), dictionary));
        }
    }
    return digest.value();
}

namespace {

// What seeding did over the kSeedsPerSize seeds of one size.
struct SeedTotals {
    std::uint64_t tries = 0;
    std::uint64_t solves = 0;
    std::uint64_t visits = 0;
    std::uint64_t most_tries = 0;
    std::uint64_t most_visits = 0;
    unsigned most_visits_seed = kNoSeed;
    // Boards that landed outside the range they were asked for without being
    // called a fallback, which the seeding promises it never does.
    std::uint64_t out_of_range = 0;
    std::uint64_t fallbacks = 0;
};

struct SeedCorpus {
    std::array<SeedTotals, kSizes.size()> sizes{};
    std::uint64_t digest = 0;
};

// The whole phonics corpus, made once: the metrics and the digest are two
// readings of the same seedings, and a seeding is the slow part of this tier.
[[nodiscard]] const SeedCorpus& seed_corpus() {
    static const SeedCorpus corpus = [] {
        const wjs::PhaseIndex& phase = phonics_phase();
        const wjs::SeedOptions defaults;
        SeedCorpus made;
        Digest digest;
        for (std::size_t which = 0; which < kSizes.size(); ++which) {
            const std::size_t size = kSizes[which];
            SeedTotals& totals = made.sizes[which];
            for (unsigned index = 0; index < kSeedsPerSize; ++index) {
                const unsigned seed = corpus_seed(size, index);
                const SeedRun run = measure_seed(phase, size, seed);
                const wjs::SeededBoard& board = run.board;

                totals.tries += board.tries;
                totals.solves += board.solves;
                totals.visits += run.visits;
                totals.most_tries = std::max<std::uint64_t>(totals.most_tries, board.tries);
                if (run.visits > totals.most_visits) {
                    totals.most_visits = run.visits;
                    totals.most_visits_seed = seed;
                }
                if (board.fallback) {
                    ++totals.fallbacks;
                } else if (board.viable.size() < defaults.min_viable ||
                           board.viable.size() > defaults.max_viable) {
                    ++totals.out_of_range;
                }
                digest.add(board);
            }
        }
        made.digest = digest.value();
        return made;
    }();
    return corpus;
}

}  // namespace

std::vector<Metric> seeding_metrics() {
    constexpr BoardMode mode = BoardMode::phonics;
    const wjs::PhaseIndex& phase = phonics_phase();
    const SeedCorpus& corpus = seed_corpus();

    std::vector<Metric> metrics;
    // The corpus is the words of the proxy phase: if the word list moved, this
    // is what says so before any seeding figure does.
    metrics.push_back(
        exact("seed.phase.words", mode, phase.words().size(), budget::kPhonicsWords, "words"));
    for (std::size_t which = 0; which < kSizes.size(); ++which) {
        const std::string label = size_label(kSizes[which]);
        const SeedTotals& totals = corpus.sizes[which];
        const budget::SeedBudget& allowed = budget::kSeedBudgets[which];

        metrics.push_back(at_most("seed.tries.total." + label, mode, totals.tries,
                                  allowed.tries_total, "verifications"));
        metrics.push_back(at_most("seed.solves.total." + label, mode, totals.solves,
                                  allowed.solves_total, "solves"));
        metrics.push_back(at_most("seed.visits.total." + label, mode, totals.visits,
                                  allowed.visits_total, "visits"));
        metrics.push_back(at_most("seed.visits.max." + label, mode, totals.most_visits,
                                  allowed.visits_max, "visits", totals.most_visits_seed));
        // The algorithm's own cap on verifications (SeedOptions::max_tries), not
        // a quarter above anything: no seeding may spend more.
        metrics.push_back(at_most("seed.tries.max." + label, mode, totals.most_tries,
                                  budget::kSeedMaxTries, "verifications"));
        metrics.push_back(at_most("seed.fallbacks." + label, mode, totals.fallbacks,
                                  allowed.fallbacks, "boards"));
        metrics.push_back(
            exact("seed.out_of_range." + label, mode, totals.out_of_range, 0, "boards"));
    }
    return metrics;
}

std::uint64_t seeding_digest() {
    return seed_corpus().digest;
}

namespace {

// What the game session spent over the kSeedsPerSize sessions of one size.
struct SessionTotals {
    std::uint64_t seedings = 0;
    std::uint64_t most_seedings = 0;
    std::uint64_t rolls = 0;
    std::uint64_t most_rolls = 0;
    unsigned most_rolls_seed = kNoSeed;
    std::uint64_t tries = 0;
    std::uint64_t solves = 0;
    std::uint64_t visits = 0;
    std::uint64_t most_visits = 0;
    unsigned most_visits_seed = kNoSeed;
    std::uint64_t out_of_range = 0;
    std::uint64_t empty = 0;
    std::uint64_t words = 0;
    std::uint64_t most_words = 0;
    std::uint64_t play_visits = 0;
    std::uint64_t play_solves = 0;
    std::uint64_t restore_solves = 0;
    std::uint64_t restore_visits = 0;
    // Boards whose record or snapshot said the other mode than the session played, or
    // whose words the record does not count as the deal did: none.
    std::uint64_t mismatched = 0;
};

struct SessionCorpus {
    std::vector<SessionTotals> sizes;
    std::uint64_t digest = 0;
};

// The sessions of one mode over `sizes`, each size kSeedsPerSize boards from its
// corpus seeds, each dealt and played through by `measure(size, seed)`. Made once
// for a mode, like the seeding corpus: the metrics and the digest read the same
// sessions.
template <typename Measure>
[[nodiscard]] SessionCorpus make_session_corpus(std::span<const std::size_t> sizes,
                                                Measure&& measure) {
    SessionCorpus made;
    Digest digest;
    for (const std::size_t size : sizes) {
        SessionTotals totals;
        for (unsigned index = 0; index < kSeedsPerSize; ++index) {
            const unsigned seed = corpus_seed(size, index);
            const SessionRun run = measure(size, seed);

            totals.seedings += run.deal.seedings;
            totals.most_seedings = std::max<std::uint64_t>(totals.most_seedings, run.deal.seedings);
            totals.rolls += run.deal.rolls;
            if (run.deal.rolls > totals.most_rolls) {
                totals.most_rolls = run.deal.rolls;
                totals.most_rolls_seed = seed;
            }
            totals.tries += run.deal.tries;
            totals.solves += run.deal_solves;
            totals.visits += run.deal_visits;
            if (run.deal_visits > totals.most_visits) {
                totals.most_visits = run.deal_visits;
                totals.most_visits_seed = seed;
            }
            if (!run.deal.in_range) {
                ++totals.out_of_range;
            }
            if (run.words == 0) {
                ++totals.empty;
            }
            totals.words += run.words;
            totals.most_words = std::max<std::uint64_t>(totals.most_words, run.words);
            totals.play_visits += run.play_visits;
            totals.play_solves += run.play_solves;
            totals.restore_visits += run.restore_visits;
            totals.restore_solves += run.restore_solves;
            // Every board the session handed out says what the deal did.
            const bool standard = run.mode == wjs::BoardMode::standard;
            const bool says_so = run.after_first_find.board.has_value() &&
                                 run.after_first_find.mode == run.mode &&
                                 run.after_first_find.board->mode == run.mode &&
                                 run.after_first_find.board->viable_count() == run.deal.viable &&
                                 run.at_rest.board.has_value() &&
                                 run.at_rest.board->viable_count() == run.deal.viable &&
                                 run.at_rest.board->phase.empty() == standard;
            if (!says_so || run.deal.viable != run.words) {
                ++totals.mismatched;
            }
            digest.add(run);
        }
        made.sizes.push_back(totals);
    }
    made.digest = digest.value();
    return made;
}

[[nodiscard]] const SessionCorpus& session_corpus() {
    static const SessionCorpus corpus = [] {
        const wjs::PhaseIndex& phase = phonics_phase();
        return make_session_corpus(kSessionSizes, [&](std::size_t size, unsigned seed) {
            return measure_session(phase, size, seed);
        });
    }();
    return corpus;
}

[[nodiscard]] const SessionCorpus& standard_session_corpus() {
    static const SessionCorpus corpus = make_session_corpus(
        kSizes,
        [](std::size_t size, unsigned seed) { return measure_standard_session(size, seed); });
    return corpus;
}

}  // namespace

std::vector<Metric> session_metrics() {
    constexpr BoardMode mode = BoardMode::phonics;
    const SessionCorpus& corpus = session_corpus();

    std::vector<Metric> metrics;
    for (std::size_t which = 0; which < kSessionSizes.size(); ++which) {
        const std::string label = size_label(kSessionSizes[which]);
        const SessionTotals& totals = corpus.sizes[which];
        const budget::SessionBudget& allowed = budget::kSessionBudgets[which];

        metrics.push_back(at_most("session.seedings.total." + label, mode, totals.seedings,
                                  allowed.seedings_total, "seedings"));
        // The session's own cap (SessionOptions::max_seedings), not a quarter above
        // anything: no board may take more.
        metrics.push_back(at_most("session.seedings.max." + label, mode, totals.most_seedings,
                                  budget::kSessionMaxSeedings, "seedings"));
        metrics.push_back(at_most("session.tries.total." + label, mode, totals.tries,
                                  allowed.tries_total, "verifications"));
        metrics.push_back(at_most("session.solves.total." + label, mode, totals.solves,
                                  allowed.solves_total, "solves"));
        metrics.push_back(at_most("session.visits.total." + label, mode, totals.visits,
                                  allowed.visits_total, "visits"));
        metrics.push_back(at_most("session.visits.max." + label, mode, totals.most_visits,
                                  allowed.visits_max, "visits", totals.most_visits_seed));
        metrics.push_back(at_most("session.out_of_range." + label, mode, totals.out_of_range,
                                  allowed.out_of_range, "boards"));
        // A board with no word on it is never dealt.
        metrics.push_back(exact("session.empty." + label, mode, totals.empty, 0, "boards"));
        metrics.push_back(exact("session.words.total." + label, mode, totals.words,
                                allowed.words_total, "words"));
        // Tracing, asking for help, recording and saving read what the deal kept.
        metrics.push_back(
            exact("session.play.solves." + label, mode, totals.play_solves, 0, "solves"));
        metrics.push_back(
            exact("session.play.visits." + label, mode, totals.play_visits, 0, "visits"));
        // A restore reads the board again with one solve.
        metrics.push_back(exact("session.restore.solves." + label, mode, totals.restore_solves,
                                kSeedsPerSize, "solves"));
        metrics.push_back(at_most("session.restore.visits." + label, mode, totals.restore_visits,
                                  allowed.restore_visits, "visits"));
        // Every record and snapshot says the mode and the count the deal made.
        metrics.push_back(
            exact("session.mismatched." + label, mode, totals.mismatched, 0, "boards"));
    }
    return metrics;
}

std::uint64_t session_digest() {
    return session_corpus().digest;
}

std::vector<Metric> standard_session_metrics() {
    constexpr BoardMode mode = BoardMode::standard;
    const SessionCorpus& corpus = standard_session_corpus();

    std::vector<Metric> metrics;
    for (std::size_t which = 0; which < kSizes.size(); ++which) {
        const std::string label = size_label(kSizes[which]);
        const SessionTotals& totals = corpus.sizes[which];
        const budget::StandardSessionBudget& allowed = budget::kStandardSessionBudgets[which];

        // A roll is a solve, so the rolls are what the deal cost beyond the first
        // board, and the visits are the solver's own tally of them.
        metrics.push_back(at_most("session.standard.rolls.total." + label, mode, totals.rolls,
                                  allowed.rolls_total, "rolls"));
        // The most rolls any one board took, which is a quarter above what the worst
        // measured and far under the session's own cap (SessionOptions::max_rolls):
        // a roll that holds no word is rare, and several in a row are rarer.
        metrics.push_back(at_most("session.standard.rolls.max." + label, mode, totals.most_rolls,
                                  allowed.rolls_max, "rolls", totals.most_rolls_seed));
        metrics.push_back(at_most("session.standard.visits.total." + label, mode, totals.visits,
                                  allowed.visits_total, "visits"));
        metrics.push_back(at_most("session.standard.visits.max." + label, mode, totals.most_visits,
                                  allowed.visits_max, "visits", totals.most_visits_seed));
        // A board with no word on it is never dealt.
        metrics.push_back(
            exact("session.standard.empty." + label, mode, totals.empty, 0, "boards"));
        // Exact: it pins the dice, the roll, the dictionary and the deal together.
        metrics.push_back(exact("session.standard.words.total." + label, mode, totals.words,
                                allowed.words_total, "words"));
        // Tracing, asking for help, recording and saving read what the deal kept,
        // however many words there are: no solve at all.
        metrics.push_back(
            exact("session.standard.play.solves." + label, mode, totals.play_solves, 0, "solves"));
        metrics.push_back(
            exact("session.standard.play.visits." + label, mode, totals.play_visits, 0, "visits"));
        // A restore reads the board again with one solve.
        metrics.push_back(exact("session.standard.restore.solves." + label, mode,
                                totals.restore_solves, kSeedsPerSize, "solves"));
        metrics.push_back(at_most("session.standard.restore.visits." + label, mode,
                                  totals.restore_visits, allowed.restore_visits, "visits"));
        // Every record and snapshot says the mode and the count the deal made.
        metrics.push_back(
            exact("session.standard.mismatched." + label, mode, totals.mismatched, 0, "boards"));
    }
    return metrics;
}

std::uint64_t standard_session_digest() {
    return standard_session_corpus().digest;
}

std::vector<Metric> binder_metrics() {
    std::vector<Metric> metrics;
    for (std::size_t i = 0; i < kBindPaths.size(); ++i) {
        const BindPath& path = kBindPaths[i];
        const std::vector<std::string> tiles = bind_tiles(path);
        const BindRun run = measure_bind(phonics_binder(), tiles);
        metrics.push_back(at_most(
            "bind.comparisons." + std::string(path.name) + "." + std::to_string(tiles.size()),
            BoardMode::phonics, run.comparisons, budget::kBindComparisons[i], "comparisons"));
    }
    return metrics;
}

std::vector<Metric> load_timing_metrics() {
    const RelativeTime time = time_against_calibration(
        [] {
            const auto loaded = wjs::Dictionary::load_file(std::string(kDictionaryPath));
            return loaded ? loaded->size() : 0U;
        },
        budget::kLoadRuns, Weight::heavy);
    return {ratio("time.load_dictionary", BoardMode::standard, time, budget::kLoadTime)};
}

std::vector<Metric> solve_timing_metrics() {
    const wjs::Dictionary& dictionary = english();
    std::vector<Metric> metrics;
    const struct {
        std::size_t rich;
        const budget::TimeBudget& allowed;
    } cases[] = {{0, budget::kSolveRich4Time}, {2, budget::kSolveRich6Time}};
    for (const auto& one : cases) {
        const RichBoard& rich = kRichBoards[one.rich];
        const wjs::Board board = rich_board(rich);
        const RelativeTime time = time_against_calibration(
            [&] {
                std::size_t words = 0;
                for (int i = 0; i < budget::kSolvesPerRun; ++i) {
                    words += wjs::solve(board, dictionary).size();
                }
                return words;
            },
            budget::kSolveRuns, Weight::light);
        // The task is kSolvesPerRun solves, so the ratio is for the batch; the
        // metric is one solve's, and the note says what that was.
        Metric metric =
            ratio("time.solve." + std::string(rich.name), BoardMode::standard, time, one.allowed);
        metric.measured = time.ratio / static_cast<double>(budget::kSolvesPerRun);
        metric.note += std::format("; one solve {:.3f} ms",
                                   time.task_ms / static_cast<double>(budget::kSolvesPerRun));
        metrics.push_back(std::move(metric));
    }
    return metrics;
}

std::vector<Metric> session_timing_metrics() {
    const wjs::PhaseIndex& phase = phonics_phase();
    const budget::TimeBudget* const allowed[] = {&budget::kDeal3Time, &budget::kDeal4Time};

    std::vector<Metric> metrics;
    for (std::size_t which = 0; which < kSessionSizes.size(); ++which) {
        const std::size_t size = kSessionSizes[which];
        const RelativeTime time = time_against_calibration(
            [&] {
                std::size_t words = 0;
                for (int i = 0; i < budget::kDealsPerRun; ++i) {
                    words += measure_session_deal(phase, size,
                                                  corpus_seed(size, static_cast<unsigned>(i)));
                }
                return words;
            },
            budget::kDealRuns, Weight::light);
        // The task is kDealsPerRun deals, so the ratio is for the batch; the
        // metric is one deal's, and the note says what that was.
        Metric metric =
            ratio("time.deal." + size_label(size), BoardMode::phonics, time, *allowed[which]);
        metric.measured = time.ratio / static_cast<double>(budget::kDealsPerRun);
        metric.note += std::format("; one deal {:.3f} ms",
                                   time.task_ms / static_cast<double>(budget::kDealsPerRun));
        metrics.push_back(std::move(metric));
    }
    return metrics;
}

std::vector<Metric> standard_session_timing_metrics() {
    std::vector<Metric> metrics;
    for (std::size_t which = 0; which < kSizes.size(); ++which) {
        const std::size_t size = kSizes[which];
        const RelativeTime time = time_against_calibration(
            [&] {
                std::size_t words = 0;
                for (int i = 0; i < budget::kDealsPerRun; ++i) {
                    words += measure_standard_session_deal(
                        size, corpus_seed(size, static_cast<unsigned>(i)));
                }
                return words;
            },
            budget::kDealRuns, Weight::light);
        // The task is kDealsPerRun deals, so the ratio is for the batch; the
        // metric is one deal's, and the note says what that was.
        Metric metric = ratio("time.deal.standard." + size_label(size), BoardMode::standard, time,
                              budget::kStandardDealTimes[which]);
        metric.measured = time.ratio / static_cast<double>(budget::kDealsPerRun);
        metric.note += std::format("; one deal {:.3f} ms",
                                   time.task_ms / static_cast<double>(budget::kDealsPerRun));
        metrics.push_back(std::move(metric));
    }
    return metrics;
}

std::vector<Metric> scan_timing_metrics() {
    std::vector<Metric> metrics;
    const struct {
        std::size_t which;
        const budget::TimeBudget& allowed;
    } cases[] = {{0, budget::kScanRead4Time},
                 {1, budget::kScanRead5Time},
                 {2, budget::kScanRead6Time},
                 {3, budget::kScanReadGlareTime}};
    for (const auto& one : cases) {
        const ScanFrame& which = kScanFrames[one.which];
        const scan::synth::Rendered frame = draw_scan_frame(which);
        const RelativeTime time = time_against_calibration(
            [&] {
                const auto reading = scan::read_board(frame.image.view());
                return reading ? reading->tiles.size() : 0U;
            },
            budget::kScanRuns, Weight::heavy);
        // The glare frame is a 4x4 as the first is; the angled 5x5 and the loose 6x6
        // are named for their sizes, and the glare one for its condition.
        const std::string label = which.condition == scan::synth::Condition::glare
                                      ? std::string("glare")
                                      : size_label(which.size);
        metrics.push_back(ratio("time.read." + label, BoardMode::standard, time, one.allowed));
    }
    return metrics;
}

}  // namespace wjs::perf
