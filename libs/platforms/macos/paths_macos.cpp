// SPDX-License-Identifier: GPL-3.0-or-later
#include <mach-o/dyld.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include "wjs/platform/paths.hpp"

namespace wjs::platform {

std::filesystem::path executable_path() {
    // The first call fails on purpose: it reports the length the name needs.
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);

    std::string buffer(size, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    buffer.resize(std::strlen(buffer.c_str()));

    // The name may go through symlinks and "..": settle it, without throwing.
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(buffer, error);
    return error ? std::filesystem::path{buffer} : resolved;
}

std::filesystem::path user_data_directory() {
    // ~/Library/Application Support is where a program keeps its own data. A
    // device backup includes it, which is the user's own backup and not the
    // program sending anything (docs/design/dashboard.md, "Backups").
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        const std::filesystem::path path{home};
        if (path.is_absolute()) {
            return path / "Library" / "Application Support";
        }
    }
    return {};
}

}  // namespace wjs::platform
