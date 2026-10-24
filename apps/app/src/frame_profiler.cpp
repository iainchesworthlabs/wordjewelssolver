// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_profiler.hpp"

#include <QAbstractEventDispatcher>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QScreen>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include "wjs/profiling.hpp"

namespace wjs::app {
namespace {

QElapsedTimer& process_clock() {
    static QElapsedTimer clock;
    return clock;
}

// The value a fraction of the way through a sorted list, interpolated between
// its neighbours as FrameStats does for the prototype's tables.
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const auto below = static_cast<std::size_t>(std::floor(position));
    const auto above = static_cast<std::size_t>(std::ceil(position));
    const double weight = position - static_cast<double>(below);
    return values[below] * (1.0 - weight) + values[above] * weight;
}

double mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double largest(const std::vector<double>& values) {
    return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
}

int count_above(const std::vector<double>& values, double limit) {
    return static_cast<int>(std::count_if(values.begin(), values.end(),
                                          [limit](double value) { return value > limit; }));
}

QString two(double value) {
    return QString::number(value, 'f', 2);
}

// A rolling window: the last kWindow values, the oldest overwritten. A frame's
// value is one double, so a program left running for an hour keeps the last
// minute or so of frames rather than growing.
constexpr int kWindow = 4096;

struct Window {
    std::array<double, kWindow> values{};
    int count = 0;  // how many of the slots hold a value
    int next = 0;   // where the next value goes
    double first_at = 0.0;
    double last_at = 0.0;

    void push(double value, double at) noexcept {
        values[static_cast<std::size_t>(next)] = value;
        next = (next + 1) % kWindow;
        if (count < kWindow) {
            ++count;
        }
        if (count == 1) {
            first_at = at;
        }
        last_at = at;
    }

    void clear() noexcept {
        count = 0;
        next = 0;
        first_at = 0.0;
        last_at = 0.0;
    }

    [[nodiscard]] std::vector<double> in_order() const {
        std::vector<double> out;
        out.reserve(static_cast<std::size_t>(count));
        // The oldest value is the next to be overwritten once the window is full.
        const int start = count < kWindow ? 0 : next;
        for (int i = 0; i < count; ++i) {
            out.push_back(values[static_cast<std::size_t>((start + i) % kWindow)]);
        }
        return out;
    }
};

// The count, mean, 95th percentile and largest of a window's values.
struct Summary {
    int frames = 0;
    double mean_ms = 0.0;
    double p95_ms = 0.0;
    double max_ms = 0.0;
};

Summary summarise(const std::vector<double>& values) {
    return {static_cast<int>(values.size()), mean(values), percentile(values, 0.95),
            largest(values)};
}

}  // namespace

// What the window's signals touch. The connections hold it by shared pointer,
// so it outlives the FrameProfiler for as long as the render thread is inside
// one of them, and it reaches the FrameProfiler (to post `finished`) only
// through `owner_`, which the destructor clears under the mutex.
struct FrameProfiler::State {
    explicit State(FrameProfiler* profiler)
        : owner_(profiler), gui_thread_(QThread::currentThread()) {}

    void set_target(int frames, int warmup) {
        const QMutexLocker locker(&mutex_);
        target_ = std::max(0, frames);
        warmup_ = std::max(0, warmup);
        warmup_remaining_ = warmup_;
        measuring_ = have_previous_ && warmup_remaining_ == 0;
        measured_ = 0;
        finished_posted_ = false;
        intervals_.clear();
        render_.clear();
        sync_.clear();
        record_.clear();
        animate_.clear();
    }

    // After this the slots still run, and still record, but post nothing.
    void detach() {
        const QMutexLocker locker(&mutex_);
        owner_ = nullptr;
    }

    int recorded_frames() const {
        const QMutexLocker locker(&mutex_);
        return intervals_.count;
    }

    bool first_frame_seen() const {
        const QMutexLocker locker(&mutex_);
        return have_previous_;
    }

