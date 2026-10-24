// SPDX-License-Identifier: GPL-3.0-or-later
// The reader held to the target of docs/design/scanner.md, over the synthetic
// corpus (libs/scan/tests/synth).
//
// The full test split runs in an optimised build, where the corpus takes some
// seconds; in Debug with the sanitizers it is much longer, so the target is skipped
// there (reported as skipped, not failed) and a smoke subset runs in every build.
// The Release leg of CI (ctest --preset linux-gcc-release) is where the target is
// held on every push.
//
// A target is moved only with a reason written beside it in the design note, never
// to turn a red run green.
//
// The hidden cases ([.scan-...] and [.recorded]) are tools, not tests: they print
// what the reader measures, and are run by name. They read the tuning split unless
// WJS_SCAN_SPLIT=test is set, so that the reader is developed against the one split
// and the other is looked at to report, not to steer.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "evaluate.hpp"
#include "png.hpp"
#include "scene.hpp"
#include "wjs/board.hpp"
#include "wjs/scan/board_finder.hpp"
#include "wjs/scan/glyph_reader.hpp"
#include "wjs/scan/reader.hpp"
#include "wjs/scan/stroke_font.hpp"

namespace {

using namespace wjs::scan;
using namespace wjs::scan::synth;

// Read through volatile: an optimising compiler that could see the value would see that SKIP
// always ends the case, and warn (C4702) about the statements after it.
const volatile bool kOptimised = WJS_SCAN_OPTIMISED != 0;

// A call and not `if constexpr (kOptimised)`: SKIP ends the case, and where the
// compiler can see that it always does, the statements after it are unreachable
// code, which is a warning and so an error.
bool optimised_build() {
    return kOptimised;
}

void skip_unless_optimised() {
    if (!optimised_build()) {
        SKIP("the scanner's target is held in an optimised build, and this is "
             << WJS_SCAN_BUILD_TYPE
             << "; the corpus is much longer here and the optimised legs hold it "
                "(docs/design/scanner.md, \"Where it runs\")");
    }
}

// --- how the targets are set, and what they are held against -------------------------
//
// The numbers a test holds the reader to were once chosen by looking at the split they
// were then held against, and three of them were relaxed after the first run on it
// missed (upright silent and flagged, the glare found rate). A bound set that way says
// the reader passes, not that it generalises. They are now derived by a rule, from one
// split, and held against another that nothing has been chosen from:
//
//   - The reader is developed on the tuning split (Split::tuning), and the figures it
//     gives, as counts, are recorded below in kTuning. Nothing else is looked at to set
//     a bound.
//   - A bound is a figure of kTuning moved to the unfavourable side by three standard
//     errors of a proportion, then rounded outward to the next half percent. The
//     standard error is of the adjusted proportion (x + 2) / (n + 4), so that a figure
//     of 0 or all does not have an error of nothing. See derive() below. The bound is
//     what the split's own sampling noise allows for, which is not much: 24 boards a
//     condition is 558 tiles, and one tile in a thousand is about the finest the test
//     can tell. Targets such as "0.3% silent" in docs/design/scanner.md cannot be
//     confirmed or refuted by it, and are not claimed.
//   - The bounds are held against the held-out split (Split::heldout), read by the
//     target tests and by nothing that steers. If a bound is missed there the test is
//     red, and the reader is not changed "until it passes" by looking at that split: a
//     reader change is made against the tuning split, the figures in kTuning are
//     re-recorded from "[.scan-thresholds]" with the reason in the commit message, and
//     the held-out split, having been looked at to steer, is retired and replaced with
//     a new base seed (libs/scan/tests/synth/scene.cpp, seed_of).
//   - The turn of the board is judged by its corners, not by whichever of the four
//     turns of the truth fits best (that scored a board numbered from the wrong corner
//     as right). No rotation error is allowed in any condition: one is a fault in how
//     the corners are numbered, not noise.
//
// What was measured when this was written (Release, MSVC), the figures of each split being
// the counts "[.scan-report]" prints:
//
//   tuning:  see kTuning.
//   test (the second split, read and reported on while the reader changed, so not held
//            out): upright correct 0.9749, silent 0.0054 (3), flagged 0.1272; angled
//            correct 0.9875, silent 0; loose correct 0.9875, silent 0.0018 (1); glare found
//            0.9167, correct 0.9923, silent 0; no rotation error.
//   heldout (base seed 6,000,000), the first reading of it, with the bounds above already
//            fixed: found 24 of 24 in every condition; correct, flagged, silent, offered,
//            of 558 tiles: upright 554, 62, 0, 558; angled 545, 57, 2, 558; loose 549, 66,
//            2, 557; glare 552, 74, 1, 556. No rotation error. Angled correct (0.9767)
//            is 0.0017 over its bound (0.975): the bound is as close as the corpus can
//            place one.
//   negatives (21 frames with no board on the tuning seeds and 60 on the others): all 81
//            are answered with an error.
//
// None of it is a measure of real dice: the corpus draws the reader's own stroke skeleton
// (docs/design/scanner.md, "The corpus plan"), so every figure here says how the reader
// does on a typeface it was built to read. No real frame has been read by anything in
// this repository, and no figure for real dice or a real camera exists; "[.recorded]"
// is the harness for the first.

// What the tuning split gave: counts, over the tiles of the frames in which the board was
// found. Re-recorded from "[.scan-thresholds]" when the reader changes (see above).
struct Figures {
    Condition condition;
    std::size_t frames;
    std::size_t found;
    std::size_t tiles;
    std::size_t correct;
    std::size_t flagged;
    std::size_t silent;
    std::size_t offered;
};

constexpr Figures kTuning[] = {
    {Condition::upright, 24, 24, 558, 551, 53, 2, 558},
    {Condition::angled, 24, 24, 558, 553, 64, 0, 558},
    {Condition::loose, 24, 24, 558, 551, 71, 0, 557},
    {Condition::glare, 24, 22, 526, 510, 76, 3, 523},
};

// One row of the target.
struct Target {
    Condition condition;
    double found;    // at least
    double correct;  // at least
    double silent;   // at most
    double flagged;  // at most
    double offered;  // at least
};

// Three standard errors of the adjusted proportion of x in n.
double margin_of(std::size_t x, std::size_t n) {
    const double adjusted = (static_cast<double>(x) + 2.0) / (static_cast<double>(n) + 4.0);
    return 3.0 * std::sqrt(adjusted * (1.0 - adjusted) / (static_cast<double>(n) + 4.0));
}

// The least a rate may be: the figure less its margin, rounded down to a half percent.
double floor_of(std::size_t x, std::size_t n) {
    const double rate = static_cast<double>(x) / static_cast<double>(n);
    return std::max(0.0, std::floor((rate - margin_of(x, n)) * 200.0) / 200.0);
}

// The most a rate may be: the figure plus its margin, rounded up to a half percent.
double ceiling_of(std::size_t x, std::size_t n) {
    const double rate = static_cast<double>(x) / static_cast<double>(n);
    return std::min(1.0, std::ceil((rate + margin_of(x, n)) * 200.0) / 200.0);
}

Target derive(const Figures& f) {
    return {f.condition,
            floor_of(f.found, f.frames),
            floor_of(f.correct, f.tiles),
            ceiling_of(f.silent, f.tiles),
            ceiling_of(f.flagged, f.tiles),
            floor_of(f.offered, f.tiles)};
}

std::vector<Target> targets() {
    std::vector<Target> out;
    for (const Figures& figures : kTuning) {
        out.push_back(derive(figures));
    }
    return out;
}

std::string describe(const Tally& t) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << "frames " << t.frames << ", found "
        << t.found_rate() << " (" << t.found << "), tiles " << t.tiles << ", correct "
        << t.correct_rate() << " (" << t.correct << "), flagged " << t.flagged_rate() << " ("
        << t.flagged << "), silent " << t.silent_rate() << " (" << t.silent << "), offered "
        << t.offered_rate() << " (" << t.offered << "), turned wrong " << t.rotation_errors;
    return out.str();
}

