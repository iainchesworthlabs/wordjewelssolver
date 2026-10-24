# SPDX-License-Identifier: GPL-3.0-or-later
# Windows. Included by cmake/Platform.cmake; the contract it fills in is
# described at the top of that file.

set(WJS_PLATFORM_DISPLAY_NAME "Windows")
set(WJS_PLATFORM_DESKTOP ON)

# The application looks for its dictionary and dice beside its own executable,
# and an installed executable is in the bindir.
set(WJS_PLATFORM_INSTALL_APP_DATA_DIR "${CMAKE_INSTALL_BINDIR}")

# An installed copy carries Qt beside itself: the install-time deploy script
# in apps/app/CMakeLists.txt writes the DLLs, plugins and QML modules there.
set(WJS_PLATFORM_QT_DEPLOY_AT_INSTALL ON)

# C:/Qt and the user's installs are where the Qt online installer and
# aqtinstall put a kit. C:/Repos/common/qt is the labs' shared prebuilt tree
# (C:\Repos\common\env.ps1 sets QT_ROOT_DIR to it); FindQt6 checks that the
# directory exists, so the entry costs nothing on a machine without it.
set(WJS_PLATFORM_QT_SEARCH_ROOTS
    "C:/Qt"
    "$ENV{USERPROFILE}/Qt"
    "C:/Repos/common/qt")
set(WJS_PLATFORM_QT_ARCH_DIRS msvc2022_64 msvc2019_64)

# Qt's offscreen platform plugin draws text with the fonts in this directory, and
# finds none of its own on Windows.
set(WJS_PLATFORM_QT_HEADLESS_ENVIRONMENT "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")

# detail/ is what no platform differs in: no native camera backend yet, the portable
# board reader (docs/design/scanner.md), and the no-op haptics every desktop
# answers with (libs/platforms/interfaces/wjs/platform/haptics.hpp).
# The application's executable icon and version resource come from a Windows
# resource script (packaging/app.rc.in). The application names the template and
# icon; only this platform sets them.
set(WJS_PLATFORM_APP_RC_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/packaging/app.rc.in")
set(WJS_PLATFORM_APP_ICON "${PROJECT_SOURCE_DIR}/assets/icon/app.ico")

set(WJS_PLATFORM_SOURCES
    detail/board_reader_portable.cpp
    detail/camera_unavailable.cpp
    detail/haptics_none.cpp
    windows/console_windows.cpp
    windows/fatal_error_windows.cpp
    windows/paths_windows.cpp
    windows/window_chrome_win.cpp)

set(WJS_PLATFORM_LINK_LIBRARIES dwmapi)

# <windows.h> without the parts and macros nobody asked for (min/max, the
# rarely used APIs) and with the wide-character API as the default. Passed by
# the build, so no source file declares them.
set(WJS_PLATFORM_COMPILE_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# A graphical program starts without a console window.
function(wjs_platform_configure_app target)
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE ON)
endfunction()

# Qt's DLLs are not next to the executable or on PATH by default, so it fails
# to start (STATUS_DLL_NOT_FOUND) unless launched from a shell that already has
# Qt's bin directory on PATH. windeployqt copies what the executable needs (Qt
# DLLs, platform and style plugins) next to it after every build, so it runs
# standalone however it is launched.
#
# <qml_dir> is the directory of the program's QML sources ("" for a program
# with none): windeployqt reads the imports in it to find which QML modules the
# program needs, and without it the window fails with "module QtQuick is not
# installed". Any further arguments are more QML directories the program's
# screens use (Mode A's live view is a file of the camera variant), each passed
# as a --qmldir.
function(wjs_platform_deploy_qt_runtime target qml_dir)
    find_program(WJS_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS "${Qt6_DIR}/../../../bin")
    if(NOT WJS_WINDEPLOYQT_EXECUTABLE)
        message(WARNING
            "windeployqt was not found; ${target} will only start from a shell "
            "that has Qt's bin directory on PATH.")
        return()
    endif()

    # qoffscreen is the platform plugin that runs without a display, which the
    # checks of the Qt Quick screens need (apps/app/tests/unit/parent_ui).
    #
    # --skip-plugin-types qmltooling keeps the QML debugger plugins out of the
    # deploy directory. One of them, qmldbg_quick3dprofiler, is the GPL-3.0-only
    # Quick 3D profiler: windeployqt copies it (and Qt6Quick3DUtils, which it
    # depends on) into anything with a QML module, whether or not the program
    # links Quick 3D. Nothing here loads it, and it must not ship: see
    # docs/design/shake-prototype.md ("Deploy").
    set(_arguments --no-translations --include-plugins qoffscreen --skip-plugin-types qmltooling)
    if(NOT "${qml_dir}" STREQUAL "")
        list(APPEND _arguments --qmldir "${qml_dir}")
    endif()
    foreach(_more_qml_dir IN LISTS ARGN)
        if(NOT "${_more_qml_dir}" STREQUAL "")
            list(APPEND _arguments --qmldir "${_more_qml_dir}")
        endif()
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${WJS_WINDEPLOYQT_EXECUTABLE}" ${_arguments} "$<TARGET_FILE:${target}>"
        COMMENT "Deploying the Qt runtime beside ${target}")
endfunction()

# A ZIP needs no tool installed and is the package every Windows host can make.
# NSIS is the installer, and it is offered only when makensis is there: a
# machine without NSIS still gets a ZIP, and the configure says which it made
# rather than failing (docs/building.md, "Packaging").
function(wjs_platform_configure_packaging)
    find_program(WJS_MAKENSIS_EXECUTABLE NAMES makensis)
    if(WJS_MAKENSIS_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS")
        message(STATUS "Packaging: makensis found at ${WJS_MAKENSIS_EXECUTABLE}: ZIP and NSIS")
    else()
        message(STATUS "Packaging: makensis was not found: the package is a ZIP only")
    endif()
    set(CPACK_GENERATOR "${CPACK_GENERATOR}" PARENT_SCOPE)
endfunction()
