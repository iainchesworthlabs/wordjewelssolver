// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace wjs::platform {

// Tells the person that the program cannot start, in the way this platform's
// graphical programs do. There is one definition per platform.
//
// A graphical program on Windows has no console window, so a missing data file
// or a window that would not load is otherwise a program that simply never
// appears: there the message is also shown in a dialog. Elsewhere a program is
// started from a terminal or a launcher that keeps the standard error stream, and
// the message goes there.
//
// The message is always written to the standard error stream. `interactive` says
// whether a dialog may also be put in front of the person: it is false in a test,
// in a headless run and on a build machine, where a dialog would wait for a click
// that never comes. `title` and `message` are UTF-8.
void show_fatal_error(std::string_view title, std::string_view message, bool interactive);

}  // namespace wjs::platform
