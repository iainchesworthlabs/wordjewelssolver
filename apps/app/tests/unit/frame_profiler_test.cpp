// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

#include "frame_profiler.hpp"

// The frame clock (apps/app/src/frame_profiler.cpp) on a window that is never shown:
// the signals it listens to are public, so a test emits them as the scene graph
// does, which makes the frame, the thread it is on and the moment it ends the
// test's. What this holds: what each signal adds to the report, and that the render
// thread may be in the middle of a slot when the profiler is destroyed.

namespace {

using wjs::app::FrameProfiler;

// The platform is read when the application is made, so it is chosen before it.
struct OffscreenPlatform {
    OffscreenPlatform() { qputenv("QT_QPA_PLATFORM", "offscreen"); }
};

// A GUI application on Qt's offscreen platform, which needs no display.
class OffscreenApplication : private OffscreenPlatform {
public:
    OffscreenApplication() : application_(argc_, argv_) {}

private:
    static inline int argc_ = 1;
    static inline char name_[] = "wjs_frame_profiler_tests";
    static inline char* argv_[] = {name_, nullptr};

    QGuiApplication application_;
};

}  // namespace

TEST_CASE("each of the window's signals adds to its own part of the report", "[app][frame]") {
    OffscreenApplication app;
    QQuickWindow window;
    FrameProfiler profiler(&window);

    // The first swap starts the clock; with no warm-up, what comes after is measured.
    window.frameSwapped();

    // The render thread's frame, its synchronisation and its recording of the pass.
    window.beforeFrameBegin();
    window.beforeSynchronizing();
    window.afterSynchronizing();
    window.beforeRenderPassRecording();
    window.afterRenderPassRecording();
    window.afterFrameEnd();
    window.frameSwapped();

    // The GUI thread's: the end of a frame's animations and polish.
    window.afterAnimating();

    const FrameProfiler::Report report = profiler.report();
    CHECK(report.render_frames == 1);
    CHECK(report.sync_frames == 1);
    CHECK(report.record_frames == 1);
    CHECK(report.animate_frames == 1);
    // Two swaps are one interval: the first has nothing before it.
    CHECK(report.frames == 1);
    CHECK(profiler.first_frame_seen());
    CHECK(report.refresh_hz == window.screen()->refreshRate());
    CHECK(report.refresh_hz > 0.0);
    CHECK(report.screen == window.screen()->name());

    const QString text = profiler.report_text();
    CHECK(text.contains(QStringLiteral("refresh_hz=")));
    CHECK(text.contains(QStringLiteral("screen=")));
    CHECK(text.contains(QStringLiteral("render.record_ms:")));
    CHECK(text.contains(QStringLiteral("ui.animate_ms:")));
}

TEST_CASE("the render and GUI threads' times leave out the warm-up, as the intervals do",
          "[app][frame]") {
    OffscreenApplication app;
    QQuickWindow window;
    FrameProfiler profiler(&window);
    profiler.set_target(100, 2);

    const auto frame = [&window] {
        window.beforeFrameBegin();
        window.afterFrameEnd();
        window.afterAnimating();
        window.frameSwapped();
    };
    // The swap that starts the clock, and the two warm-up frames: a frame before
    // each swap is the one that swap ends.
    window.frameSwapped();
    frame();
    frame();
    CHECK(profiler.report().render_frames == 0);
    CHECK(profiler.report().animate_frames == 0);
    // The warm-up is over once its second frame has been swapped.
    frame();
    frame();
    CHECK(profiler.report().render_frames == 2);
    CHECK(profiler.report().animate_frames == 2);
    CHECK(profiler.report().frames == 2);
}

TEST_CASE("the GUI thread's frame is the time it was busy, without the wait for the render thread",
          "[app][frame]") {
    OffscreenApplication app;
    QQuickWindow window;
    FrameProfiler profiler(&window);
    QAbstractEventDispatcher* loop = QAbstractEventDispatcher::instance();
    REQUIRE(loop != nullptr);
    window.frameSwapped();  // the clock starts; with no warm-up, what follows is measured
    using namespace std::chrono_literals;
    // What the sleeps actually took: a loaded machine oversleeps, and the bounds
    // below follow what happened and not what was asked for.
    using Clock = std::chrono::steady_clock;
    const auto sleep_for_ms = [](std::chrono::milliseconds asked) {
        const auto start = Clock::now();
        std::this_thread::sleep_for(asked);
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };

    // A wakeup: busy for a while, then the animations and polish are done.
    loop->awake();
    const double busy_first = sleep_for_ms(15ms);
    window.afterAnimating();
    // The render thread synchronises while the GUI thread waits for it, and for
    // longer than it was busy; only what comes after the wait is the next frame's.
    const double waited = sleep_for_ms(60ms);
    window.beforeSynchronizing();
    window.afterSynchronizing();
    const double busy_second = sleep_for_ms(8ms);
    loop->aboutToBlock();
    loop->awake();
    window.afterAnimating();

    const FrameProfiler::Report report = profiler.report();
    REQUIRE(report.animate_frames == 2);
    const double counted = report.animate_mean_ms * 2.0;
    INFO("busy " << busy_first << " + " << busy_second << " ms, waited " << waited
                 << " ms, counted " << counted << " ms");
    // All the time it was busy is in the two frames...
    CHECK(counted >= busy_first + busy_second);
    CHECK(report.animate_max_ms >= busy_first);
    // ...and the wait is not: at most half of it is, for a machine's own noise.
    CHECK(counted <= busy_first + busy_second + 0.5 * waited);
}

TEST_CASE("the target is reached once and told on the GUI thread", "[app][frame]") {
    OffscreenApplication app;
    QQuickWindow window;
    FrameProfiler profiler(&window);
    profiler.set_target(2, 1);
    int finished = 0;
    QObject::connect(&profiler, &FrameProfiler::finished, [&finished] { ++finished; });

    // The first swap is the clock starting, the next is the warm-up, then two to
    // measure.
    for (int swap = 0; swap < 6; ++swap) {
        window.frameSwapped();
    }
    CHECK(profiler.recorded_frames() == 2);
    QCoreApplication::processEvents();
    CHECK(finished == 1);
}

TEST_CASE("the profiler can go while the render thread is in the middle of a frame",
          "[app][frame][thread]") {
    OffscreenApplication app;
    QQuickWindow window;

    // The render thread, played by another: it sends the frame's signals as fast as
    // it can, and the profiler is made and destroyed under it. A slot that touched a
    // destroyed profiler is what the sanitizer builds report; the others see a
    // crash or a hang.
    std::atomic<bool> stop{false};
    std::atomic<long> frames{0};
    std::thread render([&] {
        while (!stop.load()) {
            window.beforeFrameBegin();
            window.beforeSynchronizing();
            window.afterSynchronizing();
            window.beforeRenderPassRecording();
            window.afterRenderPassRecording();
            window.afterFrameEnd();
            window.frameSwapped();
            frames.fetch_add(1);
        }
    });

    for (int round = 0; round < 200; ++round) {
        const long before = frames.load();
        {
            FrameProfiler profiler(&window);
            profiler.set_target(5, 0);
            // Let at least a few frames through the slots.
            while (frames.load() < before + 3) {
                std::this_thread::yield();
            }
            // Destroyed here, with the thread still sending.
        }
        QCoreApplication::processEvents();
    }
    stop.store(true);
    render.join();
    CHECK(frames.load() > 600);
}
