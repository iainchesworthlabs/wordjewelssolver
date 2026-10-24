// SPDX-License-Identifier: GPL-3.0-or-later
#include "evaluate.hpp"

#include <algorithm>
#include <cmath>

#include "wjs/scan/reader.hpp"

namespace wjs::scan::synth {

namespace {

// The truth grid turned `quarters` quarter turns clockwise.
std::vector<std::string> turned(const std::vector<std::string>& grid, std::size_t size,
                                std::size_t quarters) {
    std::vector<std::string> out = grid;
    for (std::size_t q = 0; q < quarters; ++q) {
        std::vector<std::string> next(out.size());
        for (std::size_t i = 0; i < size; ++i) {
            for (std::size_t j = 0; j < size; ++j) {
                next[i * size + j] = out[(size - 1 - j) * size + i];
            }
        }
        out = std::move(next);
    }
    return out;
}

std::size_t matches_of(const std::vector<std::string>& grid, const BoardReading& reading) {
    std::size_t matches = 0;
    for (std::size_t i = 0; i < grid.size() && i < reading.tiles.size(); ++i) {
        if (reading.tiles[i].letters == grid[i]) {
            ++matches;
        }
    }
    return matches;
}

}  // namespace

std::size_t expected_quarters(const Quad& corners) {
    // The corner whose first edge is nearest to horizontal.
    std::size_t first = 0;
    double flattest = 1e300;
    for (std::size_t k = 0; k < 4; ++k) {
        const Point edge = corners[(k + 1) % 4] - corners[k];
        const double slope = std::abs(std::atan2(edge.y, edge.x));
        if (slope < flattest) {
            flattest = slope;
            first = k;
        }
    }
    // With board corner `first` at the top left, the board has been turned
    // clockwise (4 - first) quarters.
    return (4 - first) % 4;
}

Tally judge_turned(const std::vector<std::string>& truth, std::size_t size, std::size_t quarters,
                   const std::expected<BoardReading, ReadError>& reading,
                   std::vector<TileRecord>* records) {
    Tally tally;
    tally.frames = 1;
    if (!reading || reading->rows != size || reading->columns != size ||
        reading->tiles.size() != size * size) {
        return tally;
    }
    tally.found = 1;

    const std::vector<std::string> grid = turned(truth, size, quarters % 4);
    // Would another turn of the truth have matched better? Then the board was
    // numbered from another corner than it should have been.
    const std::size_t own = matches_of(grid, *reading);
    for (std::size_t q = 0; q < 4; ++q) {
        if (q != quarters % 4 && matches_of(turned(truth, size, q), *reading) > own) {
            tally.rotation_errors = 1;
        }
    }

    for (std::size_t i = 0; i < grid.size(); ++i) {
        const TileReading& tile = reading->tiles[i];
        const bool correct = tile.letters == grid[i];
        const bool offered = correct || tile.alternatives[0].letters == grid[i] ||
                             tile.alternatives[1].letters == grid[i];
        ++tally.tiles;
        tally.correct += correct ? 1U : 0U;
        tally.flagged += tile.doubtful ? 1U : 0U;
        tally.silent += (!correct && !tile.doubtful) ? 1U : 0U;
        tally.offered += offered ? 1U : 0U;
        if (records != nullptr) {
            records->push_back({correct, offered, !tile.letters.empty(), tile.score, tile.margin,
                                grid[i], tile.letters});
        }
    }
    return tally;
}

Tally judge(const Rendered& frame, const std::expected<BoardReading, ReadError>& reading,
            std::vector<TileRecord>* records) {
    return judge_turned(frame.letters, frame.size, expected_quarters(frame.corners), reading,
                        records);
}

Tally evaluate(Condition condition, Split split, const ReadOptions& options,
               std::vector<TileRecord>* records) {
    Tally total;
    for (const Spec& spec : corpus(split)) {
        if (spec.condition != condition) {
            continue;
        }
        const Rendered frame = render(spec);
        const auto reading = read_board(frame.image.view(), options);
        total.add(judge(frame, reading, records));
    }
    return total;
}

Tally with_thresholds(const std::vector<TileRecord>& records, double min_score, double min_margin) {
    Tally tally;
    for (const TileRecord& record : records) {
        const bool doubtful =
            !record.has_letters || record.score < min_score || record.margin < min_margin;
        ++tally.tiles;
        tally.correct += record.correct ? 1U : 0U;
        tally.flagged += doubtful ? 1U : 0U;
        tally.silent += (!record.correct && !doubtful) ? 1U : 0U;
        tally.offered += record.offered ? 1U : 0U;
    }
    return tally;
}

}  // namespace wjs::scan::synth
