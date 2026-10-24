# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# What a package would contain, checked from the configure's own output: the
# CPack configuration and the install rules, before anything is built or
# packaged (docs/building.md, "Packaging").
#
#   cmake -DBUILD_DIR=<build tree> -DVERSION=<project version>
#         -DEXPECT_CLI=ON|OFF -DEXPECT_APP=ON|OFF
#         -DEXPECT_QT_DEPLOY=ON|OFF -DNOTICES=<NOTICES.txt> [-DPRERELEASE=<rc.1>] [-DEXPECT_TRACY=ON|OFF]
#         -P tests/package/check_package.cmake
#
# It is deliberately cheap and runs in every ctest: CPackConfig.cmake must name
# this project, its licence and a ZIP, and the generated cmake_install.cmake
# scripts must name both programs, the dictionary and the dice, the notices and
# the licence text. A dropped install() rule is then a failed test on the change
# that dropped it, not a package missing a file at release time. The archive
# itself is checked by tools/package/check_package.ps1, which also runs the
# packaged programs; that one needs a build and a package, so it is a script and
# a CI step rather than a unit test.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

foreach(_required IN ITEMS BUILD_DIR VERSION NOTICES)
    if("${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Pass -D${_required}=<value>.")
    endif()
endforeach()

set(_failures "")

# --- 1. CPack's configuration -------------------------------------------------
set(_cpack "${BUILD_DIR}/CPackConfig.cmake")
if(NOT EXISTS "${_cpack}")
    message(FATAL_ERROR
        "${_cpack} does not exist: cmake/Packaging.cmake was not included from the "
        "top-level CMakeLists.txt, or this build tree predates it (reconfigure).")
endif()

# CPackConfig.cmake is a plain list of set() calls, so it is read by including
# it: the values are then checked as CPack will see them, not as text that may
# be quoted differently.
include("${_cpack}")

function(_expect variable value)
    if(NOT "${${variable}}" STREQUAL "${value}")
        message(SEND_ERROR
            "CPack's ${variable} is '${${variable}}', expected '${value}'")
    endif()
endfunction()

_expect(CPACK_PACKAGE_NAME "WordJewelsSolver")
# The version a package carries is the project's, plus the prerelease
# identifier when one was configured (WJS_PACKAGE_PRERELEASE).
if(PRERELEASE)
    _expect(CPACK_PACKAGE_VERSION "${VERSION}-${PRERELEASE}")
else()
    _expect(CPACK_PACKAGE_VERSION "${VERSION}")
