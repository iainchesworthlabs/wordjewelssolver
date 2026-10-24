// SPDX-License-Identifier: GPL-3.0-or-later
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include "profiler.hpp"

// The QML seam to the profiler (apps/app/src/scene/profiler.cpp). It cannot be told
// what a trace holds from here (a build without Tracy has nothing behind it),
// so what is held is the one thing every build shares: that begin() and end()
// pair, whatever the begin() was given. A begin() that opens nothing and an end()
// that closes the zone around it would put a moment's work into some other
// moment's zone, in the trace of the build that has one.

using wjs::scene::Profiler;

TEST_CASE("every begin is matched by an end, named or not", "[scene][profiler]") {
    Profiler profiler;
    REQUIRE(profiler.open_count() == 0);

    profiler.begin(QStringLiteral("scene.play"));
    CHECK(profiler.open_count() == 1);
    profiler.end();
    CHECK(profiler.open_count() == 0);

    // An empty name opens no zone and still takes its place.
    profiler.begin(QString());
    CHECK(profiler.open_count() == 1);
    profiler.end();
    CHECK(profiler.open_count() == 0);
}

TEST_CASE("an unnamed begin inside a zone does not let its end close the zone",
          "[scene][profiler]") {
    Profiler profiler;
    profiler.begin(QStringLiteral("scene.play"));
    profiler.begin(QString());
    profiler.end();                     // the unnamed one's
    CHECK(profiler.open_count() == 1);  // scene.play is still open
    profiler.end();
    CHECK(profiler.open_count() == 0);
}

TEST_CASE("nesting past the seam's depth is balanced too", "[scene][profiler]") {
    Profiler profiler;
    constexpr std::size_t calls = 20;  // the stack holds fewer
    for (std::size_t i = 0; i < calls; ++i) {
        profiler.begin(QStringLiteral("scene.step"));
        CHECK(profiler.open_count() == i + 1);
    }
    // Each end() takes one begin() away, the ones past the depth first, so the
    // zones inside are not closed by them.
    for (std::size_t i = calls; i > 0; --i) {
        profiler.end();
        CHECK(profiler.open_count() == i - 1);
    }
    // And one end() too many does nothing.
    profiler.end();
    CHECK(profiler.open_count() == 0);
}

TEST_CASE("a value and a mark with nothing open, or an unnamed zone open, are harmless",
          "[scene][profiler]") {
    Profiler profiler;
    profiler.value(5);
    profiler.mark(QStringLiteral("scene.show"));
    profiler.mark(QString());
    CHECK(profiler.open_count() == 0);

    profiler.begin(QString());
    profiler.value(7);  // there is no zone to give it to
    profiler.end();
    CHECK(profiler.open_count() == 0);
}
