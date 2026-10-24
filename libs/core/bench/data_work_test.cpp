// SPDX-License-Identifier: GPL-3.0-or-later
// Work budgets for what the screens wait on besides the solver: the store that is
// rewritten after every find, the dashboard a parent opens, the phase directory
// read at start-up, and the paths a finger hands in. Nothing here is timed (the
// time of each is in timing_test.cpp): the counts and sizes are the same on every
// machine, compiler and build type, as the solver's are (work_test.cpp).
//
// Every case is for one board mode and says which. The standard board is the
// default; the phonics board is a parent's switch (docs/design/performance.md,
// "What was measured").
//
// What a failure means, and how to move a budget: docs/testing.md,
// "Performance tests". The numbers are in budgets.hpp.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "budgets.hpp"
#include "check.hpp"
#include "data_corpus.hpp"
#include "metrics.hpp"
#include "wjs/dashboard.hpp"
#include "wjs/store.hpp"

namespace {

using wjs::perf::BoardMode;
using wjs::perf::check_within_budget;
using wjs::perf::Metric;
using wjs::perf::skip_unless_optimised;

[[nodiscard]] std::vector<Metric> only(BoardMode mode, std::vector<Metric> metrics) {
    std::erase_if(metrics, [mode](const Metric& metric) { return metric.mode != mode; });
    return metrics;
}

}  // namespace

// --- the store ----------------------------------------------------------------

TEST_CASE("standard board: the store a play leaves is within its budget",
          "[perf][store][standard]") {
    skip_unless_optimised();
    check_within_budget(only(BoardMode::standard, wjs::perf::store_metrics()));
}

TEST_CASE("phonics board: the store a play leaves is within its budget", "[perf][store][phonics]") {
    skip_unless_optimised();
    check_within_budget(only(BoardMode::phonics, wjs::perf::store_metrics()));
}

TEST_CASE("the store reads back what it wrote, for both modes and at its heaviest",
          "[perf][store][standard][phonics]") {
    skip_unless_optimised();
    // The sizes above are the sizes of a store that works: the text parses and is
    // the data it was made from.
    for (const BoardMode mode : {BoardMode::standard, BoardMode::phonics}) {
        const wjs::perf::KeptStore& kept = wjs::perf::kept_store(mode);
        INFO("mode " << wjs::perf::to_string(mode));
        const auto parsed = wjs::parse_store(kept.text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->data == kept.data);
    }

    const wjs::StoreData heaviest = wjs::perf::heaviest_store();
    const std::string text = wjs::serialize_store(heaviest);
    // It is the heaviest because nothing is cut: it comes back whole, and fits.
    CHECK(text.size() <= wjs::max_store_bytes);
    const auto parsed = wjs::parse_store(text);
    REQUIRE(parsed.has_value());
    CHECK(parsed->data == heaviest);
    CHECK(parsed->data.boards.size() == wjs::max_stored_boards);
    CHECK(parsed->data.finds.size() == wjs::max_stored_finds);
    CHECK(parsed->data.vault.size() == wjs::max_vault_words);
    CHECK(parsed->data.days.size() == wjs::max_stored_days);
}

TEST_CASE("the store's file is its serialization, rewritten whole after every change",
          "[perf][store][standard][phonics]") {
    skip_unless_optimised();
    for (const BoardMode mode : {BoardMode::standard, BoardMode::phonics}) {
        const wjs::perf::KeptStore& kept = wjs::perf::kept_store(mode);
        const wjs::perf::StoreWrites writes = wjs::perf::write_through(kept);
        INFO("mode " << wjs::perf::to_string(mode));
        // The file at the end is exactly the text of the data, and it is the size
        // one save writes, so the rows above are about the file the player has.
        CHECK(writes.final_equals_text);
        CHECK(writes.final_bytes == kept.text.size());
        // Each change writes at most the file it ends with, and the last writes all
        // of it: the total is between one and twenty times a save.
        CHECK(writes.bytes >= writes.final_bytes);
        CHECK(writes.bytes <= writes.final_bytes * kept.snapshots.size());
    }
}

// --- the dashboard ------------------------------------------------------------

TEST_CASE("phonics board: the dashboard is built from the store as it was measured to be",
          "[perf][dashboard][phonics]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::dashboard_metrics());
}

// --- the phase directory ------------------------------------------------------

TEST_CASE("phonics board: loading the phase directory reads what it was measured to",
          "[perf][phase][phonics]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::phase_load_metrics());
}

// --- a path submitted to a session --------------------------------------------

TEST_CASE("a path submitted to a session changes nothing but what it finds",
          "[perf][submit][standard][phonics]") {
    skip_unless_optimised();
    check_within_budget(wjs::perf::submit_metrics());
}

TEST_CASE("the path that is submitted again and again is the miss it was meant to be",
          "[perf][submit][standard][phonics]") {
    skip_unless_optimised();
    // The timing rows submit a path that spells nothing the board holds; a board
    // where it was a find would be timing a different thing. Whatever the kind it
    // comes back as, it is not a find.
    for (const BoardMode mode : {BoardMode::standard, BoardMode::phonics}) {
        const wjs::perf::PlayingSession playing(mode, 4, 0);
        INFO("mode " << wjs::perf::to_string(mode));
        CHECK(playing.miss_kind() != wjs::TraceKind::found);
        CHECK(playing.miss_kind() != wjs::TraceKind::found_again);
        CHECK(playing.session().finds().size() == 1);
    }
}
