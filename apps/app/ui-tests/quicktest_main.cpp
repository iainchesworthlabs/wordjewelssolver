// SPDX-License-Identifier: GPL-3.0-or-later
// The entry point of the Qt Quick Test harness (docs/testing.md, "The
// interactive QML"): every tst_*.qml under apps/app/ui-tests, one ctest entry each,
// headless.
//
// It is not QUICK_TEST_MAIN_WITH_SETUP because one thing has to happen after
// Quick Test returns and before the process ends: a run in which any Qt warning
// was emitted fails, even when every test function passed. (Quick Test's own
// failOnWarning() covers a warning inside a test function; see each suite's
// init(). The handler here covers the ones outside one: the engine loading a
// suite, a screen built at teardown.) The warning is the one thing qmllint
// cannot see, and it is a failure here for the same reason it is in the stub
// shells: a screen that warned is a screen that is wrong.
//
// The platform is offscreen before the application is created, so running the
// binary by hand with no arguments is headless too. -platform on the command
// line still wins, which is how a developer opens the suite in a window.
#include <QtQuickTest/quicktest.h>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <cstdio>

#include "quick_fixture.hpp"

namespace {

int g_warnings = 0;
QtMessageHandler g_previous = nullptr;

void message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    switch (type) {
        case QtDebugMsg:
        case QtInfoMsg:
            break;
        default:
            ++g_warnings;
            break;
    }
    if (g_previous != nullptr) {
        g_previous(type, context, message);
        return;
    }
    const QByteArray bytes = message.toLocal8Bit();
    std::fprintf(stderr, "%s\n", bytes.constData());
}

}  // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // No display and no GPU: the software renderer draws into memory, as the two
    // stub shells do. Set before the application exists.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    g_previous = qInstallMessageHandler(message_handler);

    QuickFixture fixture;
    const int result =
        quick_test_main_with_setup(argc, argv, "wjs_quick_tests", QUICK_TEST_SOURCE_DIR, &fixture);

    qInstallMessageHandler(g_previous);
    if (g_warnings != 0) {
        std::fprintf(stderr,
                     "wjs_quick_tests: %d Qt warning(s) during the run; a warning fails the "
                     "suite (docs/testing.md, \"The interactive QML\").\n",
                     g_warnings);
        return 1;
    }
    return result;
}
