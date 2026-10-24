// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <functional>
#include <string_view>

namespace wjs::app {

// What the program leaves with. The codes are the program's contract with a
// script and with the system tests (tests/system/check_app.cmake); each is a
// different reason, so that a caller can tell them apart without reading text.
enum class ExitStatus : int {
    ok = 0,
    // The game could not be set up: the word list or the dice are missing or
    // unreadable, or the phase directory is not a phase directory. 1 is left to
    // the toolkit and to a crash.
    setup_failed = 2,
    // A smoke run presented no frame: the machine has no display to render on.
    no_frame = 3,
    // The window did not load: the QML failed to load, or its root is not a
    // window.
    window_failed = 4,
};

[[nodiscard]] constexpr int exit_code(ExitStatus status) noexcept {
    return static_cast<int>(status);
}

// How a fatal message reaches the person: the platform's way
// (wjs::platform::show_fatal_error), or a test's.
using FatalErrorSink =
    std::function<void(std::string_view title, std::string_view message, bool interactive)>;

// Whether a dialog may be put in front of the person. Not when a test or a
// headless run is in charge: `WJS_NO_DIALOGS` is set (to anything), or Qt is told
// to draw on a platform that has no screen (`QT_QPA_PLATFORM` is "offscreen" or
// "minimal"). A pure function of the two facts so that it is testable.
[[nodiscard]] bool dialogs_wanted(std::string_view qpa_platform, bool no_dialogs_set) noexcept;
// The same, read from this process's environment.
[[nodiscard]] bool dialogs_wanted_here();

// Says why the program cannot start, where the person can see it (a dialog on
// Windows, where the program has no console, and the error stream everywhere),
// and returns the code to leave with. With no `sink` it is the platform's.
[[nodiscard]] int fail_startup(ExitStatus status, const QString& message,
                               const FatalErrorSink& sink = {});

}  // namespace wjs::app
