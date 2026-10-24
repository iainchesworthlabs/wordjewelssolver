// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QMetaObject>
#include <QMetaProperty>
#include <QStyleHints>
#include <catch2/catch_test_macros.hpp>

#include "system_colour_scheme.hpp"
#include "test_support.hpp"

namespace {

using wjs::app::SystemColourScheme;

// A GUI application on Qt's offscreen platform (the entry's environment chooses
// it), which needs no display: the style hints SystemColourScheme listens to
// belong to it.
class OffscreenApplication : private wjs::app::test::GuiApplication {
public:
    // The system's style hints, which a test then tells of a change: the
    // platform that would say so is not there to ask. A signal is public in Qt 6;
    // the hints are const because nothing but the platform changes them.
    [[nodiscard]] QStyleHints& hints() const {
        return *const_cast<QStyleHints*>(QGuiApplication::styleHints());
    }
};

}  // namespace

TEST_CASE("the singleton starts with what the system's colour scheme is", "[app][system][colour]") {
    OffscreenApplication app;
    SystemColourScheme scheme;

    const bool asked = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    CHECK(scheme.dark() == asked);
}

TEST_CASE("a change of the system's colour scheme is reported, once", "[app][system][colour]") {
    OffscreenApplication app;
    SystemColourScheme scheme;
    app.hints().colorSchemeChanged(Qt::ColorScheme::Light);
    REQUIRE_FALSE(scheme.dark());

    int reports = 0;
    QObject::connect(&scheme, &SystemColourScheme::darkChanged, [&reports] { ++reports; });

    app.hints().colorSchemeChanged(Qt::ColorScheme::Light);
    CHECK_FALSE(scheme.dark());
    CHECK(reports == 0);  // no change, nothing to report

    app.hints().colorSchemeChanged(Qt::ColorScheme::Dark);
    CHECK(scheme.dark());
    CHECK(reports == 1);

    app.hints().colorSchemeChanged(Qt::ColorScheme::Dark);
    CHECK(scheme.dark());
    CHECK(reports == 1);

    app.hints().colorSchemeChanged(Qt::ColorScheme::Unknown);
    CHECK_FALSE(scheme.dark());
    CHECK(reports == 2);
}

TEST_CASE("the colour scheme property is read by name and notifies, as QML needs",
          "[app][system][colour]") {
    OffscreenApplication app;
    SystemColourScheme scheme;
    app.hints().colorSchemeChanged(Qt::ColorScheme::Light);

    const QMetaObject* meta = scheme.metaObject();
    const QMetaProperty property = meta->property(meta->indexOfProperty("dark"));
    REQUIRE(property.isValid());
    CHECK(property.hasNotifySignal());
    CHECK_FALSE(property.read(&scheme).toBool());

    app.hints().colorSchemeChanged(Qt::ColorScheme::Dark);
    CHECK(property.read(&scheme).toBool());
}