    void on_scene_graph_initialized() {
        // Once per thread: every call adds a table entry. The scene graph can be
        // initialised again if the window is moved to another renderer; the name
        // is already right. Under the basic render loop the scene graph is the
        // GUI thread's, which is "main" and must not be renamed.
        if (QThread::currentThread() == gui_thread_) {
            return;
        }
        if (!render_thread_named_.exchange(true)) {
            wjs::profiling::thread_name("render");
        }
    }

    void on_frame_begin() { frame_begin_ = now_ms(); }

    void on_frame_end() {
        const double at = now_ms();
        const double elapsed = at - frame_begin_;
        {
            const QMutexLocker locker(&mutex_);
            if (measuring_) {
                render_.push(elapsed, at);
            }
        }
        wjs::profiling::plot("render.frame_ms", elapsed);
    }

    void on_sync_begin() { sync_begin_ = now_ms(); }

    void on_sync_end() {
        const double at = now_ms();
        const double elapsed = at - sync_begin_;
        sync_end_.store(at);
        {
            const QMutexLocker locker(&mutex_);
            if (measuring_) {
                sync_.push(elapsed, at);
            }
        }
        wjs::profiling::plot("ui.sync_ms", elapsed);
    }

    void on_record_begin() { record_begin_ = now_ms(); }

    void on_record_end() {
        const double at = now_ms();
        const double elapsed = at - record_begin_;
        {
            const QMutexLocker locker(&mutex_);
            if (measuring_) {
                record_.push(elapsed, at);
            }
        }
        wjs::profiling::plot("render.record_ms", elapsed);
    }

    // GUI thread: the event loop woke up, and is about to sleep. What it did in
    // between is busy time, except the stretch where it waited for the render
    // thread inside the frame's synchronisation, which on_sync_end() marks the end
    // of: the GUI thread is released then, and no earlier.
    void on_awake() {
        awake_at_ = now_ms();
        awake_ = true;
    }

    void on_about_to_block() {
        if (awake_) {
            busy_ += busy_since(awake_at_);
            awake_ = false;
        }
    }

    // GUI thread: animations and polish are done. The frame's GUI-thread work is
    // what the thread was busy with since the last frame's synchronisation let it
    // go: the events, the animations' tick and the bindings it set off, and the
    // polish.
    void on_animated() {
        const double at = now_ms();
        const double elapsed = busy_ + (awake_ ? busy_since(awake_at_) : 0.0);
        busy_ = 0.0;
        // The rest of this wakeup (the wait for the render thread, then whatever
        // follows the synchronisation) is the next frame's.
        awake_at_ = at;
        {
            const QMutexLocker locker(&mutex_);
            if (measuring_) {
                animate_.push(elapsed, at);
            }
        }
        wjs::profiling::plot("ui.animate_ms", elapsed);
    }

    void on_swapped() {
        const double at = now_ms();
        bool first = false;
        bool reached = false;
        double interval = 0.0;
        {
            const QMutexLocker locker(&mutex_);
            if (!have_previous_) {
                have_previous_ = true;
                first = true;
                first_frame_ms_ = at;
            } else {
                interval = at - previous_;
                if (warmup_remaining_ > 0) {
                    --warmup_remaining_;
                } else if (target_ == 0 || measured_ < target_) {
                    intervals_.push(interval, at);
                    ++measured_;
                    if (target_ > 0 && measured_ >= target_ && !finished_posted_) {
                        finished_posted_ = true;
                        reached = true;
                    }
                }
            }
            previous_ = at;
            // The render and GUI thread windows follow the intervals: they leave
            // out the same warm-up, whose first frames create the scene and
            // compile its shaders (render.frame_ms of the first frame was 150 ms
            // and is not a frame the player sees again).
            measuring_ = warmup_remaining_ == 0;
            if (reached && owner_ != nullptr) {
                // Posted under the lock: the destructor takes it to clear owner_
                // before the object goes, so the object is alive for the post.
                QMetaObject::invokeMethod(owner_, &FrameProfiler::finish_guarded,
                                          Qt::QueuedConnection);
            }
        }

        wjs::profiling::frame_mark("UI");
        if (first) {
            // Launch to first frame: the one number a frame interval cannot hold,
            // because it is the wait before the first of them.
            wjs::profiling::plot("ui.first_frame_ms", at);
        } else {
            wjs::profiling::plot("ui.frame_ms", interval);
        }
    }

