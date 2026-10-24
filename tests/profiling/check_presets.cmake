# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# Tracy is on by default in Debug builds, and never in anything that ships.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/profiling/check_presets.cmake
#
# A Tracy client opens a TCP listener (port 8086, on every interface) and
# answers UDP discovery broadcasts, independently of anything the application
# does (docs/profiling.md, "The profiler listens on the network"). So a build
# with it in is a development build and is never packaged. A Debug build is a
# development build by definition, so the policy is: Debug has Tracy ON by
# default (a regular build can be connected to by the profiler with no option),
# everything a package is made from has it OFF unless a person turns it on by
# hand. This is the check that the policy holds, and it runs in every ctest,
# which is every CI leg:
#
#   1. Resolving `inherits` the way CMake does, WJS_ENABLE_TRACY is ON for a
#      configure preset exactly when the preset is a debug or profiling one
#      (the hidden `debug` / `profiling` presets, or named *-debug /
#      *-profiling). The *-release and *-coverage presets, which are what a
#      package and a coverage report are made from, resolve to OFF and are
#      named in the failure if they do not.
#   2. The option's default, declared in the top-level CMakeLists.txt before
#      project(), is ON for a Debug build type and OFF for every other build
#      type and for multi-config generators, and cmake/Tracy.cmake declares
#      no second default of its own.
#   3. Tracy is a vcpkg manifest FEATURE (`profiling`) and not a base
#      dependency, and the feature is not a default feature.
#   4. The top-level CMakeLists.txt adds that feature only when the option is on.
#   5. No CI workflow sets the option by hand: the presets and the default
#      decide, so a leg that quietly overrides either is a failure.
#
# It runs on whatever host runs the tests: only the presets' JSON is read.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

set(_failures "")
function(_fail text)
    set(_failures "${_failures}\n  ${text}" PARENT_SCOPE)
endfunction()

# ---- 1. the presets ----------------------------------------------------------

file(READ "${REPO_ROOT}/CMakePresets.json" _presets)
string(JSON _count LENGTH "${_presets}" configurePresets)
if(_count EQUAL 0)
    message(FATAL_ERROR "CMakePresets.json has no configure presets.")
endif()

set(_names "")
math(EXPR _last "${_count} - 1")
foreach(_i RANGE 0 ${_last})
    string(JSON _name GET "${_presets}" configurePresets ${_i} name)
    list(APPEND _names "${_name}")

    # What the preset itself sets (empty: it says nothing about Tracy).
    set(_value "")
    string(JSON _type ERROR_VARIABLE _error TYPE "${_presets}" configurePresets ${_i}
        cacheVariables WJS_ENABLE_TRACY)
    if(_error STREQUAL "NOTFOUND")
        if(_type STREQUAL "OBJECT")
            string(JSON _value GET "${_presets}" configurePresets ${_i}
                cacheVariables WJS_ENABLE_TRACY value)
        else()
            string(JSON _value GET "${_presets}" configurePresets ${_i}
                cacheVariables WJS_ENABLE_TRACY)
        endif()
    endif()
    set("_VALUE_${_name}" "${_value}")

    # What it inherits from, in order (a string or an array of strings).
    set(_parents "")
    string(JSON _type ERROR_VARIABLE _error TYPE "${_presets}" configurePresets ${_i} inherits)
    if(_error STREQUAL "NOTFOUND")
        if(_type STREQUAL "ARRAY")
            string(JSON _n LENGTH "${_presets}" configurePresets ${_i} inherits)
            if(_n GREATER 0)
                math(EXPR _m "${_n} - 1")
                foreach(_j RANGE 0 ${_m})
                    string(JSON _parent GET "${_presets}" configurePresets ${_i} inherits ${_j})
                    list(APPEND _parents "${_parent}")
                endforeach()
            endif()
        else()
            string(JSON _parent GET "${_presets}" configurePresets ${_i} inherits)
            list(APPEND _parents "${_parent}")
        endif()
    endif()
    set("_INHERITS_${_name}" "${_parents}")
endforeach()

