// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QObject>

namespace wjs::app {

// Whether the system is in dark mode, for the chrome around the board (the board
// itself is dark in both: docs/design/application.md, "Colour and contrast").
// A wrapper on QStyleHints::colorSchemeChanged and not a QML binding on
// Qt.styleHints.colorScheme, whose change notification QML does not get (the
// lesson of Cubeulator's SystemTheme).
//
// The value the hint reported is kept, and darkChanged is emitted only when the
// answer changes from light to dark or back: a scheme change between two light
// schemes is no change here. Keeping it is also what lets a test drive the
// wrapper: the platform's hint cannot be set from outside, so its signal is the
// only way to say "the scheme is now dark".
class SystemColourScheme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)

public:
    explicit SystemColourScheme(QObject* parent = nullptr);

    [[nodiscard]] bool dark() const;

signals:
    void darkChanged();

private:
    bool dark_ = false;
};

}  // namespace wjs::app