std::string describe(const Target& target) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << "[target found>=" << target.found
        << " correct>=" << target.correct << " silent<=" << target.silent
        << " flagged<=" << target.flagged << " offered>=" << target.offered << " turned wrong 0]";
    return out.str();
}

void check_target(Condition condition) {
    const Target* target = nullptr;
    const std::vector<Target> all = targets();
    for (const Target& candidate : all) {
        if (candidate.condition == condition) {
            target = &candidate;
        }
    }
    REQUIRE(target != nullptr);

    const Tally tally = evaluate(condition, Split::heldout, ReadOptions{});
    INFO(name(condition) << ": " << describe(tally) << "  " << describe(*target));
    CHECK(tally.found_rate() >= target->found);
    CHECK(tally.correct_rate() >= target->correct);
    CHECK(tally.silent_rate() <= target->silent);
    CHECK(tally.flagged_rate() <= target->flagged);
    CHECK(tally.offered_rate() >= target->offered);
    CHECK(tally.rotation_errors == 0);
}

// The split the hidden cases read: the tuning one unless WJS_SCAN_SPLIT names another
// ("test" or "heldout"). Reading "heldout" with a tool is looking at it.
Split report_split() {
    const char* wanted = std::getenv("WJS_SCAN_SPLIT");  // NOLINT: read once, at start
    if (wanted != nullptr && std::string_view(wanted) == "test") {
        return Split::test;
    }
    if (wanted != nullptr && std::string_view(wanted) == "heldout") {
        return Split::heldout;
    }
    return Split::tuning;
}

