// SPDX-License-Identifier: GPL-3.0-or-later
#include "startup_failure.hpp"

#include <QByteArray>
#include <QtGlobal>
#include <cstddef>
#include <string_view>

#include "wjs/platform/fatal_error.hpp"

namespace wjs::app {

bool dialogs_wanted(std::string_view qpa_platform, bool no_dialogs_set) noexcept {
    if (no_dialogs_set) {
        return false;
    }
    return qpa_platform != "offscreen" && qpa_platform != "minimal";
}

bool dialogs_wanted_here() {
    const QByteArray qpa = qgetenv("QT_QPA_PLATFORM");
    return dialogs_wanted(std::string_view{qpa.constData(), static_cast<std::size_t>(qpa.size())},
                          qEnvironmentVariableIsSet("WJS_NO_DIALOGS"));
}

int fail_startup(ExitStatus status, const QString& message, const FatalErrorSink& sink) {
    const QByteArray text = message.toUtf8();
    const std::string_view body{text.constData(), static_cast<std::size_t>(text.size())};
    constexpr std::string_view title = "Word Jewels Solver cannot start";
    const bool interactive = dialogs_wanted_here();
    if (sink) {
        sink(title, body, interactive);
    } else {
        wjs::platform::show_fatal_error(title, body, interactive);
    }
    return exit_code(status);
}

}  // namespace wjs::app
