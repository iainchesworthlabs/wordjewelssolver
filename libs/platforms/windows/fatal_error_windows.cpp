// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

#include "wjs/platform/fatal_error.hpp"

namespace {

// UTF-8 text as the wide string the dialog takes. Text that is not valid UTF-8
// becomes the replacement character rather than failing: this runs when the
// program is already failing.
[[nodiscard]] std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                          size);
    return wide;
}

}  // namespace

namespace wjs::platform {

void show_fatal_error(std::string_view title, std::string_view message, bool interactive) {
    std::cerr << title << ": " << message << '\n';
    std::cerr.flush();
    if (!interactive) {
        return;
    }
    // The program is a GUI-subsystem one: this dialog is the only way the person
    // learns why no window appeared. It is modal to nothing (no owner), and the
    // task-modal flag keeps it in front.
    const std::wstring text = widen(message);
    const std::wstring caption = widen(title);
    ::MessageBoxW(nullptr, text.c_str(), caption.c_str(),
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
}

}  // namespace wjs::platform
