// SPDX-License-Identifier: GPL-3.0-or-later
#include "profiler.hpp"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wjs::scene {
namespace {

// A name as UTF-8 for the wrapper. It is only made in a build with Tracy: the
// disabled variant's begin() and mark() return before this is called, so a
// normal build does not pay for the conversion or the allocation.
[[maybe_unused, nodiscard]] std::string_view utf8(const QByteArray& text) noexcept {
    return {text.constData(), static_cast<std::size_t>(text.size())};
}

}  // namespace

Profiler::Profiler(QObject* parent) : QObject(parent) {}

void Profiler::begin(const QString& name) {
    // Every begin() is matched by its end(), whether or not it opens a zone:
    // one with nothing to open (an empty name, a build without Tracy) still
    // takes a place on the stack, and one past its depth is counted. Without
    // that an end() would close the enclosing zone, which is some other
    // moment's.
    if (depth_ >= kMaxDepth) {
        ++overflow_;
        return;
    }
    if constexpr (wjs::profiling::enabled) {
        if (!name.isEmpty()) {
            const QByteArray text = name.toUtf8();
            // In place: the scope is not movable (Tracy's zone ends where it was
            // opened), which is why the stack is a fixed array of optionals.
            open_[depth_].emplace(utf8(text));
        }
    } else {
        static_cast<void>(name);
    }
    ++depth_;
}

void Profiler::end() {
    if (overflow_ > 0) {
        --overflow_;
        return;
    }
    if (depth_ > 0) {
        --depth_;
        open_[depth_].reset();
    }
}

void Profiler::value(qulonglong number) {
    if constexpr (wjs::profiling::enabled) {
        // The innermost zone is a place-holder when its begin() had no name to
        // open; there is nothing to give the number to then.
        if (overflow_ == 0 && depth_ > 0 && open_[depth_ - 1].has_value()) {
            open_[depth_ - 1]->Value(static_cast<std::uint64_t>(number));
        }
    }
    static_cast<void>(number);
}

void Profiler::mark(const QString& name) {
    if (name.isEmpty()) {
        return;
    }
    if constexpr (wjs::profiling::enabled) {
        const QByteArray text = name.toUtf8();
        // Opened and closed where it is declared: a zero-length zone.
        [[maybe_unused]] const wjs::profiling::RuntimeScope point{utf8(text)};
    }
}

}  // namespace wjs::scene
