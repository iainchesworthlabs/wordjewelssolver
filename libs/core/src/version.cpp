// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/version.hpp"

#include <format>
#include <string>

namespace wjs {

std::string version_details() {
    std::string details = std::format(
        "Word Jewels Solver {}\n"
        "  release: {}\n"
        "  commit:  {}\n"
        "  branch:  {}\n"
        "  target:  {}",
        project_version, git_describe, git_commit, git_branch, build_target);
    if (git_dirty) {
        details += "\n  state:   dirty (uncommitted changes)";
    }
    return details;
}

}  // namespace wjs
