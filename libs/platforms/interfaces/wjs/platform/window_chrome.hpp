// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace wjs::platform {

// Paint the native title bar to match the application's paper: the dark scheme
// gets the dark page colour and cream text; the light scheme the warm paper.
// `native_handle` is the window's platform handle (on Windows, its HWND). The
// call is a no-op where the platform does not offer a native title bar.
void set_window_chrome(void* native_handle, bool dark);

}  // namespace wjs::platform
