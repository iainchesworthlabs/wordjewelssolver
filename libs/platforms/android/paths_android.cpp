// SPDX-License-Identifier: GPL-3.0-or-later
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <filesystem>

#include "wjs/platform/paths.hpp"

namespace wjs::platform {

namespace {

// The resource tree the data was compiled into (wjs_platform_configure_app in
// platform.cmake, WJS_ANDROID_DATA_RESOURCE_PREFIX): dictionaries/en.txt and
// dice/3x3.txt to 6x6.txt, the files a desktop build copies beside its executable.
constexpr auto resource_root = ":/wjs-android-data";

// The directory below the application's private storage the data is unpacked into.
constexpr auto data_directory_name = "program-data";

// The name the "executable" has in the unpacked directory. Only its directory is ever
// used: the program looks for `dictionaries/` and `dice/` beside it.
constexpr auto executable_name = "WordJewelsSolver";

[[nodiscard]] bool same_content(const QString& installed, QFile& packed) {
    QFile current{installed};
    if (!current.exists() || current.size() != packed.size() ||
        !current.open(QIODevice::ReadOnly) || !packed.open(QIODevice::ReadOnly)) {
        return false;
    }
    const bool same = current.readAll() == packed.readAll();
    packed.close();
    return same;
}

// Makes the unpacked copy match the package, and says where it is. The dictionary
// loader opens a file by path, and nothing inside an APK is a file, so the data a
// desktop installs beside its executable is carried in the program's resources and
// laid down here once per version (a file already the same is left alone, so a start
// after the first reads and writes nothing). These are the program's own read-only
// files, written to its private directory; the person's data is the store, which is
// not touched. If a file cannot be written the game's setup says which one it could
// not open.
[[nodiscard]] std::filesystem::path unpack_data() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        return {};
    }
    const QDir root{QDir{base}.filePath(QString::fromLatin1(data_directory_name))};
    const QString resource_prefix = QString::fromLatin1(resource_root);

    QDirIterator packed_files{resource_prefix, QDir::Files, QDirIterator::Subdirectories};
    while (packed_files.hasNext()) {
        const QString packed_path = packed_files.next();
        const QString target = root.filePath(QDir{resource_prefix}.relativeFilePath(packed_path));
        QFile packed{packed_path};
        if (same_content(target, packed)) {
            continue;
        }
        root.mkpath(QFileInfo{target}.path());
        QFile::remove(target);
        packed.copy(target);
    }
    return std::filesystem::path{root.filePath(QString::fromLatin1(executable_name)).toStdString()};
}

}  // namespace

std::filesystem::path executable_path() {
    // The real executable is a shared library inside the package, and nothing sits
    // beside it. The answer is where the program's data was unpacked, as if the
    // executable were there: the one use of this function is to find that data.
    static const std::filesystem::path path = unpack_data();
    return path;
}

std::filesystem::path user_data_directory() {
    // The application's private files directory, which only this application can read
    // and which Android removes with it. Backup is off in the manifest, so it stays on
    // the device. QStandardPaths asks the Java side for it; it exists already.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        return {};
    }
    return std::filesystem::path{base.toStdString()};
}

}  // namespace wjs::platform
