# SPDX-License-Identifier: GPL-3.0-or-later
# macOS. Included by cmake/Platform.cmake; the contract it fills in is
# described at the top of that file.

set(WJS_PLATFORM_DISPLAY_NAME "macOS")
set(WJS_PLATFORM_DESKTOP ON)

# The application is a bundle (below), and it looks for its dictionary and dice
# beside its own executable: inside the bundle, in Contents/MacOS, not in the
# prefix's bindir. The bundle's name is the target's output name
# (apps/app/CMakeLists.txt, OUTPUT_NAME WordJewelsSolver).
set(WJS_PLATFORM_INSTALL_APP_DATA_DIR "WordJewelsSolver.app/Contents/MacOS")

# An installed copy carries Qt inside the bundle: the install-time deploy script
# in apps/app/CMakeLists.txt writes the frameworks, plugins and QML modules there.
# No macOS host has built the application with Qt yet (docs/ci.md): the release
# workflow's macOS leg is the first, and packaging/verify_bundle.sh the check.
set(WJS_PLATFORM_QT_DEPLOY_AT_INSTALL ON)

set(WJS_PLATFORM_QT_SEARCH_ROOTS
    "$ENV{HOME}/Qt" "/opt/Qt" "/usr/local/opt/qt6" "/opt/homebrew/opt/qt6")
set(WJS_PLATFORM_QT_ARCH_DIRS macos clang_64)

# The offscreen platform plugin takes its fonts from the system's.
set(WJS_PLATFORM_QT_HEADLESS_ENVIRONMENT "")

# detail/ is what no platform differs in: no native camera backend yet, the portable
# board reader (docs/design/scanner.md), and the no-op haptics every desktop
# answers with (libs/platforms/interfaces/wjs/platform/haptics.hpp).
set(WJS_PLATFORM_SOURCES
    detail/board_reader_portable.cpp
    detail/camera_unavailable.cpp
    detail/fatal_error_stderr.cpp
    detail/haptics_none.cpp
    detail/window_chrome_none.cpp
    macos/paths_macos.cpp
    posix/console_posix.cpp)

set(WJS_PLATFORM_LINK_LIBRARIES "")
set(WJS_PLATFORM_COMPILE_DEFINITIONS "")

# Where the bundle's Info.plist template, icon and signing entitlements are
# (captured here: inside a function CMAKE_CURRENT_LIST_DIR is the caller's).
set(WJS_MACOS_PACKAGING_DIR "${CMAKE_CURRENT_LIST_DIR}/packaging")

# A graphical program on macOS is an application bundle, with its own Info.plist
# (packaging/Info.plist.in: the identifier, the version, the languages and the
# camera usage text macOS requires before it lets a program use the camera) and
# its icon (packaging/AppIcon.icns, made from assets/icon by
# tools/package/make_icns.py). CMake fills the template's ${MACOSX_BUNDLE_...}
# names in from the properties below.
function(wjs_platform_configure_app target)
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${WJS_MACOS_PACKAGING_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "WordJewelsSolver"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.iainchesworth.wordjewelssolver"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) Iain Chesworth and contributors")
    target_sources(${target} PRIVATE "${WJS_MACOS_PACKAGING_DIR}/AppIcon.icns")
    set_source_files_properties("${WJS_MACOS_PACKAGING_DIR}/AppIcon.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endfunction()

# Nothing to deploy yet: the application bundle gets macdeployqt with the
# packaging work (docs/roadmap.md).
function(wjs_platform_deploy_qt_runtime target qml_dir)
endfunction()

# A disk image is the native package, and it needs no tool CPack does not
# already have (hdiutil is part of the system). A ZIP is offered as well, by
# cmake/Packaging.cmake, so a package can be unpacked without mounting it.
function(wjs_platform_configure_packaging)
    list(APPEND CPACK_GENERATOR "DragNDrop")
    message(STATUS "Packaging: ${CPACK_GENERATOR}")
    set(CPACK_GENERATOR "${CPACK_GENERATOR}" PARENT_SCOPE)
endfunction()