    Report report() const {
        std::vector<double> intervals;
        std::vector<double> render;
        std::vector<double> sync;
        std::vector<double> record;
        std::vector<double> animate;
        Report out;
        {
            const QMutexLocker locker(&mutex_);
            intervals = intervals_.in_order();
            render = render_.in_order();
            sync = sync_.in_order();
            record = record_.in_order();
            animate = animate_.in_order();
            out.warmup = warmup_;
            out.span_ms = intervals_.count > 0 ? intervals_.last_at - intervals_.first_at : 0.0;
            out.first_frame_ms = first_frame_ms_;
        }
        out.frames = static_cast<int>(intervals.size());
        out.mean_ms = mean(intervals);
        out.p50_ms = percentile(intervals, 0.50);
        out.p95_ms = percentile(intervals, 0.95);
        out.p99_ms = percentile(intervals, 0.99);
        out.max_ms = largest(intervals);
        out.over_16_7 = count_above(intervals, 16.7);
        out.over_33 = count_above(intervals, 33.0);

        const Summary render_summary = summarise(render);
        out.render_frames = render_summary.frames;
        out.render_mean_ms = render_summary.mean_ms;
        out.render_p95_ms = render_summary.p95_ms;
        out.render_max_ms = render_summary.max_ms;

        const Summary sync_summary = summarise(sync);
        out.sync_frames = sync_summary.frames;
        out.sync_mean_ms = sync_summary.mean_ms;
        out.sync_p95_ms = sync_summary.p95_ms;
        out.sync_max_ms = sync_summary.max_ms;

        const Summary record_summary = summarise(record);
        out.record_frames = record_summary.frames;
        out.record_mean_ms = record_summary.mean_ms;
        out.record_p95_ms = record_summary.p95_ms;
        out.record_max_ms = record_summary.max_ms;

        const Summary animate_summary = summarise(animate);
        out.animate_frames = animate_summary.frames;
        out.animate_mean_ms = animate_summary.mean_ms;
        out.animate_p95_ms = animate_summary.p95_ms;
        out.animate_max_ms = animate_summary.max_ms;
        return out;
    }

private:
    mutable QMutex mutex_;
    FrameProfiler* owner_ = nullptr;  // guarded by mutex_
    QThread* gui_thread_ = nullptr;
    Window intervals_;
    Window render_;
    Window sync_;
    Window record_;
    Window animate_;
    bool have_previous_ = false;
    double previous_ = 0.0;
    double first_frame_ms_ = 0.0;
    int measured_ = 0;
    int warmup_remaining_ = 0;
    bool measuring_ = false;  // guarded by mutex_: the warm-up is over
    int target_ = 0;
    int warmup_ = 0;
    bool finished_posted_ = false;

    std::atomic<bool> render_thread_named_{false};

    // Written and read by the render thread only.
    double frame_begin_ = 0.0;
    double sync_begin_ = 0.0;
    double record_begin_ = 0.0;
    // The end of the latest synchronisation: written by the render thread (the
    // GUI thread's, under the basic loop), read by the GUI thread.
    std::atomic<double> sync_end_{0.0};
    // Written and read by the GUI thread only.
    double awake_at_ = 0.0;
    bool awake_ = false;
    double busy_ = 0.0;

    // How long the GUI thread has been busy since `from`, leaving out any wait
    // inside a synchronisation that ended after it.
    [[nodiscard]] double busy_since(double from) const {
        const double start = std::max(from, sync_end_.load());
        return std::max(0.0, now_ms() - start);
    }
};