# The value a preset ends up with: its own, else its first parent that has one
# (CMake prefers the earlier preset in `inherits`).
function(_effective name result)
    if(NOT "${_VALUE_${name}}" STREQUAL "")
        set(${result} "${_VALUE_${name}}" PARENT_SCOPE)
        return()
    endif()
    foreach(_parent IN LISTS _INHERITS_${name})
        _effective("${_parent}" _inherited)
        if(NOT "${_inherited}" STREQUAL "")
            set(${result} "${_inherited}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${result} "" PARENT_SCOPE)
endfunction()

set(_instrumented_presets "")
foreach(_name IN LISTS _names)
    _effective("${_name}" _value)
    if(_value MATCHES "^(ON|TRUE|YES|Y|1)$" OR _value MATCHES "^[1-9][0-9]*$")
        set(_on TRUE)
    else()
        set(_on FALSE)
    endif()

    set(_is_debug_name FALSE)
    if(_name MATCHES "-debug$" OR _name STREQUAL "debug")
        set(_is_debug_name TRUE)
    endif()
    set(_is_profiling_name FALSE)
    if(_name MATCHES "-profiling$" OR _name STREQUAL "profiling")
        set(_is_profiling_name TRUE)
    endif()

    # ON is allowed exactly for the debug and profiling presets.
    if(_on AND NOT (_is_debug_name OR _is_profiling_name))
        _fail("configure preset '${_name}' turns WJS_ENABLE_TRACY on; only the *-debug and *-profiling presets may")
    endif()
    # The policy itself: a debug preset IS instrumented.
    if(_is_debug_name AND NOT _on)
        _fail("the debug preset '${_name}' does not resolve WJS_ENABLE_TRACY to ON; a Debug build is instrumented by default")
    endif()
    # Nothing a package or a coverage report is made from may be.
    if(_name MATCHES "release" AND _on)
        _fail("the release preset '${_name}' has Tracy in it; a package is built from it")
    endif()
    if((_name MATCHES "coverage" OR _name STREQUAL "coverage") AND _on)
        _fail("the coverage preset '${_name}' has Tracy in it; the two instrumentations corrupt each other's numbers")
    endif()
    if(_on)
        list(APPEND _instrumented_presets "${_name}")
    endif()
endforeach()

if(NOT _instrumented_presets)
    _fail("no preset turns WJS_ENABLE_TRACY on, so an instrumented build cannot be made from a preset")
endif()

# ---- 2. the option's default -------------------------------------------------

file(READ "${REPO_ROOT}/CMakeLists.txt" _root_cmake)
if(NOT _root_cmake MATCHES "option\\(WJS_ENABLE_TRACY")
    _fail("the top-level CMakeLists.txt does not declare option(WJS_ENABLE_TRACY ...); the manifest feature must be chosen before project()")
endif()
if(NOT _root_cmake MATCHES "if\\(CMAKE_CONFIGURATION_TYPES\\)[\r\n \t]+set\\(_wjs_tracy_default OFF\\)")
    _fail("the top-level CMakeLists.txt does not default WJS_ENABLE_TRACY to OFF for multi-config generators")
endif()
if(NOT _root_cmake MATCHES "elseif\\(CMAKE_BUILD_TYPE STREQUAL \"Debug\"\\)[\r\n \t]+set\\(_wjs_tracy_default ON\\)")
    _fail("the top-level CMakeLists.txt does not default WJS_ENABLE_TRACY to ON for a Debug build type")
endif()

file(READ "${REPO_ROOT}/cmake/Tracy.cmake" _tracy_cmake)
if(_tracy_cmake MATCHES "option\\(WJS_ENABLE_TRACY")
    _fail("cmake/Tracy.cmake declares a second default for WJS_ENABLE_TRACY; the top-level CMakeLists.txt is its one declaration")
endif()

# ---- 3. the manifest ---------------------------------------------------------

file(READ "${REPO_ROOT}/vcpkg.json" _manifest)
string(JSON _deps LENGTH "${_manifest}" dependencies)
if(_deps GREATER 0)
    math(EXPR _last "${_deps} - 1")
    foreach(_i RANGE 0 ${_last})
        string(JSON _type TYPE "${_manifest}" dependencies ${_i})
        if(_type STREQUAL "OBJECT")
            string(JSON _dep GET "${_manifest}" dependencies ${_i} name)
        else()
            string(JSON _dep GET "${_manifest}" dependencies ${_i})
        endif()
        if(_dep STREQUAL "tracy")
            _fail("vcpkg.json: tracy is a base dependency; it belongs only to the 'profiling' feature")
        endif()
    endforeach()
endif()

string(JSON _defaults ERROR_VARIABLE _error GET "${_manifest}" default-features)
if(_error STREQUAL "NOTFOUND")
    string(JSON _n LENGTH "${_manifest}" default-features)
    if(_n GREATER 0)
        math(EXPR _last "${_n} - 1")
        foreach(_i RANGE 0 ${_last})
            string(JSON _feature GET "${_manifest}" default-features ${_i})
            if(_feature STREQUAL "profiling")
                _fail("vcpkg.json: 'profiling' is a default feature, so every build installs Tracy")
            endif()
        endforeach()
    endif()
endif()

string(JSON _type ERROR_VARIABLE _error TYPE "${_manifest}" features profiling)
if(NOT _error STREQUAL "NOTFOUND")
    _fail("vcpkg.json has no 'profiling' feature for the option to select")
endif()

# The Tracy version is pinned in the manifest, so a baseline bump cannot move
# it silently: the tools and the GUI must match the client's wire protocol
# (docs/profiling.md).
string(JSON _n ERROR_VARIABLE _error LENGTH "${_manifest}" overrides)
if(NOT _error STREQUAL "NOTFOUND")
    _fail("vcpkg.json has no 'overrides' entry pinning the Tracy version")
endif()
set(_tracy_pin 0)
if(_n GREATER 0)
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE 0 ${_last})
        string(JSON _name GET "${_manifest}" overrides ${_i} name)
        if(_name STREQUAL "tracy")
            set(_tracy_pin 1)
            string(JSON _version GET "${_manifest}" overrides ${_i} version)
            string(JSON _port ERROR_VARIABLE _perr GET "${_manifest}" overrides ${_i} port-version)
            if(NOT _version STREQUAL "0.14.1")
                _fail("vcpkg.json pins tracy to ${_version}; the project pins 0.14.1")
            endif()
            if(NOT _perr STREQUAL "NOTFOUND" OR NOT _port EQUAL 1)
                _fail("vcpkg.json pins tracy without port-version 1")
            endif()
        endif()
    endforeach()
