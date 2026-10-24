// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QObject>

namespace wjs::app {

// Whether the system asks for reduced motion, for the parent area's Motion setting
// "System" (docs/design/parent-area.md, "Integration"). What the system's setting is
// called, and whether there is one, is the platform's and Qt's to say (Qt 6.12's
// QAccessibilityHints::motionPreference). A wrapper on its motionPreferenceChanged
// and not a QML binding on Qt.styleHints, whose change notification QML does not get
// (the lesson of Cubeulator's SystemTheme; SystemColourScheme is the same pattern).
//
// It only reports. Whether the system's preference applies is the Motion setting's:
// System follows it, Reduced does not need it (qml/Main.qml).
class SystemMotion : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool reduced READ reduced NOTIFY reducedChanged)

public:
    explicit SystemMotion(QObject* parent = nullptr);

    // The system asks for reduced motion. False when it expresses no preference,
    // and when this platform cannot say (Qt reports "no preference").
    [[nodiscard]] bool reduced() const noexcept { return reduced_; }

signals:
    // Only when the answer changes.
    void reducedChanged();

private:
    void update(bool reduced);

    bool reduced_ = false;
};

}  // namespace wjs::app
