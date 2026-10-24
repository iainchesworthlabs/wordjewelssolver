# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# PlatformContract.cmake - what a platform fragment
# (libs/platforms/<name>/platform.cmake) has to declare, and the check that it
# did. The contract itself is described at the top of cmake/Platform.cmake.
#
# Shared by the selector (which checks the one fragment it includes, at
# configure time, so a platform that forgets part of the contract fails by
# name) and by tests/platform/check_fragments.cmake (which checks every
# fragment in the tree on every host, so the platforms a leg does not build
# are still held to it).
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

set(WJS_PLATFORM_CONTRACT_VARIABLES
    WJS_PLATFORM_DISPLAY_NAME
    WJS_PLATFORM_DESKTOP
    WJS_PLATFORM_QT_SEARCH_ROOTS
    WJS_PLATFORM_QT_ARCH_DIRS
    WJS_PLATFORM_QT_HEADLESS_ENVIRONMENT
    WJS_PLATFORM_INSTALL_APP_DATA_DIR
    WJS_PLATFORM_QT_DEPLOY_AT_INSTALL
    WJS_PLATFORM_SOURCES
    WJS_PLATFORM_LINK_LIBRARIES
    WJS_PLATFORM_COMPILE_DEFINITIONS)

set(WJS_PLATFORM_CONTRACT_HOOKS
    wjs_platform_configure_app
    wjs_platform_deploy_qt_runtime
    wjs_platform_configure_packaging)

# Checks the fragment that was just included (in the caller's scope):
#   - every contract variable is defined (an empty string is a definition);
#   - every hook exists;
#   - WJS_PLATFORM_DESKTOP is ON or OFF;
#   - every file the fragment compiles exists under <platforms_dir>.
# `fragment` only names it in the message.
function(wjs_platform_check_contract fragment platforms_dir)
    foreach(_variable IN LISTS WJS_PLATFORM_CONTRACT_VARIABLES)
        if(NOT DEFINED ${_variable})
            message(FATAL_ERROR
                "${fragment} does not set ${_variable}. Set it to an empty "
                "string if the platform has nothing to say (the contract is "
                "described at the top of cmake/Platform.cmake).")
        endif()
    endforeach()

    foreach(_hook IN LISTS WJS_PLATFORM_CONTRACT_HOOKS)
        if(NOT COMMAND ${_hook})
            message(FATAL_ERROR
                "${fragment} does not define ${_hook}(). Define it as a "
                "no-op if the platform has nothing to do.")
        endif()
    endforeach()

    if(NOT WJS_PLATFORM_DESKTOP STREQUAL "ON" AND NOT WJS_PLATFORM_DESKTOP STREQUAL "OFF")
        message(FATAL_ERROR
            "${fragment}: WJS_PLATFORM_DESKTOP must be ON or OFF, not "
            "'${WJS_PLATFORM_DESKTOP}'.")
    endif()

    # An empty one would install the application's dictionary into the install
    # prefix itself. The fragments derive it from CMAKE_INSTALL_BINDIR, which
    # cmake/Platform.cmake has included GNUInstallDirs for before reading the
    # fragment; this catches that include going missing.
    if(NOT WJS_PLATFORM_INSTALL_APP_DATA_DIR)
        message(FATAL_ERROR
            "${fragment}: WJS_PLATFORM_INSTALL_APP_DATA_DIR is empty. It is the "
            "directory the application's data is installed into, relative to the "
            "install prefix (usually CMAKE_INSTALL_BINDIR).")
    endif()

    if(NOT WJS_PLATFORM_QT_DEPLOY_AT_INSTALL STREQUAL "ON" AND
       NOT WJS_PLATFORM_QT_DEPLOY_AT_INSTALL STREQUAL "OFF")
        message(FATAL_ERROR
            "${fragment}: WJS_PLATFORM_QT_DEPLOY_AT_INSTALL must be ON or OFF, "
            "not '${WJS_PLATFORM_QT_DEPLOY_AT_INSTALL}'.")
    endif()

    foreach(_file IN LISTS WJS_PLATFORM_SOURCES)
        if(NOT EXISTS "${platforms_dir}/${_file}")
            message(FATAL_ERROR
                "${fragment} lists ${_file}, which does not exist under "
                "${platforms_dir}.")
        endif()
    endforeach()

    # Every interface header under interfaces/wjs/platform must be implemented
    # by the fragment's own sources: a platform that compiles no definition of
    # vibrate(), say, links only by accident on a host where another target
    # happens to provide one. There is no way to see a C++ definition from
    # CMake, so the rule is that one of the fragment's sources includes the
    # header (every implementation does, as the compiler would demand of it).
    # It finds a new interface header without an implementation on every
    # platform, and the haptics interface added with no platform backend at
    # once.
    file(GLOB _interface_headers "${platforms_dir}/interfaces/wjs/platform/*.hpp")
    foreach(_header IN LISTS _interface_headers)
        get_filename_component(_header_name "${_header}" NAME)
        set(_implemented FALSE)
        foreach(_source IN LISTS WJS_PLATFORM_SOURCES)
            if(_implemented)
                break()
            endif()
            file(READ "${platforms_dir}/${_source}" _source_text)
            string(FIND "${_source_text}" "#include \"wjs/platform/${_header_name}\"" _include_at)
            if(NOT _include_at EQUAL -1)
                set(_implemented TRUE)
            endif()
        endforeach()
        if(NOT _implemented)
            message(FATAL_ERROR
                "${fragment} names no source that includes wjs/platform/${_header_name}. "
                "Every interface in libs/platforms/interfaces/wjs/platform/ needs one "
                "implementation per platform (add one, or the shared no-op under "
                "libs/platforms/detail/ and name it in the fragment).")
        endif()
    endforeach()
endfunction()
