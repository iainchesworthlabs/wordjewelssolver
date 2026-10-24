// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace wjs::app {

// The one place the application's text crosses between the library's (UTF-8, in
// std::string and std::string_view) and Qt's (UTF-16, in QString). Every
// conversion is explicit and goes through UTF-8 or UTF-16 by name, so it does not
// depend on the locale or on QString's `toStdString` / `QString::fromStdString`
// conventions.

// Library text as a QString. Text that is not valid UTF-8 comes out with the
// replacement character in it rather than failing.
[[nodiscard]] inline QString to_qstring(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A QString as library text.
[[nodiscard]] inline std::string to_utf8(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string{bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

// A filesystem path from Qt text. Going through UTF-16 keeps it portable: the
// standard converts to the native encoding, where a narrow string would not
// survive a path with letters the system's code page lacks.
[[nodiscard]] inline std::filesystem::path to_path(const QString& text) {
    return std::filesystem::path{text.toStdU16String()};
}

}  // namespace wjs::app
