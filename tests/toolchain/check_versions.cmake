# SPDX-License-Identifier: GPL-3.0-or-later
# The toolchain pins live in one file (.github/toolchain-versions.json) and
# the workflows read them from there. This check holds the two together: a
# version bumped in a workflow but not in the file, or the other way round,
# is a red test rather than a build that quietly uses something else
# (docs/ci.md, "Toolchain choices").
#
# Run as:
#   cmake -DREPO_ROOT=<checkout> -P tests/toolchain/check_versions.cmake

if(NOT DEFINED REPO_ROOT)
    message(FATAL_ERROR "check_versions.cmake: pass -DREPO_ROOT=<checkout>")
endif()

set(_failures "")

function(_fail message_text)
    set(_failures "${_failures}\n  ${message_text}" PARENT_SCOPE)
endfunction()

# ---- the file ---------------------------------------------------------------

set(_versions_file "${REPO_ROOT}/.github/toolchain-versions.json")
if(NOT EXISTS "${_versions_file}")
    message(FATAL_ERROR "The toolchain pins file is missing: ${_versions_file}")
endif()
file(READ "${_versions_file}" _versions)

string(JSON _cmake GET "${_versions}" cmake)
string(JSON _ninja GET "${_versions}" ninja)
string(JSON _clang_tidy GET "${_versions}" clang-tidy)
string(JSON _qt GET "${_versions}" qt version)
string(JSON _qt_kit GET "${_versions}" qt kit)
string(JSON _qt_modules GET "${_versions}" qt modules)
string(JSON _qt_action GET "${_versions}" qt action)
string(JSON _aqtsource GET "${_versions}" qt aqtsource)
string(JSON _vcpkg_pin_file GET "${_versions}" vcpkg pin_file)

if(NOT _cmake MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "toolchain-versions.json: cmake is not a version: '${_cmake}'")
endif()
if(NOT _clang_tidy MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "toolchain-versions.json: clang-tidy is not a version: '${_clang_tidy}'")
endif()

# ---- the workflows ----------------------------------------------------------

# Every workflow that configures a build pins CMake and Ninja to the versions
# in the file. The Qt checks below read ci.yml only: the analysis jobs do not
# build the Qt application.
set(_ci "")
foreach(_workflow IN ITEMS ci analysis)
    set(_workflow_file "${REPO_ROOT}/.github/workflows/${_workflow}.yml")
    if(NOT EXISTS "${_workflow_file}")
        _fail("the ${_workflow}.yml workflow is missing")
        continue()
    endif()
    file(READ "${_workflow_file}" _workflow_text)
    if(_workflow STREQUAL "ci")
        set(_ci "${_workflow_text}")
    endif()

    string(REGEX MATCHALL "cmakeVersion: \"([^\"]+)\"" _cmake_pins "${_workflow_text}")
    list(LENGTH _cmake_pins _cmake_count)
    if(_cmake_count EQUAL 0)
        _fail("${_workflow}.yml pins no CMake version; it must use ${_cmake}")
    endif()
    foreach(_pin IN LISTS _cmake_pins)
        string(REGEX REPLACE "^cmakeVersion: \"([^\"]+)\"$" "\\1" _value "${_pin}")
        if(NOT _value STREQUAL _cmake)
            _fail("${_workflow}.yml pins CMake ${_value}; toolchain-versions.json says ${_cmake}")
        endif()
    endforeach()

    string(REGEX MATCHALL "ninjaVersion: \"([^\"]+)\"" _ninja_pins "${_workflow_text}")
    list(LENGTH _ninja_pins _ninja_count)
    if(_ninja_count EQUAL 0)
        _fail("${_workflow}.yml pins no Ninja version; it must use ${_ninja}")
    endif()
    foreach(_pin IN LISTS _ninja_pins)
        string(REGEX REPLACE "^ninjaVersion: \"([^\"]+)\"$" "\\1" _value "${_pin}")
        if(NOT _value STREQUAL _ninja)
            _fail("${_workflow}.yml pins Ninja ${_value}; toolchain-versions.json says ${_ninja}")
        endif()
    endforeach()
endforeach()

# ---- the analysis tooling ---------------------------------------------------

# clang-tidy is a PyPI wheel pinned in one requirements file; the nightly job
# installs that file and runs it with warnings as errors. A version bumped in
# the requirements but not here, or the other way round, is a red test.
set(_analysis_requirements "${REPO_ROOT}/.github/requirements/analysis.txt")
if(NOT EXISTS "${_analysis_requirements}")
    _fail("the clang-tidy requirements file is missing: ${_analysis_requirements}")
