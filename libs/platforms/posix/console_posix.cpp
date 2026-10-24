// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/platform/console.hpp"

namespace wjs::platform {

// A POSIX terminal carries bytes, and the programs here write UTF-8: there is
// nothing to switch on.
void use_utf8_console() {}

}  // namespace wjs::platform