// What a board read from a frame with no board in it came to: one with most of its
// tiles trusted is a board believed, which is the failure.
bool believed(const BoardReading& reading) {
    std::size_t trusted = 0;
    for (const TileReading& tile : reading.tiles) {
        trusted += (!tile.doubtful && !tile.letters.empty()) ? 1U : 0U;
    }
    return trusted * 2 > reading.tiles.size();
}

}  // namespace

TEST_CASE("upright boards are read to the target", "[scan][corpus]") {
    skip_unless_optimised();
    check_target(Condition::upright);
}

TEST_CASE("angled boards are read to the target", "[scan][corpus]") {
    skip_unless_optimised();
    check_target(Condition::angled);
}

TEST_CASE("boards of loose dice are read to the target", "[scan][corpus]") {
    skip_unless_optimised();
    check_target(Condition::loose);
}

TEST_CASE("boards under glare are read to the target", "[scan][corpus]") {
    skip_unless_optimised();
    check_target(Condition::glare);
}

TEST_CASE("frames with no board are not read as one", "[scan][corpus]") {
    skip_unless_optimised();
    std::size_t frames = 0;
    std::size_t rejected = 0;
    std::size_t trusted_boards = 0;
    for (std::size_t k = 0; k < kNegativeKindCount; ++k) {
        for (unsigned i = 0; i < 7; ++i) {
            const GrayImage image = render_negative(static_cast<NegativeKind>(k), i);
            const auto reading = read_board(image.view());
            ++frames;
            if (!reading) {
                ++rejected;
            } else if (believed(*reading)) {
                ++trusted_boards;
            }
        }
    }
    INFO("frames " << frames << ", answered with an error " << rejected << ", believed "
                   << trusted_boards);
    CHECK(rejected * 100 >= frames * 95);
    CHECK(trusted_boards == 0);
}

TEST_CASE("the judge counts a board numbered from the wrong corner as wrong", "[scan][smoke]") {
    // A 2 x 2 board, a b / c d, and the readings of it a reader could make.
    const std::vector<std::string> truth = {"A", "B", "C", "D"};
    const auto reading_of = [](std::initializer_list<const char*> letters) {
        BoardReading reading;
        reading.rows = 2;
        reading.columns = 2;
        for (const char* l : letters) {
            TileReading tile;
            tile.letters = l;
            tile.doubtful = false;
            reading.tiles.push_back(tile);
        }
        return std::expected<BoardReading, ReadError>(reading);
    };
    const auto upright = reading_of({"A", "B", "C", "D"});
    const auto quarter = reading_of({"C", "A", "D", "B"});  // the board turned once, clockwise

    // Judged in the turn it is in, each is all right; judged in the wrong one, it is all
    // wrong and silent, and the judge says that a different turn would have fitted.
    const Tally right = judge_turned(truth, 2, 0, upright);
    CHECK(right.correct == 4);
    CHECK(right.rotation_errors == 0);
    CHECK(judge_turned(truth, 2, 1, quarter).correct == 4);
    const Tally wrong = judge_turned(truth, 2, 0, quarter);
    CHECK(wrong.correct == 0);
    CHECK(wrong.silent == 4);
    CHECK(wrong.rotation_errors == 1);
    CHECK(judge_turned(truth, 2, 3, upright).rotation_errors == 1);
    // Not found is not a rotation error, and neither is a board of another size.
    CHECK(judge_turned(truth, 2, 0, std::unexpected(ReadError::board_not_found)).rotation_errors ==
          0);
    CHECK(judge_turned(truth, 3, 0, upright).found == 0);
}

