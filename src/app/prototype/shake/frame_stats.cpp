#include "frame_stats.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QSGRendererInterface>
#include <QTimer>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

namespace {

QElapsedTimer& processClock() {
    static QElapsedTimer clock;
    return clock;
}

constexpr double kMsPerFrame60 = 16.7;
constexpr double kMsMissedRefresh = 25.0;  // a 60 Hz display's next refresh, and a half
constexpr double kMsPerFrame30 = 33.3;

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

int countAbove(const std::vector<double>& values, double limit) {
    return static_cast<int>(std::count_if(values.begin(), values.end(),
                                          [limit](double value) { return value > limit; }));
}

QVariantList firstOf(const std::vector<double>& values, std::size_t count) {
    QVariantList list;
    for (std::size_t i = 0; i < values.size() && i < count; ++i) {
        list.append(values[i]);
    }
    return list;
}

// Every value, to two places, so a run can be read again later.
QVariantList allOf(const std::vector<double>& values) {
    QVariantList list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const double value : values) {
        list.append(std::round(value * 100.0) / 100.0);
    }
    return list;
}

}  // namespace

FrameStats::FrameStats(QObject* parent) : QObject(parent) {
    if (!processClock().isValid()) {
        processClock().start();
    }
}

void FrameStats::startProcessClock() {
    processClock().start();
}

double FrameStats::now() const {
    return static_cast<double>(processClock().nsecsElapsed()) / 1.0e6;
}

void FrameStats::setWindow(QQuickWindow* window) {
    if (window == window_) {
        return;
    }
    if (window_) {
        disconnect(window_, nullptr, this, nullptr);
    }
    window_ = window;
    if (window_) {
        // The render thread emits these; the slots only stamp a time under a
        // lock, so they are connected directly.
        connect(window_, &QQuickWindow::frameSwapped, this, &FrameStats::onSwapped,
                Qt::DirectConnection);
        connect(window_, &QQuickWindow::beforeFrameBegin, this, &FrameStats::onFrameBegin,
                Qt::DirectConnection);
        connect(window_, &QQuickWindow::afterFrameEnd, this, &FrameStats::onFrameEnd,
                Qt::DirectConnection);
        connect(window_, &QQuickWindow::beforeSynchronizing, this, &FrameStats::onSyncBegin,
                Qt::DirectConnection);
        connect(window_, &QQuickWindow::afterSynchronizing, this, &FrameStats::onSyncEnd,
                Qt::DirectConnection);
    }
    emit windowChanged();
}

QString FrameStats::graphicsApi() const {
    if (!window_) {
        return {};
    }
    const QSGRendererInterface* rendererInterface = window_->rendererInterface();
    if (!rendererInterface) {
        return {};
    }
    switch (rendererInterface->graphicsApi()) {
        case QSGRendererInterface::OpenGL:
            return QStringLiteral("OpenGL");
        case QSGRendererInterface::Direct3D11:
            return QStringLiteral("Direct3D11");
        case QSGRendererInterface::Direct3D12:
            return QStringLiteral("Direct3D12");
        case QSGRendererInterface::Vulkan:
            return QStringLiteral("Vulkan");
        case QSGRendererInterface::Metal:
            return QStringLiteral("Metal");
        case QSGRendererInterface::Software:
            return QStringLiteral("Software");
        case QSGRendererInterface::Null:
            return QStringLiteral("Null");
        case QSGRendererInterface::Unknown:
            break;
    }
    return QStringLiteral("Unknown");
}

void FrameStats::begin() {
    const QMutexLocker locker(&mutex_);
    swaps_.clear();
    cpu_.clear();
    sync_.clear();
    beganAt_ = now();
    swaps_.reserve(8192);
    cpu_.reserve(8192);
    sync_.reserve(8192);
    recording_ = true;
}

