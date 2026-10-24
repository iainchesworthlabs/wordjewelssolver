// SPDX-License-Identifier: GPL-3.0-or-later
#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "wjs/platform/paths.hpp"

namespace wjs::platform {

std::filesystem::path executable_path() {
    // readlink does not say when it truncated, so grow until the name fits.
    std::string buffer(256, '\0');
    for (;;) {
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            return {};
        }
        if (static_cast<std::size_t>(length) < buffer.size()) {
            buffer.resize(static_cast<std::size_t>(length));
            return std::filesystem::path{buffer};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path user_data_directory() {
    // The XDG base directory specification: $XDG_DATA_HOME if it is set to an
    // absolute path, otherwise ~/.local/share. A relative value is to be
    // ignored, by the same specification.
    if (const char* data_home = std::getenv("XDG_DATA_HOME"); data_home != nullptr) {
        const std::filesystem::path path{data_home};
        if (path.is_absolute()) {
            return path;
        }
    }
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        const std::filesystem::path path{home};
        if (path.is_absolute()) {
            return path / ".local" / "share";
        }
    }
    return {};
}

}  // namespace wjs::platform