TEST_CASE("the turn of a board is the one its corners give", "[scan][smoke]") {
    // The corners of a board in its own order, as the picture shows them: top left, top right,
    // bottom right, bottom left of the board. In the picture's square a (0,0), b (1,0), c (1,1),
    // d (0,1), a board lying upright has them a b c d; turned once clockwise (its top left
    // now at the top right), b c d a; turned once anticlockwise, d a b c.
    const Point a{0, 0};
    const Point b{100, 0};
    const Point c{100, 100};
    const Point d{0, 100};
    CHECK(expected_quarters({a, b, c, d}) == 0);
    CHECK(expected_quarters({b, c, d, a}) == 1);
    CHECK(expected_quarters({c, d, a, b}) == 2);
    CHECK(expected_quarters({d, a, b, c}) == 3);
    // A tilt of 35 degrees either way does not change which edge is the flattest.
    const auto rotated = [](Quad quad, double degrees) {
        const double s = std::sin(degrees * 3.14159265358979 / 180.0);
        const double k = std::cos(degrees * 3.14159265358979 / 180.0);
        for (Point& p : quad) {
            p = {p.x * k - p.y * s, p.x * s + p.y * k};
        }
        return quad;
    };
    for (const double degrees : {-35.0, -10.0, 10.0, 35.0}) {
        CHECK(expected_quarters(rotated({a, b, c, d}, degrees)) == 0);
        CHECK(expected_quarters(rotated({b, c, d, a}, degrees)) == 1);
    }

    // The corpus agrees: upright frames lie within 10 degrees, so they are never turned.
    for (unsigned index = 0; index < 6; ++index) {
        Spec spec;
        spec.condition = Condition::upright;
        spec.split = Split::tuning;
        spec.size = 4;
        spec.index = index;
        CHECK(expected_quarters(render(spec).corners) == 0);
    }
}

TEST_CASE("a board of each condition is read in any build", "[scan][smoke]") {
    // One 4x4 board of each condition, read end to end. Not the target: it is that
    // the pipeline runs clean (under the sanitizers in Debug) and gets the plain
    // cases right, and the glare one is only asked not to crash.
    for (std::size_t c = 0; c < kConditionCount; ++c) {
        Spec spec;
        spec.condition = static_cast<Condition>(c);
        spec.split = Split::tuning;
        spec.size = 4;
        spec.index = 1;
        const Rendered frame = render(spec);
        const auto reading = read_board(frame.image.view());
        const Tally tally = judge(frame, reading);
        INFO(name(spec.condition) << " seed " << frame.seed << ": " << describe(tally));
        if (spec.condition == Condition::glare) {
            continue;
        }
        REQUIRE(tally.found == 1);
        CHECK(tally.correct * 10 >= tally.tiles * 8);
    }
}

TEST_CASE("a frame with no board is not read as one in any build", "[scan][smoke]") {
    for (std::size_t k = 0; k < kNegativeKindCount; ++k) {
        const GrayImage image = render_negative(static_cast<NegativeKind>(k), 0);
        const auto reading = read_board(image.view());
        INFO("kind " << k);
        // Nothing here is a board, and the reader says so: an answer of any kind, doubted
        // or not, is a failure. (This used to let a doubted board through.)
        CHECK_FALSE(reading.has_value());
    }
}

// --- tools, not tests: hidden, run by name ---------------------------------------

