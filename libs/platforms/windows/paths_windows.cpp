// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>

#include <filesystem>
#include <string>

#include "wjs/platform/paths.hpp"

namespace wjs::platform {

std::filesystem::path executable_path() {
    // MAX_PATH is not a limit on a long path, so grow until the name fits.
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path{buffer};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path user_data_directory() {
    // %LOCALAPPDATA% is the per-user, per-machine one (the roaming profile is
    // %APPDATA%, which follows the user to another machine). The variable is
    // set for every interactive logon.
    constexpr const wchar_t* name = L"LOCALAPPDATA";
    const DWORD needed = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring buffer(needed, L'\0');
    const DWORD length = ::GetEnvironmentVariableW(name, buffer.data(), needed);
    if (length == 0 || length >= needed) {
        return {};
    }
    buffer.resize(length);
    std::filesystem::path path{buffer};
    return path.is_absolute() ? path : std::filesystem::path{};
}

}  // namespace wjs::platform