endif()
if(NOT _tracy_pin)
    _fail("vcpkg.json 'overrides' does not pin tracy; a baseline bump would move the profiler's version")
endif()

# ---- 4. the one place the feature is requested --------------------------------

file(READ "${REPO_ROOT}/CMakeLists.txt" _root)
string(REGEX MATCHALL "[^\n]*\"profiling\"[^\n]*" _mentions "${_root}")
if(NOT _root MATCHES "if\\(WJS_ENABLE_TRACY\\)[ \t\r\n]*list\\(APPEND VCPKG_MANIFEST_FEATURES \"profiling\"\\)")
    _fail("CMakeLists.txt does not add the 'profiling' feature under if(WJS_ENABLE_TRACY) alone")
endif()
list(LENGTH _mentions _mention_count)
if(_mention_count GREATER 1)
    _fail("CMakeLists.txt names the 'profiling' feature more than once: ${_mentions}")
endif()

# ---- 5. CI -------------------------------------------------------------------

file(GLOB _workflows "${REPO_ROOT}/.github/workflows/*.yml" "${REPO_ROOT}/.github/workflows/*.yaml")
foreach(_workflow IN LISTS _workflows)
    file(READ "${_workflow}" _text)
    if(_text MATCHES "WJS_ENABLE_TRACY=")
        get_filename_component(_file "${_workflow}" NAME)
        _fail("${_file} sets WJS_ENABLE_TRACY by hand; the presets and the option's default decide")
    endif()
endforeach()

# ---- result ------------------------------------------------------------------

if(_failures)
    message(FATAL_ERROR "The Tracy policy does not hold:${_failures}")
endif()

list(LENGTH _names _preset_count)
list(JOIN _instrumented_presets ", " _instrumented_list)
message(STATUS "OK: ${_preset_count} configure presets; Tracy is on in: ${_instrumented_list}")
