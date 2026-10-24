// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/platform/window_chrome.hpp"

namespace wjs::platform {

void set_window_chrome([[maybe_unused]] void* native_handle, [[maybe_unused]] bool dark) {
    // The desktop's window manager draws its own title bar; there is nothing to
    // paint here.
}

}  // namespace wjs::platform