TEST_CASE("what the corpus measures now", "[.scan-report]") {
    // wjs_scan_tests "[.scan-report]": the five numbers of each condition, and what
    // drawing and reading a frame cost.
    for (const Target& target : targets()) {
        const auto start = std::chrono::steady_clock::now();
        const Tally tally = evaluate(target.condition, report_split(), ReadOptions{});
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
        std::ostringstream cost;
        cost << std::fixed << std::setprecision(0) << elapsed.count();
        std::cout << std::setw(8) << name(target.condition) << ": " << describe(tally) << "  "
                  << describe(target) << "  " << cost.str() << " ms for " << tally.frames
                  << " frames (draw and read)\n";
    }
}

TEST_CASE("what the tuning split gives, and the bounds that follow from it", "[.scan-thresholds]") {
    // The figures to record in kTuning when the reader changes, and the bounds they
    // derive. Always the tuning split: this is the one place the bounds are set from.
    for (const Figures& recorded : kTuning) {
        const Tally now = evaluate(recorded.condition, Split::tuning, ReadOptions{});
        const bool same = now.frames == recorded.frames && now.found == recorded.found &&
                          now.tiles == recorded.tiles && now.correct == recorded.correct &&
                          now.flagged == recorded.flagged && now.silent == recorded.silent &&
                          now.offered == recorded.offered;
        std::cout << "    {Condition::" << name(recorded.condition) << ", " << now.frames << ", "
                  << now.found << ", " << now.tiles << ", " << now.correct << ", " << now.flagged
                  << ", " << now.silent << ", " << now.offered << "},"
                  << (same ? "  // as recorded" : "  // DIFFERS from kTuning") << "\n";
        std::cout << "        "
                  << describe(derive({recorded.condition, now.frames, now.found, now.tiles,
                                      now.correct, now.flagged, now.silent, now.offered}))
                  << "\n";
    }
}

TEST_CASE("how the doubt trades off against silent errors", "[.scan-tune]") {
    // The thresholds are chosen on the tuning split and nothing else. This reads the
    // split with the doubt switched off (so every tile carries its raw score and
    // margin) and prints, for each bound on the silent errors, the least doubt that
    // keeps under it. The score floor matters little and is held at 0.5.
    ReadOptions open;
    open.min_score = -2.0;
    open.min_margin = -2.0;
    open.min_contrast = 0.0;
    std::vector<TileRecord> records;
    for (const Figures& figures : kTuning) {
        const Tally frames = evaluate(figures.condition, Split::tuning, open, &records);
        std::cout << name(figures.condition) << ": " << describe(frames) << "\n";
    }
    std::cout << "silent<=  margin  flagged  silent  correct  offered\n";
    for (const double bound : {0.002, 0.003, 0.005, 0.01, 0.02}) {
        for (double margin = 0.0; margin < 2.0; margin += 0.01) {
            const Tally t = with_thresholds(records, 0.5, margin);
            if (t.silent_rate() <= bound) {
                std::cout << std::fixed << std::setprecision(3) << std::setw(8) << bound
                          << std::setw(8) << margin << std::setprecision(4) << std::setw(9)
                          << t.flagged_rate() << std::setw(8) << t.silent_rate() << std::setw(9)
                          << t.correct_rate() << std::setw(9) << t.offered_rate() << "\n";
                break;
            }
        }
    }
}

