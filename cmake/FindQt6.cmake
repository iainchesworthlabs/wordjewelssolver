# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# FindQt6.cmake - wrapper picked up automatically by find_package(Qt6 ...)
# because cmake/ is on CMAKE_MODULE_PATH (CMake tries Module mode, i.e. a
# Find<Pkg>.cmake, before Config mode).
#
# It never replaces Qt6Config.cmake: it only widens CMAKE_PREFIX_PATH when Qt6
# isn't already discoverable - first with QT_ROOT_DIR (what the labs shell
# environment, C:\Repos\common\env.ps1, and CI export), then with the
# aqtinstall layout (C:/Qt, ~/Qt, /opt/Qt, ...) each platform fragment lists -
# and then defers to Qt's own config package for the real work. An explicit
# -DCMAKE_PREFIX_PATH=... or -DQt6_DIR=... always wins.
# ---------------------------------------------------------------------------

if(NOT "$ENV{QT_ROOT_DIR}" STREQUAL "")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT_DIR}")
endif()

find_package(Qt6 ${Qt6_FIND_VERSION} CONFIG QUIET COMPONENTS ${Qt6_FIND_COMPONENTS})

if(NOT Qt6_FOUND)
    # Where an aqtinstall-style Qt lives is a fact about the platform, so it
    # is declared by the platform's fragment (cmake/Platform.cmake).
    set(_qt6_search_roots ${WJS_PLATFORM_QT_SEARCH_ROOTS})
    set(_qt6_arch_dirs ${WJS_PLATFORM_QT_ARCH_DIRS})

    set(_qt6_candidates "")
    foreach(root IN LISTS _qt6_search_roots)
        if(NOT root OR NOT IS_DIRECTORY "${root}")
            continue()
        endif()

        file(GLOB _qt6_version_dirs LIST_DIRECTORIES true "${root}/6.*")
        foreach(version_dir IN LISTS _qt6_version_dirs)
            if(NOT IS_DIRECTORY "${version_dir}")
                continue()
            endif()
            foreach(arch IN LISTS _qt6_arch_dirs)
                if(EXISTS "${version_dir}/${arch}/lib/cmake/Qt6/Qt6Config.cmake")
                    list(APPEND _qt6_candidates "${version_dir}/${arch}")
                endif()
            endforeach()
        endforeach()

        # System-wide Qt6 installs (e.g. apt's qt6-base-dev) ship directly under root.
        if(EXISTS "${root}/lib/cmake/Qt6/Qt6Config.cmake")
            list(APPEND _qt6_candidates "${root}")
        endif()
    endforeach()

    if(_qt6_candidates)
        list(SORT _qt6_candidates COMPARE NATURAL)
        list(REVERSE _qt6_candidates)
        list(GET _qt6_candidates 0 _qt6_chosen)

        message(STATUS "FindQt6: auto-detected prebuilt Qt6 at ${_qt6_chosen} (pass -DCMAKE_PREFIX_PATH to override)")
        list(APPEND CMAKE_PREFIX_PATH "${_qt6_chosen}")

        set(_qt6_args CONFIG)
        if(Qt6_FIND_QUIETLY)
            list(APPEND _qt6_args QUIET)
        endif()
        if(Qt6_FIND_REQUIRED)
            list(APPEND _qt6_args REQUIRED)
        endif()
        if(Qt6_FIND_COMPONENTS)
            list(APPEND _qt6_args COMPONENTS ${Qt6_FIND_COMPONENTS})
        endif()

        find_package(Qt6 ${Qt6_FIND_VERSION} ${_qt6_args})

        unset(_qt6_args)
        unset(_qt6_chosen)
    endif()

    unset(_qt6_search_roots)
    unset(_qt6_arch_dirs)
    unset(_qt6_candidates)
    unset(_qt6_version_dirs)
endif()

# ---------------------------------------------------------------------------
# The type compiler's (qmltc, ENABLE_TYPE_COMPILER) generated C++ includes Qt's
# private headers, in both their forms ("QtCore/private/x.h" and "private/x.h"),
# and links the qmltc runtime. An aqtinstall-style kit ships the headers and the
# runtime on disk but only as private modules, so a module that asked for the
# type compiler is given them by hand (Qt's own developer builds load them for
# themselves).
# ---------------------------------------------------------------------------
function(wjs_enable_qmltc_private_headers target)
    find_package(Qt6QmltcPrivate 6.12 CONFIG QUIET)
    if(TARGET Qt6::QmltcPrivate)
        target_link_libraries("${target}" PRIVATE Qt6::QmltcPrivate)
    endif()

    get_filename_component(_wjs_qt_prefix "${Qt6Qml_DIR}/../../.." ABSOLUTE)
    set(_wjs_private_include_dirs "")
    foreach(_wjs_module IN ITEMS QtQml QtCore QtQuick QtGui QtQuickLayouts QtQmlModels QtQuickShapes)
        list(APPEND _wjs_private_include_dirs
            "${_wjs_qt_prefix}/include/${_wjs_module}"
            "${_wjs_qt_prefix}/include/${_wjs_module}/${Qt6Qml_VERSION}"
            "${_wjs_qt_prefix}/include/${_wjs_module}/${Qt6Qml_VERSION}/${_wjs_module}"
            # A framework kit (Qt's macOS and iOS builds) keeps the same
            # headers inside the framework, and the type compiler's code
            # includes them as "private/x_p.h".
            "${_wjs_qt_prefix}/lib/${_wjs_module}.framework/Headers"
            "${_wjs_qt_prefix}/lib/${_wjs_module}.framework/Headers/${Qt6Qml_VERSION}"
            "${_wjs_qt_prefix}/lib/${_wjs_module}.framework/Headers/${Qt6Qml_VERSION}/${_wjs_module}")
    endforeach()
    # SYSTEM: these are Qt's headers, and the target's warnings are errors; a framework
    # kit's compiler (AppleClang) would otherwise warn about them.
    target_include_directories("${target}" SYSTEM PRIVATE ${_wjs_private_include_dirs})
endfunction()