FrameProfiler::FrameProfiler(QQuickWindow* window, QObject* parent)
    : QObject(parent), state_(std::make_shared<State>(this)) {
    if (!process_clock().isValid()) {
        process_clock().start();
    }
    set_window(window);
}

FrameProfiler::~FrameProfiler() {
    disconnect_window();
    // The render thread may be inside a slot right now: it finishes it on the
    // state it holds, and posts nothing to an object that is going.
    state_->detach();
}

void FrameProfiler::start_process_clock() {
    process_clock().start();
}

double FrameProfiler::now_ms() {
    return static_cast<double>(process_clock().nsecsElapsed()) / 1.0e6;
}

void FrameProfiler::set_window(QQuickWindow* window) {
    if (window == window_) {
        return;
    }
    disconnect_window();
    window_ = window;
    connect_window();
}

void FrameProfiler::disconnect_window() {
    for (const QMetaObject::Connection& connection : connections_) {
        QObject::disconnect(connection);
    }
    connections_.clear();
}

void FrameProfiler::connect_window() {
    if (window_ == nullptr) {
        return;
    }
    // Every one of these but the last two is emitted by the render thread. The
    // slots stamp a time and send a zone, plot or mark; they do not touch the
    // window. Each connection carries the state by value: a slot that is running
    // when this object is destroyed finishes on a state that is still there.
    const auto link_to = [this](const auto* sender, auto signal, auto slot) {
        connections_.push_back(
            QObject::connect(sender, signal, [state = state_, slot] { ((*state).*slot)(); }));
    };
    const auto link = [&link_to, this](auto signal, auto slot) { link_to(window_, signal, slot); };
    link(&QQuickWindow::frameSwapped, &State::on_swapped);
    link(&QQuickWindow::beforeFrameBegin, &State::on_frame_begin);
    link(&QQuickWindow::afterFrameEnd, &State::on_frame_end);
    link(&QQuickWindow::beforeSynchronizing, &State::on_sync_begin);
    link(&QQuickWindow::afterSynchronizing, &State::on_sync_end);
    link(&QQuickWindow::beforeRenderPassRecording, &State::on_record_begin);
    link(&QQuickWindow::afterRenderPassRecording, &State::on_record_end);
    link(&QQuickWindow::sceneGraphInitialized, &State::on_scene_graph_initialized);
    // The GUI thread's: its animations and polish end at afterAnimating, and
    // its busy time is the event loop's, between waking and sleeping.
    link(&QQuickWindow::afterAnimating, &State::on_animated);
    if (const QAbstractEventDispatcher* dispatcher = QAbstractEventDispatcher::instance()) {
        link_to(dispatcher, &QAbstractEventDispatcher::awake, &State::on_awake);
        link_to(dispatcher, &QAbstractEventDispatcher::aboutToBlock, &State::on_about_to_block);
    }
}

void FrameProfiler::set_target(int frames, int warmup) {
    state_->set_target(frames, warmup);
}

int FrameProfiler::recorded_frames() const {
    return state_->recorded_frames();
}

bool FrameProfiler::first_frame_seen() const {
    return state_->first_frame_seen();
}

void FrameProfiler::finish_guarded() {
    emit finished();
}

FrameProfiler::Report FrameProfiler::report() const {
    Report out = state_->report();
    if (window_ != nullptr && window_->screen() != nullptr) {
        out.refresh_hz = window_->screen()->refreshRate();
        out.screen = window_->screen()->name();
    }
    return out;
}

