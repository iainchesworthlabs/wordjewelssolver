// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QQuickWindow>
#include <QString>

#include <memory>
#include <vector>

namespace wjs::app {

// The frame clock of the window (docs/profiling.md, "The frame"): a frame mark
// named "UI" at every swap, the interval between swaps as the plot
// `ui.frame_ms`, the frame split into what the GUI thread and the render thread
// each spent (`ui.animate_ms`, `ui.sync_ms`, `render.record_ms`,
// `render.frame_ms`), and a rolling window of all of it from which a report is
// made.
//
// The swap is where a frame ends, so frameSwapped is connected directly: the
// slot stamps a monotonic time and sends the mark and the plot where the frame
// ended (the render thread of Qt's threaded loop, the GUI thread of the basic
// one). It never blocks the render thread beyond a short mutex around one
// number, and it never touches the window. The GUI thread names itself "main";
// the render thread is named "render" the first time the scene graph is
// initialised.
//
// The report is what the smoke mode prints: count, mean, p50, p95, p99 and max
// of the frame interval, the frames over 16.7 and over 33 ms, the first
// frame's time from the start of the process, and the refresh rate of the
// screen the window is on, which is what the frame budget is held against
// (libs/core/bench/budgets.hpp). It is measured on delivery of frameSwapped, so a
// frame the compositor only presented late counts late.
class FrameProfiler : public QObject {
    Q_OBJECT

public:
    // Attaches to `window`, which must outlive this. The window is the one the
    // QML engine made; the application sets it up after loadFromModule and
    // before exec, so the first frame is measured.
    explicit FrameProfiler(QQuickWindow* window, QObject* parent = nullptr);
    ~FrameProfiler() override;

    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;

    // Starts the process clock. Called first thing in main, so that "time from
    // the start of the process" includes Qt's own start-up.
    static void start_process_clock();
    // Milliseconds since start_process_clock().
    [[nodiscard]] static double now_ms();

    // Records `frames` frame intervals after leaving out the first `warmup` of
    // them (the window's first frames create the scene and its textures, and
    // are not a steady frame), then emits finished() on the GUI thread. The
    // render thread's and the GUI thread's times leave out the same frames. A
    // target of 0 -- the default -- only keeps the rolling window.
    void set_target(int frames, int warmup);

    void set_window(QQuickWindow* window);

    [[nodiscard]] int recorded_frames() const;
    [[nodiscard]] bool first_frame_seen() const;

    struct Report {
        int frames = 0;
        int warmup = 0;
        double span_ms = 0.0;
        double mean_ms = 0.0;
        double p50_ms = 0.0;
        double p95_ms = 0.0;
        double p99_ms = 0.0;
        double max_ms = 0.0;
        int over_16_7 = 0;
        int over_33 = 0;

        double first_frame_ms = 0.0;

        int render_frames = 0;
        double render_mean_ms = 0.0;
        double render_p95_ms = 0.0;
        double render_max_ms = 0.0;

        int sync_frames = 0;
        double sync_mean_ms = 0.0;
        double sync_p95_ms = 0.0;
        double sync_max_ms = 0.0;

        // The render thread recording the scene graph's main render pass
        // (beforeRenderPassRecording to afterRenderPassRecording): the part of
        // render.frame_ms that is the scene's own draw calls.
        int record_frames = 0;
        double record_mean_ms = 0.0;
        double record_p95_ms = 0.0;
        double record_max_ms = 0.0;

        // The GUI thread's busy time for a frame (ui.animate_ms): from the end
        // of the last frame's synchronisation, which released it, to the end of
        // this frame's animations and polish. The events, the animations' tick,
        // the bindings that set off and the polish: the part of a frame that is
        // QML, before the render thread is asked to synchronise.
        int animate_frames = 0;
        double animate_mean_ms = 0.0;
        double animate_p95_ms = 0.0;
        double animate_max_ms = 0.0;

        // The refresh rate of the window's screen in Hz, 0 when it is not known
        // (no window or no screen).
        double refresh_hz = 0.0;
        // And its name ("SyncMaster"), empty when not known: which of several
        // screens the window was on is half of reading a frame time.
        QString screen;
    };

    [[nodiscard]] Report report() const;
    // The report as the smoke mode prints it: one machine-readable summary line
    // (`smoke: frames=... p50_ms=...`) and one line for each thing it measured.
    [[nodiscard]] QString report_text() const;

signals:
    // The target set with set_target() was reached. Emitted on the GUI thread.
    void finished();

private:
    // Everything the slots touch, apart from the window: shared with the
    // connections, defined in the .cpp.
    struct State;

    void finish_guarded();
    void connect_window();
    void disconnect_window();

    QQuickWindow* window_ = nullptr;
    std::shared_ptr<State> state_;
    std::vector<QMetaObject::Connection> connections_;
};

}  // namespace wjs::app
