// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QString>
#include <QVariantMap>

#include <vector>

// Frame timing for the shake prototype, taken from the scene graph's own
// signals so the scene being measured carries no measuring code.
//
// While a recording is open it keeps, for every frame the window draws:
//   - the time the frame was queued for presenting (frameSwapped), from which
//     the interval between frames comes;
//   - the render thread's CPU time for the frame (beforeFrameBegin to
//     afterFrameEnd), which is the GPU command recording and submission, not
//     the GPU's own time;
//   - the time the render thread spent synchronising with the GUI thread
//     (beforeSynchronizing to afterSynchronizing), during which the GUI
//     thread waits.
class FrameStats : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged)

public:
    explicit FrameStats(QObject* parent = nullptr);

    // Starts the process clock. Called first thing in main, so that "time since
    // the process started" includes Qt's own start-up.
    static void startProcessClock();

    QQuickWindow* window() const { return window_; }
    void setWindow(QQuickWindow* window);

    // Milliseconds since the process clock started.
    Q_INVOKABLE double now() const;
    // The graphics API the scene graph is drawing with ("Direct3D11",
    // "OpenGL", ...), or "" until the scene graph exists.
    Q_INVOKABLE QString graphicsApi() const;

    // Starts collecting frames; end() stops and summarises.
    Q_INVOKABLE void begin();
    Q_INVOKABLE QVariantMap end();
    // How many frames have been presented since begin().
    Q_INVOKABLE int recordedFrames() const;

    // Saves what the window shows now to a PNG file.
    Q_INVOKABLE bool grab(const QString& path);
    // Writes text to a file (UTF-8), making the directory if it is missing.
    Q_INVOKABLE bool writeText(const QString& path, const QString& text);
    // Sends the window a left-button press at (fromX, fromY), then a move every
    // intervalMs through `steps` points of a back-and-forth path between the
    // two, then the release. Stands in for a hand dragging the board, so the
    // pointer path through the scene is exercised without one.
    Q_INVOKABLE void dragBetween(double fromX, double fromY, double toX, double toY, int steps,
                                 int intervalMs);

signals:
    void windowChanged();
    // The first frame the window presents, on the GUI thread.
    void firstFrame(double atMs);

private:
    void sendMouse(QEvent::Type type, const QPointF& position, Qt::MouseButtons buttons);
    void onSwapped();
    void onFrameBegin();
    void onFrameEnd();
    void onSyncBegin();
    void onSyncEnd();

    QPointer<QQuickWindow> window_;
    mutable QMutex mutex_;
    bool recording_ = false;
    bool seenFirstFrame_ = false;
    double beganAt_ = 0;
    double frameBeginAt_ = 0;
    double syncBeginAt_ = 0;
    std::vector<double> swaps_;
    std::vector<double> cpu_;
    std::vector<double> sync_;
};
