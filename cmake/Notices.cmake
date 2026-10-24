# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# Notices.cmake - the third-party notices a package carries, assembled from
# fragment files at configure time.
#
#   wjs_generate_notices(<out-file>
#       FRAGMENT_DIR <dir>...   directories holding <fragment>.txt files,
#                               searched in the order given
#       FRAGMENTS <name>...     which fragments, in the order they appear
#       TOKENS <KEY=value>...   every {{KEY}} in the fragments becomes value
#       FILES <NAME=path>...    every {{FILE:NAME}} becomes that file's text)
#
#   wjs_write_notices()         this project's NOTICES.txt, for this build
#   wjs_add_notices_resource(<target>)   :/notices/NOTICES.txt on a program
#   wjs_install_notices()       NOTICES.txt and LICENSE.txt beside the programs
#
# The one file a package installs and the application embeds as
# :/notices/NOTICES.txt (the About screen's Licences view) is written once, from
# the versions CMake already knows, rather than kept by hand per platform
# (docs/licensing.md, "Notices").
#
# The fragments are plain text in notices/fragments/, one section each: the
# header and the project's own licence, Qt and Quick 3D, the word list, the
# translations, the assets, and the tools that are only used to build and test.
# A fragment that describes what a PACKAGE contains is included only when that
# package is built - Qt and the translations only with the application - so a
# command-line package does not claim to carry Qt.
#
# Substitution is string(REPLACE) on {{TOKEN}} markers, deliberately not
# configure_file(@ONLY): licence texts must stay byte-exact, and copyright lines
# carry @ in e-mail addresses. Every expansion of a variable that holds licence
# text is quoted, because the texts contain semicolons.
#
# Failure is loud, at configure. A fragment the caller names that does not
# exist, a {{FILE:NAME}} whose file is missing, and any {{TOKEN}} still in the
# output each stop the configure with a message naming the culprit: a package
# whose notice still reads "{{QT_VERSION}}" is worse than no notice.
#
# Line endings are normalised to LF whatever the checkout's autocrlf did, so
# the generated file is byte-identical on every platform that builds it, and
# every fragment and licence file read is a configure dependency, so editing one
# re-runs the generator. The output is rewritten only when its content changes,
# so an unchanged configure does not touch the resource's mtime.
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

include(GNUInstallDirs)

