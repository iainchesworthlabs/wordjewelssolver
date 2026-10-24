// SPDX-License-Identifier: GPL-3.0-or-later
#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>

#include "budgets.hpp"

// The window's frame budget, measured on the application itself: its smoke run
// (`WordJewelsSolver --smoke <frames>`, docs/profiling.md, "The smoke mode") in
// a child process, and the report it prints. The budget is the design's
// (docs/design/performance.md, "Budgets"): one frame, and no frame over 33 ms
// during the shake and the drop. One frame is the DISPLAY's, which the report
// carries (libs/core/bench/budgets.hpp, "The window's frame"): 16.7 ms on a 60 Hz
// screen and 10 ms on a 100 Hz one.
//
// It is a wall-time check, so it is treated as the performance tier's are
// (docs/testing.md, "Performance tests"): it skips in a build where a clock is
// not about the program (Debug, coverage, Tracy), and it is opt-in with its own
// variable -- WJS_PERF_FRAME_TIMING=1 -- because it also needs a display and
// must never fail a shared runner. CTest runs it alone (RUN_SERIAL).

namespace {

// Read through volatile, so that an optimising compiler cannot see the value: it would see
// that SKIP always ends the case and warn (C4702) about every statement after it.
const volatile bool optimised = WJS_TEST_OPTIMISED != 0;
const volatile bool instrumented = WJS_TEST_INSTRUMENTED != 0;

// The frames measured, after the smoke run's own warm-up. Four seconds at
// 60 Hz: enough shakes and drops for a tail to mean something.
constexpr int kFrames = 240;
constexpr int kTimeoutMs = 180'000;

[[nodiscard]] bool frame_timing_requested() {
    const QByteArray value = qgetenv("WJS_PERF_FRAME_TIMING");
    return !value.isEmpty() && value != "0";
}

// Stands the case aside in a build where a frame interval is not a statement
// about the program, and where nobody asked for one. A call, not `if constexpr`:
// SKIP ends the case, and a compiler that can see it always does would warn
// about the statements after it.
void skip_unless_frame_wanted() {
    if (!optimised) {
        SKIP("a frame interval is not about the program in a build that is not optimised");
    }
    if (instrumented) {
        SKIP(
            "the profiling calls and the coverage counters are compiled in: the frame times of "
            "this build are not the program's");
    }
    if (!frame_timing_requested()) {
        SKIP(
            "the window's frame check is opt-in and needs a display: set "
            "WJS_PERF_FRAME_TIMING=1 on a machine with one (docs/testing.md, \"Performance "
            "tests\")");
    }
    if (qEnvironmentVariable("QT_QPA_PLATFORM")
            .startsWith(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        SKIP(
            "Quick 3D does not render on the offscreen platform plugin (docs/profiling.md, "
            "\"The smoke mode\"): its frames are the 2D screens and not the scene");
    }
}

// The application's own binary, beside this test's (the build puts every program
// in one directory).
[[nodiscard]] QString application_path() {
    const QString directory = QFileInfo(QString::fromUtf8(WJS_APP_PATH)).absolutePath();
    for (const QString& name :
         {QStringLiteral("WordJewelsSolver.exe"), QStringLiteral("WordJewelsSolver")}) {
        const QString path = directory + QLatin1Char('/') + name;
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

// The one line the smoke run prints for a machine (`smoke: key=value ...`), as
// the numbers it holds.
[[nodiscard]] QHash<QString, double> smoke_summary(const QString& output) {
    QHash<QString, double> values;
    for (const QString& line : output.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QStringLiteral("smoke: "))) {
            continue;
        }
        const QStringList tokens = line.mid(7).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& token : tokens) {
            const qsizetype at = token.indexOf(QLatin1Char('='));
            if (at <= 0) {
                continue;
            }
            bool ok = false;
            const double value = token.mid(at + 1).toDouble(&ok);
            if (ok) {
                values.insert(token.left(at), value);
            }
        }
    }
    return values;
}

// A word of the summary line that is not a number (`screen=Acer_X34_P`).
[[nodiscard]] QString smoke_word(const QString& output, const QString& key) {
    for (const QString& line : output.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QStringLiteral("smoke: "))) {
            continue;
        }
        for (const QString& token : line.mid(7).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (token.startsWith(key + QLatin1Char('='))) {
                return token.mid(key.size() + 1);
            }
        }
    }
    return {};
}

// The application object QProcess needs, made once (Catch2's main makes none).
QCoreApplication& application() {
    static int argc = 1;
    static char name[] = "wjs_frame_timing_tests";
    static char* argv[] = {name, nullptr};
    static QCoreApplication instance(argc, argv);
    return instance;
}

}  // namespace

