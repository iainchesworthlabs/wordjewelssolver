// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace wjs::platform {

// Makes the process's console read and write UTF-8, so that text a program
// prints is what it meant to print. Where the console already is UTF-8 this
// does nothing.
void use_utf8_console();

}  // namespace wjs::platform