endif()
_expect(CPACK_PACKAGE_VENDOR "Iain Chesworth")
_expect(CPACK_PACKAGE_CONTACT "Iain Chesworth")
_expect(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/iainchesworthlabs/wordjewelssolver")
_expect(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Finds every word on a Boggle board")

# CPack refuses to package a Tracy build unless a development package is asked
# for by name (cmake/PackagePreflight.cmake): the script has to be wired in, and
# CPack has to be told whether this build has Tracy.
if(NOT CPACK_PRE_BUILD_SCRIPTS MATCHES "PackagePreflight\.cmake$" OR NOT EXISTS "${CPACK_PRE_BUILD_SCRIPTS}")
    list(APPEND _failures
        "CPack does not run cmake/PackagePreflight.cmake before packaging "
        "(CPACK_PRE_BUILD_SCRIPTS is '${CPACK_PRE_BUILD_SCRIPTS}')")
endif()
if(DEFINED EXPECT_TRACY)
    if(EXPECT_TRACY)
        set(_tracy_on ON)
    else()
        set(_tracy_on OFF)
    endif()
    if(NOT "${CPACK_WJS_TRACY}" STREQUAL "${_tracy_on}")
        list(APPEND _failures
            "CPack thinks Tracy is '${CPACK_WJS_TRACY}' but the build has it ${_tracy_on}")
    endif()
endif()
if(CPACK_WJS_ALLOW_DEV_PACKAGE)
    list(APPEND _failures
        "CPACK_WJS_ALLOW_DEV_PACKAGE is on in CPackConfig.cmake; a development "
        "package has to be asked for on the cpack command line, not configured")
endif()

string(FIND "${CPACK_GENERATOR}" "ZIP" _zip)
if(_zip EQUAL -1)
    list(APPEND _failures "CPack names no ZIP generator: ${CPACK_GENERATOR}")
endif()

if(NOT EXISTS "${CPACK_RESOURCE_FILE_LICENSE}")
    list(APPEND _failures
        "CPack's licence file does not exist: ${CPACK_RESOURCE_FILE_LICENSE}")
endif()

string(FIND "${CPACK_PACKAGE_DIRECTORY}" "packages" _packages)
if(_packages EQUAL -1)
    list(APPEND _failures
        "CPack's package directory is '${CPACK_PACKAGE_DIRECTORY}', not a packages/ directory")
endif()

# --- 2. The install rules the package is made of ------------------------------
file(GLOB_RECURSE _install_scripts "${BUILD_DIR}/cmake_install.cmake")
if(NOT _install_scripts)
    list(APPEND _failures "no cmake_install.cmake under ${BUILD_DIR}")
endif()

set(_install_text "")
foreach(_script IN LISTS _install_scripts)
    # A package already unpacked in the tree holds a copy of every install
    # script; the rules to check are the tree's own, not an old package's.
    if(_script MATCHES "/packages/" OR _script MATCHES "/CMakeFiles/")
        continue()
    endif()
    file(READ "${_script}" _one)
    string(APPEND _install_text "${_one}\n")
endforeach()

# The programs this build makes, and the files they read beside themselves.
set(_required_entries)
if(EXPECT_CLI)
    list(APPEND _required_entries "wjs-cli")
endif()
if(EXPECT_APP)
    list(APPEND _required_entries
        "WordJewelsSolver"
        "dice/3x3.txt"
        "dice/4x4.txt"
        "dice/5x5.txt"
        "dice/6x6.txt")
endif()
if(EXPECT_CLI OR EXPECT_APP)
    list(APPEND _required_entries "dictionaries/en.txt")
endif()
list(APPEND _required_entries "NOTICES.txt" "LICENSE.txt")

foreach(_entry IN LISTS _required_entries)
    string(FIND "${_install_text}" "${_entry}" _found)
    if(_found EQUAL -1)
        list(APPEND _failures "no install rule names ${_entry}")
    endif()
endforeach()

# The application's Qt runtime and QML modules come from Qt's own deploy script,
# which install()s it; that script's file name is generated, so the rule is
# recognised by its name's one stable word. Only a build that has the
# application, on a platform that deploys Qt at install time, has the rule.
if(EXPECT_APP AND EXPECT_QT_DEPLOY)
    string(FIND "${_install_text}" "deploy" _deploy)
    if(_deploy EQUAL -1)
        list(APPEND _failures
            "no install rule runs Qt's deploy script, so an installed application "
            "would carry no Qt runtime")
    endif()
endif()

# Mode A's live view imports Qt Multimedia. A package of a build with the module
# has to carry qml/QtMultimedia and the Multimedia plugins, which Qt's
# install-time deploy copies only when the application's own QML import scan
# names the module. The scan's input list is written at configure time, so this
# holds the camera's deployment hint without packaging anything
# (apps/app/src/camera/CameraImport.qml says why the hint exists); the archive check
# (`tools/package/check_package.ps1`) then requires both directories in the
# package itself.
if(EXPECT_MULTIMEDIA)
    set(_import_scan "${BUILD_DIR}/apps/app/.qt/qml_imports/WordJewelsSolver_build.rsp")
    if(NOT EXISTS "${_import_scan}")
        list(APPEND _failures
            "the application's QML import scan input is missing: ${_import_scan}")
    else()
        file(READ "${_import_scan}" _import_scan_text)
        string(FIND "${_import_scan_text}" "wjs_camera_import" _camera_import)
        if(_camera_import EQUAL -1)
            list(APPEND _failures
                "the application's QML import scan does not read the camera's "
                "QtMultimedia hint (apps/app/src/camera/CameraImport.qml), so an "
                "installed camera's live view would find no qml/QtMultimedia")
        endif()
    endif()
endif()

# A package with the profiler in it carries Tracy's licence, and one without it
# does not claim to (notices/fragments/tracy.txt).
if(DEFINED EXPECT_TRACY AND EXISTS "${NOTICES}")
    file(READ "${NOTICES}" _notice_text)
    string(FIND "${_notice_text}" "Tracy Profiler (development packages only)" _tracy_section)
    if(EXPECT_TRACY AND _tracy_section EQUAL -1)
        list(APPEND _failures "this build has Tracy in it but ${NOTICES} has no Tracy section")
    elseif(NOT EXPECT_TRACY AND NOT _tracy_section EQUAL -1)
        list(APPEND _failures "this build has no Tracy but ${NOTICES} has a Tracy section")
    endif()
endif()

# The notice the package installs is the one this build wrote, not a stale one.
string(FIND "${_install_text}" "${NOTICES}" _notices_path)
if(_notices_path EQUAL -1)
    list(APPEND _failures "no install rule names ${NOTICES}")
endif()

if(_failures)
    string(REPLACE ";" "\n  " _report "${_failures}")
    message(FATAL_ERROR "The package configuration:\n  ${_report}\n")
endif()

message(STATUS
    "packaging: ${CPACK_PACKAGE_NAME} ${CPACK_PACKAGE_VERSION}, generators ${CPACK_GENERATOR}; "
    "the install rules checked (application: ${EXPECT_APP}, command line: ${EXPECT_CLI})")