QString FrameProfiler::report_text() const {
    const Report r = report();
    const double over_share =
        r.frames > 0 ? 100.0 * static_cast<double>(r.over_16_7) / static_cast<double>(r.frames)
                     : 0.0;

    QString text;
    // The one line a machine reads (apps/app/tests/unit/frame_timing_test.cpp and the
    // capture recipes): key=value, no spaces in a value. New keys go at the
    // end; a reader finds a key by name.
    text += QStringLiteral(
                "smoke: frames=%1 warmup=%2 span_ms=%3 p50_ms=%4 p95_ms=%5 p99_ms=%6 "
                "max_ms=%7 over16_7=%8 over33=%9 first_frame_ms=%10 render_mean_ms=%11 "
                "render_max_ms=%12 sync_mean_ms=%13 sync_max_ms=%14 refresh_hz=%15 "
                "record_mean_ms=%16 record_max_ms=%17 animate_mean_ms=%18 animate_max_ms=%19 "
                "screen=%20\n")
                .arg(r.frames)
                .arg(r.warmup)
                .arg(two(r.span_ms))
                .arg(two(r.p50_ms))
                .arg(two(r.p95_ms))
                .arg(two(r.p99_ms))
                .arg(two(r.max_ms))
                .arg(r.over_16_7)
                .arg(r.over_33)
                .arg(two(r.first_frame_ms))
                .arg(two(r.render_mean_ms))
                .arg(two(r.render_max_ms))
                .arg(two(r.sync_mean_ms))
                .arg(two(r.sync_max_ms))
                .arg(two(r.refresh_hz))
                .arg(two(r.record_mean_ms))
                .arg(two(r.record_max_ms))
                .arg(two(r.animate_mean_ms))
                .arg(two(r.animate_max_ms))
                .arg(r.screen.isEmpty()
                         ? QStringLiteral("unknown")
                         : QString(r.screen).replace(QLatin1Char(' '), QLatin1Char('_')));

    text += QStringLiteral(
                "ui.frame_ms: %1 frames over %2 ms: mean %3, p50 %4, p95 %5, p99 %6, max %7 "
                "ms (the window is on '%9', which refreshes at %8 Hz)\n")
                .arg(r.frames)
                .arg(two(r.span_ms))
                .arg(two(r.mean_ms))
                .arg(two(r.p50_ms))
                .arg(two(r.p95_ms))
                .arg(two(r.p99_ms))
                .arg(two(r.max_ms))
                .arg(two(r.refresh_hz))
                .arg(r.screen);
    text += QStringLiteral("ui.frame_ms over 16.7 ms: %1 (%2%)   over 33 ms: %3\n")
                .arg(r.over_16_7)
                .arg(two(over_share))
                .arg(r.over_33);
    text += QStringLiteral("ui.first_frame_ms: %1 ms (process start to the first frame)\n")
                .arg(two(r.first_frame_ms));
    text += QStringLiteral(
                "render.frame_ms: %1 frames, mean %2, p95 %3, max %4 ms (the render thread's "
                "own time for a frame)\n")
                .arg(r.render_frames)
                .arg(two(r.render_mean_ms))
                .arg(two(r.render_p95_ms))
                .arg(two(r.render_max_ms));
    text += QStringLiteral(
                "render.record_ms: %1 frames, mean %2, p95 %3, max %4 ms (recording the main "
                "render pass: the scene's draw calls)\n")
                .arg(r.record_frames)
                .arg(two(r.record_mean_ms))
                .arg(two(r.record_p95_ms))
                .arg(two(r.record_max_ms));
    text += QStringLiteral(
                "ui.sync_ms: %1 frames, mean %2, p95 %3, max %4 ms (the render thread waiting "
                "for the GUI thread)\n")
                .arg(r.sync_frames)
                .arg(two(r.sync_mean_ms))
                .arg(two(r.sync_p95_ms))
                .arg(two(r.sync_max_ms));
    text += QStringLiteral(
                "ui.animate_ms: %1 frames, mean %2, p95 %3, max %4 ms (the GUI thread busy from "
                "the last synchronisation to the end of this frame's animations and polish)\n")
                .arg(r.animate_frames)
                .arg(two(r.animate_mean_ms))
                .arg(two(r.animate_p95_ms))
                .arg(two(r.animate_max_ms));
    return text;
}

}  // namespace wjs::app
