// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/platform/window_chrome.hpp"

#include <windows.h>

#include <dwmapi.h>

namespace {

// Windows 11's per-window caption attributes. The values are from dwmapi.h in
// recent SDKs; define them here so an older SDK still compiles.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kBorderColor = 34;
constexpr DWORD kCaptionColor = 35;
constexpr DWORD kTextColor = 36;

void set_colour(HWND window, DWORD attribute, COLORREF colour) {
    DwmSetWindowAttribute(window, attribute, &colour, sizeof(colour));
}

}  // namespace

namespace wjs::platform {

void set_window_chrome(void* native_handle, bool dark) {
    const HWND native = static_cast<HWND>(native_handle);
    if (native == nullptr) {
        return;
    }

    const BOOL immersive = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(native, kUseImmersiveDarkMode, &immersive, sizeof(immersive));

    // The content colours, not an arbitrary accent: the title bar is part of the
    // same paper as the page.
    if (dark) {
        set_colour(native, kCaptionColor, RGB(24, 31, 34));  // #181F22, Theme.surface
        set_colour(native, kTextColor, RGB(242, 236, 222));  // #F2ECDE, Theme.ink
        set_colour(native, kBorderColor, RGB(30, 42, 47));   // #1E2A2F, Theme.card
    } else {
        set_colour(native, kCaptionColor, RGB(246, 241, 231));  // #F6F1E7, Theme.page
        set_colour(native, kTextColor, RGB(30, 42, 48));        // #1E2A30, Theme.ink
        set_colour(native, kBorderColor, RGB(230, 223, 208));   // #E6DFD0, Theme.line
    }
}

}  // namespace wjs::platform
