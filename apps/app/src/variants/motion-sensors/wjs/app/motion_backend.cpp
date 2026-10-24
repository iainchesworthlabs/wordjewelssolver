// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/app/motion_backend.hpp"

#include <QAccelerometer>
#include <QAccelerometerReading>
#include <QElapsedTimer>
#include <QGyroscope>
#include <QGyroscopeReading>
#include <QSensor>
#include <QString>
#include <algorithm>
#include <memory>
#include <utility>

namespace wjs::app {
namespace {

// The highest rate the platform lists for a sensor, or nothing when it lists
// none (then its own default is used). A tablet's list is what its hardware
// offers; a shake wants as much of it as the game can process.
template <typename Sensor>
void use_highest_rate(Sensor& sensor) {
    int rate = 0;
    for (const qrange& range : sensor.availableDataRates()) {
        rate = std::max({rate, range.first, range.second});
    }
    if (rate > 0) {
        sensor.setDataRate(rate);
    }
}

}  // namespace

class MotionBackend::Impl : public QObject {
public:
    explicit Impl(Sink sink) : sink_(std::move(sink)) {}

    [[nodiscard]] bool available() {
        if (!checked_) {
            checked_ = true;
            // A sensor type being registered is not a sensor being there: the
            // Windows runtime backend registers both types and then says the
            // device has neither, and start() still answers true with no
            // reading ever arriving. So this only says the module and a
            // default sensor are present; MotionSource's available() becomes
            // true when the first sample really arrives.
            const QByteArray type{QAccelerometer::sensorType};
            has_accelerometer_ = !QSensor::sensorsForType(type).isEmpty() &&
                                 !QSensor::defaultSensorForType(type).isEmpty();
        }
        return has_accelerometer_;
    }

    bool start() {
        if (!available()) {
            return false;
        }
        if (running_) {
            return true;
        }
        if (!accelerometer_) {
            make_sensors();
        }
        clock_.start();
        running_ = accelerometer_->start();
        if (!running_) {
            // The accelerometer is the stream that keeps time; without it the
            // gyroscope would run for nothing, draining the battery and delivering
            // to no one. Nothing is left running.
            accelerometer_->stop();
            return false;
        }
        if (gyroscope_) {
            // A gyroscope that will not start is not fatal: the accelerometer
            // still shakes the dice, with no turning.
            gyroscope_->start();
        }
        return true;
    }

    void stop() {
        if (accelerometer_) {
            accelerometer_->stop();
        }
        if (gyroscope_) {
            gyroscope_->stop();
        }
        running_ = false;
    }

private:
    void make_sensors() {
        accelerometer_ = std::make_unique<QAccelerometer>();
        use_highest_rate(*accelerometer_);
        QObject::connect(accelerometer_.get(), &QSensor::readingChanged, this, [this] { read(); });

        if (!QSensor::sensorsForType(QByteArray(QGyroscope::sensorType)).isEmpty()) {
            gyroscope_ = std::make_unique<QGyroscope>();
            use_highest_rate(*gyroscope_);
        }
    }

    void read() {
        const auto* accelerometer = accelerometer_->reading();
        if (accelerometer == nullptr || sink_ == nullptr) {
            return;
        }
        wjs::DeviceMotion sample;
        sample.t = static_cast<double>(clock_.nsecsElapsed()) / 1'000'000'000.0;
        sample.ax = accelerometer->x();
        sample.ay = accelerometer->y();
        sample.az = accelerometer->z();
        if (gyroscope_ != nullptr) {
            if (const auto* gyroscope = gyroscope_->reading()) {
                sample.wx = gyroscope->x();
                sample.wy = gyroscope->y();
                sample.wz = gyroscope->z();
            }
        }
        sink_(sample);
    }

    Sink sink_;
    bool checked_ = false;
    bool has_accelerometer_ = false;
    bool running_ = false;
    std::unique_ptr<QAccelerometer> accelerometer_;
    std::unique_ptr<QGyroscope> gyroscope_;
    QElapsedTimer clock_;
};

MotionBackend::MotionBackend(Sink sink) : impl_(std::make_unique<Impl>(std::move(sink))) {}

MotionBackend::~MotionBackend() {
    impl_->stop();
}

bool MotionBackend::available() {
    return impl_->available();
}

bool MotionBackend::start() {
    return impl_->start();
}

void MotionBackend::stop() {
    impl_->stop();
}

}  // namespace wjs::app
