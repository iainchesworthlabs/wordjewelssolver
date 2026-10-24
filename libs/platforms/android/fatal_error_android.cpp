// SPDX-License-Identifier: GPL-3.0-or-later
#include <android/log.h>

#include <iostream>
#include <string>
#include <string_view>

#include "wjs/platform/fatal_error.hpp"

namespace wjs::platform {

// An Android application has no console: the standard error stream goes nowhere a
// person can read, so the message is written to the system log as well, where
// `adb logcat -s WordJewelsSolver` shows it. There is no dialog: one needs the Java
// side (an AlertDialog on the UI thread), and a program that cannot start has no
// event loop to run one on. The message is the only thing a person reports.
void show_fatal_error(std::string_view title, std::string_view message,
                      [[maybe_unused]] bool interactive) {
    std::cerr << title << ": " << message << '\n';
    std::cerr.flush();

    const std::string text = std::string{title} + ": " + std::string{message};
    __android_log_write(ANDROID_LOG_FATAL, "WordJewelsSolver", text.c_str());
}

}  // namespace wjs::platform
