// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <memory>

#include "wjs/motion.hpp"

namespace wjs::app {

// The device's motion, from Qt Sensors when the application was built with it
// and never when it was not. One sample of the accelerometer and the gyroscope
// becomes:
//   - sample(ax, ay, az, wx, wy, wz), the numbers the board scene pushes its
//     dice with (accelerometer in g, gravity included; gyroscope in rad/s);
//   - shakeDetected(), once per real shake, which rolls the box like the Shake
//     button;
// all of it through the pure wjs::MotionProcessor, so the processing is tested
// on a recorded trace without hardware.
//
// The application starts it only while Solo Play's box or the falling dice
// need it, and only while the parent has Shake on; leaving the screen stops it.
// On the desktops there is no accelerometer: available() is false, no sample
// arrives, and the drag that stands in for one stays (SoloScreen.qml).
//
// It is read only: no sensor value is stored, sent or written anywhere.
class MotionSource : public QObject {
    Q_OBJECT

    // Whether a sample has really arrived from this platform's sensors. False
    // until then, including on the three desktops, whose Windows runtime
    // backend registers the sensor types and then sends nothing. (The backend
    // module being present is a build fact, not a device fact.)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // Whether the sensors are running now.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // Whether a detected shake should roll the box. The board scene still gets
    // every sample when this is false: motion is also how the dice are pushed.
    Q_PROPERTY(bool shakeEnabled READ shakeEnabled WRITE setShakeEnabled NOTIFY shakeEnabledChanged)

public:
    explicit MotionSource(QObject* parent = nullptr);
    ~MotionSource() override;

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool shakeEnabled() const noexcept { return shake_enabled_; }

    void setActive(bool active);
    void setShakeEnabled(bool enabled);

    // One sample of the device's motion: the way the sensor backend delivers each
    // reading, and so the way a recorded trace is played through the same path
    // (processing, availability, the shake gate) without a sensor.
    void accept(const wjs::DeviceMotion& motion);

signals:
    void availableChanged();
    void activeChanged();
    void shakeEnabledChanged();

    // One sample, ready for the board scene's pushMotion().
    void sample(qreal ax, qreal ay, qreal az, qreal wx, qreal wy, qreal wz);
    // A new shake has been detected; it is at most one per shake.
    void shakeDetected();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    bool active_ = false;
    bool available_ = false;
    bool shake_enabled_ = true;
};

}  // namespace wjs::app