QVariantMap FrameStats::end() {
    std::vector<double> swaps;
    std::vector<double> cpu;
    std::vector<double> sync;
    double beganAt = 0.0;
    {
        const QMutexLocker locker(&mutex_);
        recording_ = false;
        swaps = swaps_;
        cpu = cpu_;
        sync = sync_;
        beganAt = beganAt_;
    }

    std::vector<double> intervals;
    for (std::size_t i = 1; i < swaps.size(); ++i) {
        intervals.push_back(swaps[i] - swaps[i - 1]);
    }

    QVariantMap result;
    result.insert(QStringLiteral("frames"), static_cast<int>(swaps.size()));
    result.insert(QStringLiteral("spanMs"), swaps.size() > 1 ? swaps.back() - swaps.front() : 0.0);
    result.insert(QStringLiteral("firstSwapAtMs"), swaps.empty() ? 0.0 : swaps.front());
    result.insert(QStringLiteral("intervalMeanMs"), mean(intervals));
    result.insert(QStringLiteral("intervalP50Ms"), percentile(intervals, 0.50));
    result.insert(QStringLiteral("intervalP95Ms"), percentile(intervals, 0.95));
    result.insert(QStringLiteral("intervalP99Ms"), percentile(intervals, 0.99));
    result.insert(QStringLiteral("intervalMaxMs"), largest(intervals));
    // From begin() to the first frame: what a person waits for before anything
    // moves. It is not in the intervals, which are between frames.
    result.insert(QStringLiteral("firstFrameLatencyMs"),
                  swaps.empty() ? 0.0 : swaps.front() - beganAt);
    result.insert(QStringLiteral("over16_7"), countAbove(intervals, kMsPerFrame60));
    result.insert(QStringLiteral("over25"), countAbove(intervals, kMsMissedRefresh));
    result.insert(QStringLiteral("over33"), countAbove(intervals, kMsPerFrame30));
    result.insert(QStringLiteral("cpuMeanMs"), mean(cpu));
    result.insert(QStringLiteral("cpuP95Ms"), percentile(cpu, 0.95));
    result.insert(QStringLiteral("cpuMaxMs"), largest(cpu));
    result.insert(QStringLiteral("cpuOver16_7"), countAbove(cpu, kMsPerFrame60));
    result.insert(QStringLiteral("syncMeanMs"), mean(sync));
    result.insert(QStringLiteral("syncMaxMs"), largest(sync));
    result.insert(QStringLiteral("firstIntervalsMs"), firstOf(intervals, 6));
    result.insert(QStringLiteral("firstCpuMs"), firstOf(cpu, 6));
    result.insert(QStringLiteral("intervalsMs"), allOf(intervals));
    result.insert(QStringLiteral("cpuMs"), allOf(cpu));
    return result;
}

int FrameStats::recordedFrames() const {
    const QMutexLocker locker(&mutex_);
    return static_cast<int>(swaps_.size());
}

bool FrameStats::grab(const QString& path) {
    if (!window_) {
        return false;
    }
    const QImage image = window_->grabWindow();
    if (image.isNull()) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    return image.save(path);
}

bool FrameStats::writeText(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

void FrameStats::sendMouse(QEvent::Type type, const QPointF& position, Qt::MouseButtons buttons) {
    if (!window_) {
        return;
    }
    const Qt::MouseButton changed = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, position, window_->mapToGlobal(position), changed, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(window_, &event);
}

void FrameStats::dragBetween(double fromX, double fromY, double toX, double toY, int steps,
                             int intervalMs) {
    const QPointF from(fromX, fromY);
    const QPointF to(toX, toY);
    sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton);
    auto* timer = new QTimer(this);
    timer->setInterval(intervalMs);
    auto counter = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, counter, steps, from, to]() {
        // Back and forth, eight times across, so the pointer keeps moving fast.
        const double phase = static_cast<double>(*counter) / static_cast<double>(steps) * 8.0;
        const double along = std::abs(std::fmod(phase, 2.0) - 1.0);
        sendMouse(QEvent::MouseMove, from + (to - from) * along, Qt::LeftButton);
        if (++(*counter) >= steps) {
            sendMouse(QEvent::MouseButtonRelease, to, Qt::NoButton);
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

void FrameStats::onSwapped() {
    const double at = now();
    bool first = false;
    {
        const QMutexLocker locker(&mutex_);
        if (recording_) {
            swaps_.push_back(at);
        }
        if (!seenFirstFrame_) {
            seenFirstFrame_ = true;
            first = true;
        }
    }
    if (first) {
        QMetaObject::invokeMethod(
            this, [this, at]() { emit firstFrame(at); }, Qt::QueuedConnection);
    }
}

void FrameStats::onFrameBegin() {
    frameBeginAt_ = now();
}

void FrameStats::onFrameEnd() {
    const double elapsed = now() - frameBeginAt_;
    const QMutexLocker locker(&mutex_);
    if (recording_) {
        cpu_.push_back(elapsed);
    }
}

void FrameStats::onSyncBegin() {
    syncBeginAt_ = now();
}

void FrameStats::onSyncEnd() {
    const double elapsed = now() - syncBeginAt_;
    const QMutexLocker locker(&mutex_);
    if (recording_) {
        sync_.push_back(elapsed);
    }
}
