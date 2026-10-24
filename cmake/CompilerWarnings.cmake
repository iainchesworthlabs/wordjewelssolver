# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# CompilerWarnings.cmake
#
# Defines an INTERFACE target `wjs::warnings` that turns on a strict,
# cross-compiler warning set with "warnings as errors". Link it PRIVATE-ly
# into every first-party target (library, app, tests). Third-party code (Qt,
# vcpkg packages) is pulled in as SYSTEM headers by their package configs, so
# these flags never fire on dependency code.
# ---------------------------------------------------------------------------

add_library(wjs_warnings INTERFACE)
add_library(wjs::warnings ALIAS wjs_warnings)

# MSVC's static analyzer, run as part of the compile rather than beside it. It
# is opt-in because it costs several times a normal compile; the nightly
# analysis workflow turns it on (docs/ci.md, "Nightly analysis"). The analyzer's
# warnings are C6xxx diagnostics, so they pass through /WX like every other
# warning. SARIF is what GitHub's code scanning ingests; /analyze:external- keeps
# the report to this repository's code, and CMake marks imported targets'
# headers as external so Qt and Catch2 are skipped too.
option(WJS_ENABLE_MSVC_ANALYSIS
    "Run MSVC's static analyzer (/analyze) during compilation" OFF)
if(WJS_ENABLE_MSVC_ANALYSIS)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR
            "WJS_ENABLE_MSVC_ANALYSIS needs the MSVC compiler: clang-cl has "
            "no /analyze, and clang-tidy is the analyzer for every other "
            "compiler (docs/ci.md).")
    endif()
    target_compile_options(wjs_warnings INTERFACE
        "$<$<CXX_COMPILER_ID:MSVC>:/analyze;/analyze:external-;/analyze:log:format:sarif>")
endif()

# The mandated baseline is -Wall -Wextra -Werror; the remaining flags catch the
# classes of mistake (shadowing, dubious conversions, missing overrides) that a
# generator is most likely to introduce.
set(WJS_GNU_CLANG_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wformat=2)

set(WJS_MSVC_WARNINGS
    /W4
    /WX
    /permissive-)

# clang-cl reports CXX_COMPILER_ID "Clang" (it would otherwise get
# WJS_GNU_CLANG_WARNINGS below), but its -Wall alias is bound to
# MSVC's /Wall semantics - "warning level 4 and everything disabled by
# default" - not plain clang's moderate -Wall. That silently turns on
# off-by-default groups like -Wc++98-compat, which then fail the build on
# ordinary C++23 code (scoped enums, [[nodiscard]], nested namespaces, ...).
# /W4 is what clang-cl itself recommends in place of -Wall for this reason.
set(WJS_CLANG_CL_WARNINGS
    /W4
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wformat=2)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(wjs_warnings INTERFACE ${WJS_CLANG_CL_WARNINGS})
else()
    target_compile_options(wjs_warnings INTERFACE
        "$<$<CXX_COMPILER_ID:MSVC>:${WJS_MSVC_WARNINGS}>"
        "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:${WJS_GNU_CLANG_WARNINGS}>")
endif()

# ---------------------------------------------------------------------------
# Qt's headers are third-party code, and the QML compiler's generated C++ (qmltc)
# includes Qt's private ones, which are not clean under -Wold-style-cast,
# -Wnon-virtual-dtor or -Wsign-conversion. Imported targets' include directories
# are system directories to CMake, but the Visual Studio generator does not pass
# that on to clang-cl, so the coverage preset (ClangCL) failed on qfixed_p.h and
# qquicktextinterface_p.h. This names the directories to clang-cl by hand, as
# /imsvc: a directory that is both -I and /imsvc is a system directory to it.
# Called once Qt has been found (top-level CMakeLists.txt); a no-op for every
# other compiler, whose generators do the right thing.
# ---------------------------------------------------------------------------
function(wjs_treat_qt_headers_as_system)
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
            AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
        return()
    endif()
    if(NOT Qt6_DIR)
        return()
    endif()
    # <prefix>/lib/cmake/Qt6 -> <prefix>/include, where each module has
    # Qt<Module>/, Qt<Module>/<version>/ and Qt<Module>/<version>/Qt<Module>/
    # (the last holds the private headers).
    get_filename_component(_qt_include "${Qt6_DIR}/../../../include" ABSOLUTE)
    set(_directories "${_qt_include}")
    get_directory_property(_targets IMPORTED_TARGETS)
    foreach(_target IN LISTS _targets)
        if(_target MATCHES "^Qt6::([A-Za-z0-9]+)$")
            string(REGEX REPLACE "Private$" "" _module "${CMAKE_MATCH_1}")
            foreach(_candidate IN ITEMS
                    "${_qt_include}/Qt${_module}"
                    "${_qt_include}/Qt${_module}/${Qt6_VERSION}"
                    "${_qt_include}/Qt${_module}/${Qt6_VERSION}/Qt${_module}")
                if(IS_DIRECTORY "${_candidate}")
                    list(APPEND _directories "${_candidate}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _directories)
    foreach(_directory IN LISTS _directories)
        target_compile_options(wjs_warnings INTERFACE
            "$<$<COMPILE_LANGUAGE:CXX>:SHELL:/imsvc \"${_directory}\">")
    endforeach()
endfunction()