else()
    file(READ "${_analysis_requirements}" _requirements_text)
    string(REPLACE "." "\\." _clang_tidy_regex "${_clang_tidy}")
    if(NOT _requirements_text MATCHES "clang-tidy==${_clang_tidy_regex}")
        _fail("analysis.txt does not pin clang-tidy==${_clang_tidy}; toolchain-versions.json says it must")
    endif()
endif()
if(NOT EXISTS "${REPO_ROOT}/.github/workflows/analysis.yml")
    _fail("the analysis workflow is missing")
else()
    file(READ "${REPO_ROOT}/.github/workflows/analysis.yml" _analysis_text)
    foreach(_needle IN ITEMS
            "requirements/analysis.txt"
            "run-clang-tidy"
            "warnings-as-errors")
        string(FIND "${_analysis_text}" "${_needle}" _at)
        if(_at EQUAL -1)
            _fail("analysis.yml no longer uses '${_needle}'; the clang-tidy gate would not run as docs/ci.md says")
        endif()
    endforeach()
endif()

# The Qt kit is installed by install-qt-action at the file's pin, with the
# file's version, architecture, modules and aqtinstall source. The aqtsource is
# a git commit, not a PyPI release: Qt 6.11+ split the Windows repository per
# architecture and aqtinstall 3.3.0, the last PyPI release, cannot read it
# (docs/ci.md, "Qt on the Windows leg"). the base kit carries lupdate and lrelease (qttools is an archive of the
# base package from Qt 6.12, not a module), which the catalogue drift check needs; qtquick3d and qtshadertools are the board scene's and, from 6.12, modules
# of their own; qtmultimedia, qtsensors and qtspeech
# are the application's sound, motion and speech (their silent variants exist
# for a kit without them, but this leg is the one that proves the real ones).
if(NOT _ci MATCHES "uses:[ \t]*jurplel/install-qt-action@[0-9a-f]+")
    _fail("ci.yml does not install Qt with a pinned jurplel/install-qt-action")
endif()
foreach(_pair IN ITEMS
        "action=${_qt_action}"
        "version=\"${_qt}\""
        "arch=${_qt_kit}"
        "modules=\"${_qt_modules}\""
        "aqtsource=\"${_aqtsource}\"")
    string(FIND "${_pair}" "=" _equals)
    math(EXPR _value_start "${_equals} + 1")
    string(SUBSTRING "${_pair}" 0 ${_equals} _key)
    string(SUBSTRING "${_pair}" ${_value_start} -1 _value)
    if(_key STREQUAL "action")
        if(NOT _ci MATCHES "uses:[ \t]*${_value}")
            _fail("ci.yml does not use ${_value}; toolchain-versions.json says it must")
        endif()
    else()
        string(FIND "${_ci}" "${_key}: ${_value}" _at)
        if(_at EQUAL -1)
            _fail("ci.yml does not pass ${_key}: ${_value} to install-qt-action; toolchain-versions.json says it must")
        endif()
    endif()
endforeach()
if(NOT _ci MATCHES "cache:[ \t]*true")
    _fail("ci.yml does not ask install-qt-action for cache: true")
endif()
if(_ci MATCHES "aqtinstall==")
    _fail("ci.yml still installs aqtinstall from PyPI; it must use install-qt-action and aqtsource (docs/ci.md)")
endif()

# ---- the vcpkg pin ----------------------------------------------------------

set(_pin_file "${REPO_ROOT}/${_vcpkg_pin_file}")
if(NOT EXISTS "${_pin_file}")
    _fail("the vcpkg pin file named by toolchain-versions.json is missing: ${_pin_file}")
else()
    file(STRINGS "${_pin_file}" _pin LIMIT_COUNT 1)
    string(STRIP "${_pin}" _pin)
    file(READ "${REPO_ROOT}/vcpkg.json" _manifest)
    string(JSON _baseline GET "${_manifest}" builtin-baseline)
    if(NOT _baseline STREQUAL _pin)
        _fail("vcpkg.json's builtin-baseline (${_baseline}) is not the commit in ${_vcpkg_pin_file} (${_pin}); docs/ci.md says they move together")
    endif()
endif()

# ---- result -----------------------------------------------------------------

if(_failures)
    message(FATAL_ERROR "The toolchain pins have drifted:${_failures}")
endif()
message(STATUS "toolchain: CMake ${_cmake}, Ninja ${_ninja}, clang-tidy ${_clang_tidy}, Qt ${_qt} (${_qt_kit}: ${_qt_modules}) by ${_qt_action}; the workflows, the requirements and the vcpkg pin agree")
