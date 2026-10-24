// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "wjs/calendar.hpp"

// The reference days were counted with an independent implementation (Python's
// datetime) for these dates.

TEST_CASE("a civil date is the number of days since 1970-01-01", "[calendar]") {
    CHECK(wjs::day_from_civil(1970, 1, 1) == 0);
    CHECK(wjs::day_from_civil(1969, 12, 31) == -1);
    CHECK(wjs::day_from_civil(2000, 3, 1) == 11017);
    CHECK(wjs::day_from_civil(2024, 2, 29) == 19782);
    CHECK(wjs::day_from_civil(2026, 10, 16) == 20742);
}

TEST_CASE("consecutive dates are consecutive days, across a month, a year and a leap day",
          "[calendar]") {
    CHECK(wjs::day_from_civil(2026, 11, 1) == wjs::day_from_civil(2026, 10, 31) + 1);
    CHECK(wjs::day_from_civil(2027, 1, 1) == wjs::day_from_civil(2026, 12, 31) + 1);
    CHECK(wjs::day_from_civil(2024, 3, 1) == wjs::day_from_civil(2024, 2, 29) + 1);
    CHECK(wjs::day_from_civil(2023, 3, 1) == wjs::day_from_civil(2023, 2, 28) + 1);
}

TEST_CASE("the weekday is ISO numbered, Monday 1 to Sunday 7", "[calendar]") {
    CHECK(wjs::iso_weekday(0) == 4);  // 1970-01-01 was a Thursday
    CHECK(wjs::iso_weekday(-1) == 3);
    CHECK(wjs::iso_weekday(20738) == 1);  // 2026-10-12
    CHECK(wjs::iso_weekday(20742) == 5);  // 2026-10-16, a Friday
    CHECK(wjs::iso_weekday(20743) == 6);
    CHECK(wjs::iso_weekday(20744) == 7);
    CHECK(wjs::iso_weekday(20745) == 1);
}

TEST_CASE("a week starts on the calendar locale's first day", "[calendar]") {
    const wjs::Day friday = 20742;

    // Monday-first (most of the world): the week of 2026-10-16 starts on the 12th.
    CHECK(wjs::week_start(friday, 1) == 20738);
    // Sunday-first (the United States' calendar): it starts on the 11th.
    CHECK(wjs::week_start(friday, 7) == 20737);
    // Saturday-first: the 10th.
    CHECK(wjs::week_start(friday, 6) == 20736);
    // The first day itself is its own week's start.
    CHECK(wjs::week_start(20738, 1) == 20738);
    CHECK(wjs::week_start(20744, 1) == 20738);  // Sunday ends a Monday-first week
    CHECK(wjs::week_start(20745, 1) == 20745);  // and the next Monday begins another
}

TEST_CASE("a first day of the week outside 1 to 7 is read as Monday", "[calendar]") {
    CHECK(wjs::week_start(20742, 0) == 20738);
    CHECK(wjs::week_start(20742, 8) == 20738);
    CHECK(wjs::week_start(20742, -3) == 20738);
}

TEST_CASE("every week is seven days, whatever the first day, over a long run of days",
          "[calendar]") {
    for (int first = 1; first <= 7; ++first) {
        for (wjs::Day day = -400; day < 400; ++day) {
            const wjs::Day start = wjs::week_start(day, first);
            REQUIRE(start <= day);
            REQUIRE(day - start <= 6);
            REQUIRE(wjs::iso_weekday(start) == first);
        }
    }
}