function(wjs_generate_notices out)
    cmake_parse_arguments(PARSE_ARGV 1 N "" "" "FRAGMENT_DIR;FRAGMENTS;TOKENS;FILES")
    if(N_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "wjs_generate_notices: unexpected arguments: ${N_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT N_FRAGMENT_DIR)
        message(FATAL_ERROR "wjs_generate_notices: FRAGMENT_DIR is required")
    endif()
    if(NOT N_FRAGMENTS)
        message(FATAL_ERROR "wjs_generate_notices: FRAGMENTS names no fragment for ${out}")
    endif()

    # 1. The fragments, in order, one blank line between them. Each name is
    #    resolved against the FRAGMENT_DIR list in order, first match wins.
    set(text "")
    foreach(fragment IN LISTS N_FRAGMENTS)
        set(path "")
        foreach(dir IN LISTS N_FRAGMENT_DIR)
            if(EXISTS "${dir}/${fragment}.txt")
                set(path "${dir}/${fragment}.txt")
                break()
            endif()
        endforeach()
        if(NOT path)
            string(REPLACE ";" ", " searched "${N_FRAGMENT_DIR}")
            message(FATAL_ERROR
                "notices: the fragment list names '${fragment}' but no ${fragment}.txt "
                "exists in any of these directories: ${searched}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
        file(READ "${path}" chunk)
        string(REPLACE "\r\n" "\n" chunk "${chunk}")
        string(REGEX REPLACE "\n+$" "" chunk "${chunk}")
        string(APPEND text "${chunk}\n\n")
    endforeach()

    # 2. {{FILE:NAME}} -> the named file's text. Read only when a fragment asks
    #    for it, so a file only one build's fragments use is not required by
    #    every build.
    foreach(pair IN LISTS N_FILES)
        string(FIND "${pair}" "=" eq)
        if(eq EQUAL -1)
            message(FATAL_ERROR "wjs_generate_notices: FILES entry '${pair}' is not NAME=path")
        endif()
        string(SUBSTRING "${pair}" 0 ${eq} name)
        math(EXPR after "${eq} + 1")
        string(SUBSTRING "${pair}" ${after} -1 path)
        string(CONCAT marker "{{FILE:" "${name}" "}}")
        string(FIND "${text}" "${marker}" at)
        if(at EQUAL -1)
            continue()
        endif()
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR
                "notices: a fragment uses ${marker} but its file ${path} does not exist")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
        file(READ "${path}" content)
        string(REPLACE "\r\n" "\n" content "${content}")
        string(REGEX REPLACE "\n+$" "" content "${content}")
        string(REPLACE "${marker}" "${content}" text "${text}")
    endforeach()

    # 3. {{KEY}} -> value.
    foreach(pair IN LISTS N_TOKENS)
        string(FIND "${pair}" "=" eq)
        if(eq EQUAL -1)
            message(FATAL_ERROR "wjs_generate_notices: TOKENS entry '${pair}' is not KEY=value")
        endif()
        string(SUBSTRING "${pair}" 0 ${eq} key)
        math(EXPR after "${eq} + 1")
        string(SUBSTRING "${pair}" ${after} -1 value)
        string(CONCAT marker "{{" "${key}" "}}")
        string(REPLACE "${marker}" "${value}" text "${text}")
    endforeach()

    # 4. Nothing may be left unfilled.
    string(REGEX MATCH "{{[A-Za-z0-9_:.-]+}}" leftover "${text}")
    if(leftover)
        message(FATAL_ERROR
            "notices: ${out} would still contain the token ${leftover}: no TOKENS or FILES "
            "value was supplied for it")
    endif()
    string(REGEX REPLACE "\n+$" "\n" text "${text}")

    # 5. Write, only on change.
    get_filename_component(dir "${out}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    set(previous "")
    if(EXISTS "${out}")
        file(READ "${out}" previous)
    endif()
    if(NOT previous STREQUAL text)
        file(WRITE "${out}" "${text}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# This project's notice, for this build. A section describes what a package
# contains, so Qt and the assets are named only when the application is built
# and wjs-cli only when the command-line program is. Qt's version and series are
# written into it, so Qt is found by the top-level CMakeLists.txt before this
# runs.
# ---------------------------------------------------------------------------
function(wjs_write_notices)
    set(fragments header project)
    if(WJS_BUILD_CLI)
        list(APPEND fragments cli)
    endif()
    set(qt_version "")
    set(qt_series "")
    if(WJS_BUILD_APP)
        list(APPEND fragments application qt qt-quick3d)
        set(qt_version "${Qt6_VERSION}")
        string(REGEX MATCH "^[0-9]+\\.[0-9]+" qt_series "${Qt6_VERSION}")
        if(NOT qt_version OR NOT qt_series)
            message(FATAL_ERROR
                "notices: WJS_BUILD_APP is ON but the Qt version is '${Qt6_VERSION}', so "
                "the Qt section would name no version. The top-level CMakeLists.txt calls "
                "find_package(Qt6) before this file for exactly this reason.")
        endif()
    endif()
    list(APPEND fragments enable translations)
    if(WJS_BUILD_APP)
        list(APPEND fragments assets fonts)
    endif()
    list(APPEND fragments catch2)
    # A build with the Tracy profiler in it is a development build, and its package
    # (cmake/PackagePreflight.cmake) says so and carries Tracy's licence. A release
    # build never has this section.
    if(WJS_ENABLE_TRACY)
        list(APPEND fragments tracy)
    endif()

    set(out "${CMAKE_BINARY_DIR}/notices/NOTICES.txt")
    wjs_generate_notices("${out}"
        FRAGMENT_DIR "${PROJECT_SOURCE_DIR}/notices/fragments"
        FRAGMENTS ${fragments}
        TOKENS
            "VERSION=${PROJECT_VERSION}"
            "PLATFORM=${WJS_PLATFORM_DISPLAY_NAME}"
            "LOCATION=${CMAKE_INSTALL_BINDIR}/NOTICES.txt"
            "QT_VERSION=${qt_version}"
            "QT_SERIES=${qt_series}"
        FILES
            "LGPL3=${PROJECT_SOURCE_DIR}/notices/licences/LGPL-3.0.txt")

    set(WJS_NOTICES_FILE "${out}" PARENT_SCOPE)
    message(STATUS "Notices: ${out} (${fragments})")
endfunction()

# ---------------------------------------------------------------------------
# :/notices/NOTICES.txt on a program, for the About screen's Licences view.
# Added to the executable targets that need it, not to a static library: a
# resource in a static library is linked out unless something refers to it.
# ---------------------------------------------------------------------------
function(wjs_add_notices_resource target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "wjs_add_notices_resource: no target ${target}")
    endif()
    if(NOT WJS_NOTICES_FILE)
        message(FATAL_ERROR
            "wjs_add_notices_resource: WJS_NOTICES_FILE is unset; call wjs_write_notices() "
            "from the top-level CMakeLists.txt before the targets are added.")
    endif()
    qt_add_resources(${target} wjs_notices
        PREFIX "/notices"
        BASE "${CMAKE_BINARY_DIR}/notices"
        FILES "${WJS_NOTICES_FILE}")
endfunction()

# ---------------------------------------------------------------------------
# Where the notice and the licence text land: beside the programs, in
# CMAKE_INSTALL_BINDIR, which is where a person who unpacks the archive looks
# and where the application's own licences view is the embedded copy. On a
# platform whose application keeps its data inside a bundle, the same two files
# go beside that program too, so a notice is never in only one of the places a
# program lives.
# ---------------------------------------------------------------------------
function(wjs_install_notices)
    if(NOT WJS_NOTICES_FILE)
        message(FATAL_ERROR
            "wjs_install_notices: WJS_NOTICES_FILE is unset; call wjs_write_notices() "
            "from the top-level CMakeLists.txt first.")
    endif()
    install(FILES "${WJS_NOTICES_FILE}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME "LICENSE.txt")
    if(WJS_BUILD_APP AND NOT WJS_PLATFORM_INSTALL_APP_DATA_DIR STREQUAL CMAKE_INSTALL_BINDIR)
        install(FILES "${WJS_NOTICES_FILE}" DESTINATION "${WJS_PLATFORM_INSTALL_APP_DATA_DIR}")
        install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
            DESTINATION "${WJS_PLATFORM_INSTALL_APP_DATA_DIR}" RENAME "LICENSE.txt")
    endif()
endfunction()
