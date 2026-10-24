# SPDX-License-Identifier: GPL-3.0-or-later
# The program's version is written in three places: project(VERSION) in the
# root CMakeLists.txt, "version" in vcpkg.json, and the newest release heading in
# CHANGELOG.md. They are bumped together at a release; this check holds them to
# each other, so one bumped without the others is a red test and not a package
# that says 0.2.0 to the installer and 0.1.0 to the manifest.
#
# Run as:
#   cmake -DREPO_ROOT=<checkout> [-DVERSION=<project version>] -P check_version.cmake
#
# Also what the release job runs, before it builds anything:
#   -DTAG=<v0.1.0>                 the tag being released must be that version
#   -DREQUIRE_CHANGELOG_ENTRY=ON   the changelog must already have its heading
#   -DPRINT=ON                     print only the version (for a script to read)
#
# Without a release heading in the changelog (the project has not shipped one
# yet) the changelog leg has nothing to compare and is skipped, unless
# REQUIRE_CHANGELOG_ENTRY says a release needs it.

if(NOT DEFINED REPO_ROOT)
    message(FATAL_ERROR "check_version.cmake: pass -DREPO_ROOT=<checkout>")
endif()

set(_failures "")
set(_skipped "")

function(_fail message_text)
    set(_failures "${_failures}\n  ${message_text}" PARENT_SCOPE)
endfunction()

# ---- project(VERSION) -------------------------------------------------------
# Read from the file, not from a configured build, so the release job can run
# this on a bare checkout; a ctest run passes the configured value as well and
# the two have to agree.
file(READ "${REPO_ROOT}/CMakeLists.txt" _root)
if(_root MATCHES "project\\([^)]*VERSION[ \t\r\n]+([0-9]+\\.[0-9]+\\.[0-9]+)")
    set(_project "${CMAKE_MATCH_1}")
else()
    message(FATAL_ERROR "check_version.cmake: no project(... VERSION x.y.z) in ${REPO_ROOT}/CMakeLists.txt")
endif()
if(DEFINED VERSION AND NOT "${VERSION}" STREQUAL "" AND NOT "${VERSION}" STREQUAL "${_project}")
    _fail("the configured project version is ${VERSION} but CMakeLists.txt says ${_project} (reconfigure)")
endif()

if(PRINT)
    message(NOTICE "${_project}")
    return()
endif()

# ---- vcpkg.json -------------------------------------------------------------
file(READ "${REPO_ROOT}/vcpkg.json" _manifest)
string(JSON _manifest_version GET "${_manifest}" version)
if(NOT "${_manifest_version}" STREQUAL "${_project}")
    _fail("vcpkg.json version is ${_manifest_version}, project(VERSION) is ${_project}")
endif()

# ---- CHANGELOG.md -----------------------------------------------------------
# The newest release is the first "## [x.y.z]" heading; "## [Unreleased]" above
# it is the work after it.
file(STRINGS "${REPO_ROOT}/CHANGELOG.md" _headings
    REGEX "^## \\[[0-9]+\\.[0-9]+\\.[0-9]+[^]]*\\]")
if(_headings)
    list(GET _headings 0 _top)
    string(REGEX REPLACE "^## \\[([0-9]+\\.[0-9]+\\.[0-9]+)[^]]*\\].*$" "\\1" _changelog "${_top}")
    if(NOT "${_changelog}" STREQUAL "${_project}")
        _fail("the newest CHANGELOG.md release is ${_changelog}, project(VERSION) is ${_project}")
    endif()
elseif(REQUIRE_CHANGELOG_ENTRY)
    _fail("CHANGELOG.md has no '## [${_project}]' release heading")
else()
    set(_skipped " (CHANGELOG.md has no release heading yet, so it was not compared)")
endif()

# ---- the tag ----------------------------------------------------------------
if(DEFINED TAG AND NOT "${TAG}" STREQUAL "")
    string(REGEX REPLACE "^v" "" _tag_version "${TAG}")
    if(NOT "${_tag_version}" STREQUAL "${_project}")
        _fail("the tag ${TAG} is not v${_project}")
    endif()
endif()

if(_failures)
    message(FATAL_ERROR "The program's version is not one number:${_failures}")
endif()
message(STATUS "Version ${_project}: project(VERSION) and vcpkg.json agree${_skipped}")
