// SPDX-License-Identifier: GPL-3.0-or-later
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <string_view>

#include "qt_text.hpp"

// The one place text crosses between the library (UTF-8) and Qt (UTF-16), and a
// path from Qt text. The words are ASCII, a phonetic sound or a folder name need
// not be.

using wjs::app::to_path;
using wjs::app::to_qstring;
using wjs::app::to_utf8;

TEST_CASE("UTF-8 library text and a QString are the same text", "[app][text]") {
    const std::string utf8 = "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC";

    const QString text = to_qstring(utf8);

    CHECK(text.size() == 7);  // c a f é space 日 本
    CHECK(text.at(3) == QChar(0x00E9));
    CHECK(to_utf8(text) == utf8);
    CHECK(to_qstring(std::string_view{}).isEmpty());
    CHECK(to_utf8(QString()).empty());
}

TEST_CASE("text with an embedded NUL keeps its length", "[app][text]") {
    const std::string with_nul("a\0b", 3);

    CHECK(to_qstring(with_nul).size() == 3);
    CHECK(to_utf8(to_qstring(with_nul)) == with_nul);
}

TEST_CASE("a path from Qt text keeps letters the system's code page lacks", "[app][text]") {
    const QString name = QString::fromUtf8("m\xC3\xA9moire-\xE6\x97\xA5\xE6\x9C\xAC.txt");

    const std::filesystem::path path = to_path(name);

    // Through UTF-16 to the filesystem's own encoding, and back, unchanged.
    CHECK(QString::fromStdU16String(path.u16string()) == name);
    CHECK(to_path(QStringLiteral("a/b.txt")).filename() == std::filesystem::path("b.txt"));
    CHECK(to_path(QString()).empty());
}