TEST_CASE("the window holds the frame budget through the smoke run's shakes", "[app][frame]") {
    skip_unless_frame_wanted();
    application();

    const QString app = application_path();
    INFO("the application, from WJS_APP_PATH: " << WJS_APP_PATH);
    if (app.isEmpty()) {
        SKIP("the application is not built (WJS_BUILD_APP=OFF, or this test was built alone)");
    }

    QTemporaryDir directory;
    REQUIRE(directory.isValid());

    QProcess process;
    process.setProgram(app);
    process.setArguments({QStringLiteral("--smoke"), QString::number(kFrames),
                          // A store of its own: the smoke writes the boards it
                          // plays, and a run must not touch the person's.
                          QStringLiteral("--store"),
                          directory.filePath(QStringLiteral("store.wjs"))});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setWorkingDirectory(QFileInfo(app).absolutePath());
    process.start();
    REQUIRE(process.waitForStarted(kTimeoutMs));
    if (!process.waitForFinished(kTimeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        FAIL("the smoke run did not finish in " << kTimeoutMs / 1000 << " s");
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QString errors = QString::fromUtf8(process.readAllStandardError());
    INFO("stdout:\n" << output.toStdString());
    INFO("stderr:\n" << errors.toStdString());

    if (process.exitStatus() != QProcess::NormalExit) {
        FAIL("the smoke run was killed or crashed");
    }
    if (process.exitCode() == 3) {
        // The application says no frame was presented: Quick 3D needs a display,
        // and there is not one here (docs/profiling.md, "The smoke mode").
        SKIP("the window could not present a frame: " << errors.toStdString());
    }
    REQUIRE(process.exitCode() == 0);

    const QHash<QString, double> report = smoke_summary(output);
    REQUIRE(report.contains(QStringLiteral("frames")));
    REQUIRE(report.contains(QStringLiteral("p50_ms")));
    REQUIRE(report.contains(QStringLiteral("max_ms")));
    REQUIRE(report.contains(QStringLiteral("over33")));
    REQUIRE(report.contains(QStringLiteral("refresh_hz")));

    INFO("frames " << report.value(QStringLiteral("frames")) << ", window on screen '"
                   << smoke_word(output, QStringLiteral("screen")).toStdString() << "', display "
                   << report.value(QStringLiteral("refresh_hz")) << " Hz, p50 "
                   << report.value(QStringLiteral("p50_ms")) << " ms, p95 "
                   << report.value(QStringLiteral("p95_ms")) << " ms, max "
                   << report.value(QStringLiteral("max_ms")) << " ms, over 33 ms "
                   << report.value(QStringLiteral("over33")) << "; render thread mean "
                   << report.value(QStringLiteral("render_mean_ms")) << " ms of which recording "
                   << report.value(QStringLiteral("record_mean_ms")) << " ms, GUI thread mean "
                   << report.value(QStringLiteral("animate_mean_ms")) << " ms to animate and "
                   << report.value(QStringLiteral("sync_mean_ms")) << " ms to sync");

    // The frames asked for, and the warm-up left out of them.
    CHECK(report.value(QStringLiteral("frames")) == static_cast<double>(kFrames));
    // The typical frame is one period of the display the window was on, and all
    // but the odd one are close to it.
    const double refresh_hz = report.value(QStringLiteral("refresh_hz"));
    CHECK(report.value(QStringLiteral("p50_ms")) <=
          wjs::perf::budget::frame_p50_budget_ms(refresh_hz));
    CHECK(report.value(QStringLiteral("p95_ms")) <=
          wjs::perf::budget::frame_p95_budget_ms(refresh_hz));
    // No frame over 33 ms during the shake and the drop.
    CHECK(report.value(QStringLiteral("max_ms")) <= wjs::perf::budget::kFrameMaxMs);
    CHECK(report.value(QStringLiteral("over33")) == 0.0);
    // The report is of a window that drew.
    CHECK(report.value(QStringLiteral("first_frame_ms")) > 0.0);
}

// The budget's arithmetic is always checked, with or without a display: it is the
// part of the check that can be wrong on a machine that never runs the window.
TEST_CASE("the frame budget follows the display's refresh rate", "[app][frame]") {
    namespace budget = wjs::perf::budget;

    // 60 Hz is the budget the design states: 17 ms for the median.
    CHECK(budget::frame_p50_budget_ms(60.0) == Catch::Approx(16.97).margin(0.01));
    CHECK(budget::frame_p50_budget_ms(60.0) <= 17.0);
    // A 100 Hz display is held to 10 ms, so a window that skips every second
    // refresh (a 20 ms median) fails it, and one that holds them passes.
    CHECK(budget::frame_p50_budget_ms(100.0) == Catch::Approx(10.3));
    CHECK(19.9 > budget::frame_p50_budget_ms(100.0));
    CHECK(10.03 <= budget::frame_p50_budget_ms(100.0));
    CHECK(budget::frame_p95_budget_ms(100.0) == Catch::Approx(12.0));
    CHECK(budget::frame_p95_budget_ms(144.0) < budget::frame_p95_budget_ms(60.0));

    // A refresh rate that is not one is the 60 Hz budget, not a division by zero.
    for (const double unknown : {0.0, -1.0, 1.0e9, std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
        CHECK(budget::frame_period_ms(unknown) == Catch::Approx(1000.0 / 60.0));
    }
}
