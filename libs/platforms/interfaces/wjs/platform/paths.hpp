// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace wjs::platform {

// There is one definition of each function here per platform
// (libs/platforms/<name>/paths_<name>.cpp).

// The absolute path of the running executable, so that data installed beside
// it (dictionaries) can be found whatever the current directory is. Empty if
// the operating system will not say.
[[nodiscard]] std::filesystem::path executable_path();

// The directory under which this user's programs keep data that belongs to
// them and is not shared: the place a program writes what it has learned about
// its user. It is the per-user, per-device one, not a roaming or synchronised
// one, because nothing the program keeps moves between devices (the parent
// area's store, docs/design/dashboard.md). Absolute, and not created: the
// caller makes the directory it writes in. Empty if the operating system will
// not say, in which case the caller keeps nothing on disk.
[[nodiscard]] std::filesystem::path user_data_directory();

}  // namespace wjs::platform
