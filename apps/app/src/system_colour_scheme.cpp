// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_colour_scheme.hpp"

#include <QGuiApplication>
#include <QStyleHints>

namespace wjs::app {

SystemColourScheme::SystemColourScheme(QObject* parent) : QObject(parent) {
    if (const auto* hints = QGuiApplication::styleHints()) {
        dark_ = hints->colorScheme() == Qt::ColorScheme::Dark;
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
            const bool dark = scheme == Qt::ColorScheme::Dark;
            if (dark == dark_) {
                return;
            }
            dark_ = dark;
            emit darkChanged();
        });
    }
}

bool SystemColourScheme::dark() const {
    return dark_;
}

}  // namespace wjs::app