TEST_CASE("what the reader confuses", "[.scan-confusions]") {
    // Each wrong tile as truth -> read, with how often; how often each true letter is
    // right; and why each frame that was not read was not. The place to look when a
    // number is below its target.
    std::map<std::string, int> confusions;
    std::map<std::string, int> failures;
    std::map<std::string, std::pair<int, int>> per_letter;
    for (const Spec& spec : corpus(report_split())) {
        const Rendered frame = render(spec);
        const auto reading = read_board(frame.image.view());
        std::vector<TileRecord> records;
        const Tally tally = judge(frame, reading, &records);
        if (tally.found == 0) {
            const std::string why = reading ? "wrong size " + std::to_string(reading->rows) +
                                                  " for " + std::to_string(frame.size)
                                            : std::string(to_string(reading.error()));
            ++failures[std::string(name(spec.condition)) + ": " + why];
            continue;
        }
        for (const TileRecord& r : records) {
            ++per_letter[r.truth].first;
            per_letter[r.truth].second += r.correct ? 1 : 0;
            if (!r.correct) {
                ++confusions[std::string(name(spec.condition)) + " " + r.truth + " -> " +
                             (r.letters.empty() ? "(nothing)" : r.letters)];
            }
        }
    }
    std::cout << "per letter (right/seen):";
    for (const auto& [letter, counts] : per_letter) {
        std::cout << " " << letter << " " << counts.second << "/" << counts.first;
    }
    std::cout << '\n';
    for (const auto& [why, count] : failures) {
        std::cout << count << "x not read, " << why << '\n';
    }
    std::vector<std::pair<int, std::string>> ranked;
    for (const auto& [what, count] : confusions) {
        ranked.emplace_back(count, what);
    }
    std::ranges::sort(ranked, std::greater<>());
    for (std::size_t i = 0; i < std::min<std::size_t>(ranked.size(), 40); ++i) {
        std::cout << ranked[i].first << "x " << ranked[i].second << '\n';
    }
}

TEST_CASE("the glyph step alone, given the true corners", "[.scan-oracle]") {
    // Draws the board square from the corners the corpus knows and the dice from
    // where they truly are, so that the finder, the cell count and the centring are
    // out of it, and reads each cell: what the glyph step gets right when the geometry
    // is perfect. The glyph height is still estimated by the reader's own rule, so
    // this is the glyph step as the reader runs it.
    const ReadOptions options;
    for (const Figures& target : kTuning) {
        Tally total;
        for (const Spec& spec : corpus(report_split())) {
            if (spec.condition != target.condition) {
                continue;
            }
            const Rendered frame = render(spec);
            const GrayImage board = warp_to_square(frame.image, frame.corners, 480);
            BoardReading reading;
            reading.rows = reading.columns = frame.size;
            const double cell = 480.0 / static_cast<double>(frame.size);
            const double length =
                static_cast<double>(frame.size) + static_cast<double>(frame.size - 1) * frame.gap;
            for (std::size_t i = 0; i < frame.size * frame.size; ++i) {
                const double fx =
                    (0.5 + static_cast<double>(i % frame.size) * (1.0 + frame.gap)) / length;
                const double fy =
                    (0.5 + static_cast<double>(i / frame.size) * (1.0 + frame.gap)) / length;
                const double half_side = 0.34 * cell * (frame.glyph_height / 0.55);
                const GlyphRead read = GlyphReader::standard().read(
                    board, {fx * 480.0, fy * 480.0}, half_side, options.min_contrast);
                TileReading tile;
                if (read.has_ink) {
                    const auto& font = stroke_font();
                    tile.letters = std::string(font[read.best[0].glyph].letters);
                    tile.score = read.best[0].score;
                    tile.margin = read.best[0].score - read.best[1].score;
                    tile.doubtful =
                        tile.score < options.min_score || tile.margin < options.min_margin;
                    tile.alternatives[0] = {std::string(font[read.best[1].glyph].letters),
                                            read.best[1].score};
                    tile.alternatives[1] = {std::string(font[read.best[2].glyph].letters),
                                            read.best[2].score};
                }
                reading.tiles.push_back(tile);
            }
            total.add(judge_turned(frame.letters, frame.size, 0, reading));
        }
        std::cout << std::setw(8) << name(target.condition) << ": " << describe(total) << '\n';
    }
}

TEST_CASE("frames with no board", "[.scan-negatives]") {
    // The frames with no board, on the tuning seeds (100 to 119 of each kind), or on the
    // test seeds (0 to 6) with WJS_SCAN_SPLIT=test: how many were answered with an error,
    // and any that came back as a board.
    const bool test = report_split() == Split::test;
    std::size_t frames = 0;
    std::size_t errors = 0;
    for (std::size_t k = 0; k < kNegativeKindCount; ++k) {
        for (unsigned i = test ? 0 : 100; i < (test ? 7U : 120U); ++i) {
            const GrayImage image = render_negative(static_cast<NegativeKind>(k), i);
            const auto reading = read_board(image.view());
            ++frames;
            if (!reading) {
                ++errors;
                continue;
            }
            std::cout << "kind " << k << " #" << i << ": a " << reading->rows << "x"
                      << reading->columns << " board, believed " << believed(*reading) << '\n';
        }
    }
    std::cout << errors << " of " << frames
              << " frames with no board were answered with an error\n";
}

