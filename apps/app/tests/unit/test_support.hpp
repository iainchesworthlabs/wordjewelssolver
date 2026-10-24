// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

// What the app tests share: an application object to make inside a case, and a
// directory of its own that is gone when the case ends. Test support: nothing in
// libs and apps link it.
//
// Which Qt platform a GUI application draws on is not chosen here. The CTest
// entries of the app tests carry QT_QPA_PLATFORM=offscreen (tests/CMakeLists.txt),
// as every other test that starts Qt Quick does, so no case sets the environment
// from inside the process and a case run by hand is told how (docs/testing.md).

namespace wjs::app::test {

// A QCoreApplication for a case that needs an event loop, a timer or a queued
// connection. Made inside the case, so the cases that need none create none.
class CoreApplication {
public:
    CoreApplication() : application_(argc_, argv_) {}

private:
    static inline int argc_ = 1;
    static inline char name_[] = "wjs_app_tests";
    static inline char* argv_[] = {name_, nullptr};

    QCoreApplication application_;
};

// A QGuiApplication, for what asks the platform: the style hints, the font
// database, Qt Multimedia's device list and Qt Sensors.
class GuiApplication {
public:
    GuiApplication() : application_(argc_, argv_) {}

private:
    static inline int argc_ = 1;
    static inline char name_[] = "wjs_app_tests";
    static inline char* argv_[] = {name_, nullptr};

    QGuiApplication application_;
};

// Runs the event loop until `done()` is true or `timeout_ms` has gone by, and
// says which. For a result that arrives from another thread or a queued signal:
// the wait ends when the thing happens, and the timeout is only how long a
// broken one is waited for.
[[nodiscard]] inline bool spin_until(const std::function<bool()>& done, int timeout_ms = 30000) {
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        if (clock.elapsed() > timeout_ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

// Lets the event loop run for `milliseconds` and nothing else: for a case that
// asserts that something does NOT happen, where there is no event to wait for.
inline void pause(int milliseconds) {
    static_cast<void>(spin_until([] { return false; }, milliseconds));
}

// A directory of its own under the system's temporary one, named by the clock and a
// counter so that two runs of a case, here or in another worktree, never share it.
class TempDirectory {
public:
    explicit TempDirectory(const std::string& prefix = "wjs_app_test") {
        static int counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                (prefix + "_" + std::to_string(stamp) + "_" + std::to_string(++counter));
        std::filesystem::create_directories(path_);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace wjs::app::test
