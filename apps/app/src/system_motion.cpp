// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_motion.hpp"

#include <QAccessibilityHints>
#include <QGuiApplication>
#include <QStyleHints>

namespace wjs::app {

SystemMotion::SystemMotion(QObject* parent) : QObject(parent) {
    const auto* style = QGuiApplication::styleHints();
    const QAccessibilityHints* hints = style != nullptr ? style->accessibility() : nullptr;
    if (hints == nullptr) {
        return;
    }
    reduced_ = hints->motionPreference() == Qt::MotionPreference::ReducedMotion;
    connect(hints, &QAccessibilityHints::motionPreferenceChanged, this,
            [this](Qt::MotionPreference preference) {
                update(preference == Qt::MotionPreference::ReducedMotion);
            });
}

void SystemMotion::update(bool reduced) {
    if (reduced == reduced_) {
        return;
    }
    reduced_ = reduced;
    emit reducedChanged();
}

}  // namespace wjs::app
