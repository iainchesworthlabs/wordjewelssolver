// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Days, and the week the dashboard reports on. The library keeps no clock and
// asks the operating system nothing: a caller that knows the date (the
// application, from the device's calendar) turns it into a Day, and the store
// and the dashboard work from that. A day is all the store ever records of
// when something happened: no hour, no minute, so nothing in it could be
// turned into a figure for time on screen (docs/design/game.md).

namespace wjs {

// A calendar day, counted from 1970-01-01 (day 0) in the device's own calendar
// and time zone. The same day number is the same date wherever it is read.
using Day = std::int32_t;

// The day of a civil date in the proleptic Gregorian calendar. month is 1 to
// 12 and day 1 to 31; nothing is checked, so a date that does not exist is
// counted on as if it did (the 31st of April is the 1st of May).
[[nodiscard]] constexpr Day day_from_civil(int year, int month, int day) noexcept {
    // Howard Hinnant's days_from_civil: years run from March, so the leap day
    // is the last of its year.
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int year_of_era = year - era * 400;
    const int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

// The weekday of a day, ISO numbered: 1 is Monday and 7 is Sunday (the same
// numbering as Qt::DayOfWeek, so a caller can pass QLocale's first day of the
// week straight in).
[[nodiscard]] constexpr int iso_weekday(Day day) noexcept {
    // Day 0, 1970-01-01, was a Thursday.
    const int since_epoch = ((day % 7) + 7) % 7;
    return (since_epoch + 3) % 7 + 1;
}

// The first day of the week that holds `today`, for a calendar whose week
// starts on `first_day_of_week` (ISO numbered, 1 to 7). A value outside that
// range is read as Monday. The week is that day through the sixth after it.
[[nodiscard]] constexpr Day week_start(Day today, int first_day_of_week) noexcept {
    const int first = first_day_of_week >= 1 && first_day_of_week <= 7 ? first_day_of_week : 1;
    const int back = (iso_weekday(today) - first + 7) % 7;
    return today - back;
}

}  // namespace wjs
