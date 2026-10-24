// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <array>
#include <cstddef>
#include <optional>

#include "wjs/profiling.hpp"

namespace wjs::scene {

// The QML side of the profiling calls (docs/profiling.md, "QML and the scene"):
// a QML singleton that opens and closes a named Tracy zone around a moment in
// QML or JavaScript, so that a binding storm, a view rebuild or a property
// cascade is a zone in the trace and not only a slow frame. Tracy's API is
// macros and QML has no preprocessor, so the name arrives here as a string and
// the wrapper's run-time scope (wjs::profiling::RuntimeScope) carries it: one
// cached source location per name, values included.
//
// The discipline, because QML has no scopes of its own:
//
//   - One begin() per synchronous moment, and its end() in the same function,
//     after the work. Nesting is allowed and must be last-in first-out, as the
//     calls are: `begin("outer"); begin("inner"); end(); end()`.
//   - The calls are on one thread (QML's). A begin() and an end() on different
//     threads would end a zone Tracy opened for another one.
//   - Every begin() is balanced by its end(), even one that opens no zone: an
//     empty name, or a begin() past the seam's depth (which is counted and not
//     nested without limit). An end() never closes a zone another moment
//     opened, and an end() with nothing open does nothing. A begin() without
//     its end() leaves the stack one deep.
//   - value() sets the number shown beside the innermost open zone, as a
//     compile-time zone's Value() does (the cells an atlas built, say).
//   - mark() is a moment with no work: a zero-length zone, for the trace's
//     timeline rather than for a measurement.
//
// In a build without Tracy every call is empty: no conversion of the name, no
// allocation, no work. The singleton is in the scene's QML module
// (WordJewelsSolver.Scene) because the scene and the application both import
// it; the application's own modules use it too.
class Profiler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Profiler(QObject* parent = nullptr);

    // Opens a zone named `name` and leaves it open until end(). The name is
    // `<layer>.<thing>`, as every zone's is (docs/profiling.md); with an empty
    // one no zone opens, but the call is still matched by its end().
    Q_INVOKABLE void begin(const QString& name);
    // Closes the innermost open begin(), if there is one.
    Q_INVOKABLE void end();
    // A number shown beside the innermost open zone.
    Q_INVOKABLE void value(qulonglong number);
    // A discrete moment: a zero-length zone named `name`.
    Q_INVOKABLE void mark(const QString& name);

    // How many begin() calls have no end() yet, in any build (apps/app/tests/unit/
    // profiler_test.cpp holds the balance on it): the stack's depth, and the
    // calls past it.
    [[nodiscard]] std::size_t open_count() const noexcept { return depth_ + overflow_; }

private:
    // How deep the seam will nest. Deeper than any synchronous moment of the
    // screens, and small enough that a begin() without its end() cannot grow
    // without bound.
    static constexpr std::size_t kMaxDepth = 8;

    std::array<std::optional<wjs::profiling::RuntimeScope>, kMaxDepth> open_;
    std::size_t depth_ = 0;
    // The begin() calls past kMaxDepth, whose end() calls are to be ignored.
    std::size_t overflow_ = 0;
};

}  // namespace wjs::scene