namespace {

// What reading a directory of recorded frames came to.
struct RecordedRun {
    Tally total;
    // Frames read and judged, and files passed over (no readable picture, or a truth that
    // is not a square board).
    std::size_t judged = 0;
    std::vector<std::string> skipped;
};

// The recorded-frames harness: every <name>.pgm in the directory (in name order) with its
// <name>.txt beside it, the true board in the board text format ("T A P / S I N / ...")
// as the picture shows it, upright, numbered from the top left: the same grid the reader
// gives. A real frame has no corners to turn the truth by, so the turn is the reader's
// to get right and a board it numbers from another corner scores as wrong tiles. One
// line a frame goes to `out`, and one for the total.
RecordedRun read_recorded(const std::filesystem::path& directory, std::ostream& out) {
    RecordedRun run;
    std::vector<std::filesystem::path> pictures;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".pgm") {
            pictures.push_back(entry.path());
        }
    }
    std::ranges::sort(pictures);
    for (const std::filesystem::path& picture : pictures) {
        const GrayImage image = read_pgm(picture);
        std::ifstream truth_file(std::filesystem::path(picture).replace_extension(".txt"));
        std::stringstream text;
        text << truth_file.rdbuf();
        const auto board = wjs::Board::parse(text.str());
        if (image.empty() || !board || board->rows() != board->columns()) {
            out << "skipped " << picture.filename().string() << " (no image, or no square truth)\n";
            run.skipped.push_back(picture.filename().string());
            continue;
        }
        std::vector<std::string> truth;
        for (std::size_t row = 0; row < board->rows(); ++row) {
            for (std::size_t column = 0; column < board->columns(); ++column) {
                truth.push_back(board->tile({row, column}));
            }
        }
        const Tally one = judge_turned(truth, board->rows(), 0, read_board(image.view()));
        out << picture.filename().string() << ": " << describe(one) << "\n";
        run.total.add(one);
        ++run.judged;
    }
    out << "all: " << describe(run.total) << "\n";
    return run;
}

// A board in the text format, from the letters of a frame.
std::string text_of(const std::vector<std::string>& letters, std::size_t size) {
    std::string text;
    for (std::size_t row = 0; row < size; ++row) {
        text += row > 0 ? " / " : "";
        for (std::size_t column = 0; column < size; ++column) {
            text += column > 0 ? " " : "";
            text += letters[row * size + column];
        }
    }
    return text;
}

}  // namespace

TEST_CASE("recorded frames, kept outside the repository", "[.recorded]") {
    // The real measurement. Frames of a child's room are never committed
    // (docs/licensing.md): this reads the directory named by WJS_SCAN_RECORDED_DIR, with
    // the harness the self-test below holds (read_recorded), and prints the five numbers.
    // It skips when the variable is not set. It has not been run on anything real.
    const char* directory = std::getenv("WJS_SCAN_RECORDED_DIR");  // NOLINT: read once, at start
    if (directory == nullptr) {
        SKIP("WJS_SCAN_RECORDED_DIR is not set; no recorded frames to read");
    }
    (void)read_recorded(directory, std::cout);
}

