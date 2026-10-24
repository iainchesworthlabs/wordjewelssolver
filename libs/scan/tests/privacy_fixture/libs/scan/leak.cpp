// SPDX-License-Identifier: GPL-3.0-or-later
// A fixture for the privacy check's own test (libs/scan/tests/check_privacy.cmake): a file
// that does what the check forbids, in a tree of its own that the real check never
// reads. The check run on this tree has to fail.
#include <fstream>

void keep_a_frame(const char* path) {
    std::ofstream out(path);
}