TEST_CASE("the recorded-frames harness reads a picture with its truth and judges it",
          "[scan][corpus][smoke]") {
    // The harness has never met a real frame, and the first session on a real board should
    // not be spent finding out that it cannot read a header. So it is run here on frames
    // the corpus draws, written the way a camera tool writes them.
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("wjs_scan_recorded_selftest_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);

    Spec spec;
    spec.condition = Condition::upright;
    spec.split = Split::tuning;
    spec.size = 4;
    spec.index = 2;
    const Rendered frame = render(spec);
    REQUIRE(expected_quarters(frame.corners) == 0);  // upright in the picture: the truth as it lies

    // One frame as written by write_pgm, and one the way an editor writes them: comments
    // in the header, the fields on lines of their own.
    REQUIRE(write_pgm(directory / "a_plain.pgm", frame.image));
    {
        std::ofstream file(directory / "b_commented.pgm", std::ios::binary);
        file << "P5\n# written by some tool\n"
             << frame.image.width << " # width\n"
             << frame.image.height << "\n# the maxval\n255\n";
        file.write(reinterpret_cast<const char*>(frame.image.pixels.data()),
                   static_cast<std::streamsize>(frame.image.pixels.size()));
    }
    for (const char* file_name : {"a_plain.txt", "b_commented.txt"}) {
        std::ofstream(directory / file_name) << text_of(frame.letters, frame.size) << "\n";
    }
    // Files that are to be passed over, each for its own reason.
    REQUIRE(write_pgm(directory / "c_no_truth.pgm", frame.image));
    REQUIRE(write_pgm(directory / "d_not_square.pgm", frame.image));
    std::ofstream(directory / "d_not_square.txt") << "A B C / D E F\n";
    {
        std::ofstream cut(directory / "e_cut_short.pgm", std::ios::binary);
        cut << "P5\n960 720\n255\n" << std::string(1000, 'x');
    }
    std::ofstream(directory / "e_cut_short.txt") << text_of(frame.letters, frame.size) << "\n";
    {
        std::ofstream sixteen(directory / "f_sixteen_bit.pgm", std::ios::binary);
        sixteen << "P5\n2 2\n65535\n" << std::string(8, 'x');
    }
    std::ofstream(directory / "f_sixteen_bit.txt") << "A B / C D\n";
    std::ofstream(directory / "notes.md") << "not a picture\n";

    std::ostringstream log;
    const RecordedRun run = read_recorded(directory, log);
    INFO(log.str());
    std::filesystem::remove_all(directory);

    CHECK(run.judged == 2);
    CHECK(run.skipped == std::vector<std::string>{"c_no_truth.pgm", "d_not_square.pgm",
                                                  "e_cut_short.pgm", "f_sixteen_bit.pgm"});
    CHECK(run.total.frames == 2);
    CHECK(run.total.found == 2);
    CHECK(run.total.tiles == 32);
    CHECK(run.total.correct >= 28);  // 14 of 16 on each, as the reader test holds it
    CHECK(run.total.rotation_errors == 0);
    // The two files are the same picture, so they are read the same.
    CHECK(log.str().find("a_plain.pgm: ") != std::string::npos);
    CHECK(log.str().find("all: frames 2, found 1.0000 (2)") != std::string::npos);
}

TEST_CASE("a picture file is read only if it is a whole grey PGM", "[scan][corpus][smoke]") {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("wjs_scan_pgm_selftest_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    GrayImage image(5, 3);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = static_cast<std::uint8_t>(i * 17);
    }
    REQUIRE(write_pgm(directory / "ok.pgm", image));
    const GrayImage back = read_pgm(directory / "ok.pgm");
    CHECK(back.width == 5);
    CHECK(back.height == 3);
    CHECK(back.pixels == image.pixels);

    const auto write = [&](const char* file_name, const std::string& bytes) {
        std::ofstream(directory / file_name, std::ios::binary) << bytes;
        return read_pgm(directory / file_name);
    };
    CHECK(write("p6.pgm", "P6\n1 1\n255\nabc").empty());
    CHECK(write("zero.pgm", "P5\n0 4\n255\n").empty());
    CHECK(write("negative.pgm", "P5\n-2 4\n255\n12345678").empty());
    CHECK(write("huge.pgm", "P5\n70000 70000\n255\n1").empty());
    CHECK(write("words.pgm", "P5\nwide 4\n255\n1234").empty());
    CHECK(write("short.pgm", "P5\n4 4\n255\n123").empty());
    CHECK(write("empty.pgm", "").empty());
    CHECK(write("tiny.pgm", "P5 1 1 255 Z").pixels == std::vector<std::uint8_t>{'Z'});
    CHECK(read_pgm(directory / "missing.pgm").empty());
    std::filesystem::remove_all(directory);
}
