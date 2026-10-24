# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# Tracy is built for any machine of its architecture.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/profiling/check_tracy_port.cmake
#
# Tracy's own CMake compiles for the machine it is built on (-march=native on x86,
# -mcpu=native on arm) unless NO_ISA_EXTENSIONS is set. CI shares one vcpkg binary
# cache per triplet between jobs that run on different hosted machines, so a
# tracy-capture built on one runner and restored on another with an older CPU dies
# with SIGILL (an illegal instruction) and profiling.trace_cli fails at random.
#
# The vcpkg fork's tracy port does not pass the option, so this repository carries
# the port as an overlay (cmake/vcpkg-ports/tracy, found through
# vcpkg-configuration.json) that does. This check holds the three things that make
# that true: the overlay is configured, it passes the option, and it is the version
# the manifest pins (so the overlay cannot quietly stop being the port that is
# built, or drift from the version the rest of the tree is written against).
# Once the fork's port passes the option itself, the overlay and this check go.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

set(_failures "")
function(_fail)
    string(JOIN "" _text ${ARGV})
    set(_failures "${_failures}\n  ${_text}" PARENT_SCOPE)
endfunction()

set(_overlay "cmake/vcpkg-ports")

# 1. The overlay is configured.
file(READ "${REPO_ROOT}/vcpkg-configuration.json" _configuration)
string(JSON _count LENGTH "${_configuration}" "overlay-ports")
set(_listed FALSE)
math(EXPR _last "${_count} - 1")
foreach(_index RANGE ${_last})
    string(JSON _path GET "${_configuration}" "overlay-ports" ${_index})
    if(_path STREQUAL "${_overlay}")
        set(_listed TRUE)
    endif()
endforeach()
if(NOT _listed)
    _fail("vcpkg-configuration.json does not list '${_overlay}' under overlay-ports")
endif()

# 2. The port passes the option.
set(_portfile "${REPO_ROOT}/${_overlay}/tracy/portfile.cmake")
if(NOT EXISTS "${_portfile}")
    _fail("${_overlay}/tracy/portfile.cmake is missing")
else()
    file(STRINGS "${_portfile}" _options REGEX "^[ \t]*-DNO_ISA_EXTENSIONS=ON")
    if(NOT _options)
        _fail("${_overlay}/tracy/portfile.cmake does not pass -DNO_ISA_EXTENSIONS=ON to Tracy: "
              "its tools and client would be built for the CPU of whichever runner built them "
              "(-march=native) and die with SIGILL on an older one that restores the cache")
    endif()
endif()

# 3. It is the version the manifest pins.
file(READ "${REPO_ROOT}/vcpkg.json" _manifest)
string(JSON _overrides LENGTH "${_manifest}" "overrides")
set(_pinned_version "")
set(_pinned_port 0)
math(EXPR _last "${_overrides} - 1")
foreach(_index RANGE ${_last})
    string(JSON _name GET "${_manifest}" "overrides" ${_index} "name")
    if(_name STREQUAL "tracy")
        string(JSON _pinned_version GET "${_manifest}" "overrides" ${_index} "version")
        string(JSON _pinned_port ERROR_VARIABLE _ignored GET "${_manifest}" "overrides" ${_index} "port-version")
    endif()
endforeach()
file(READ "${REPO_ROOT}/${_overlay}/tracy/vcpkg.json" _port)
string(JSON _port_version GET "${_port}" "version")
string(JSON _port_port ERROR_VARIABLE _ignored GET "${_port}" "port-version")
if(NOT _port_version STREQUAL "${_pinned_version}" OR NOT _port_port STREQUAL "${_pinned_port}")
    _fail("the overlay is tracy ${_port_version}#${_port_port} and vcpkg.json pins "
          "${_pinned_version}#${_pinned_port}")
endif()

# 4. The overlay's bytes are kept as they are, since vcpkg applies patches from it.
file(READ "${REPO_ROOT}/.gitattributes" _attributes)
if(NOT _attributes MATCHES "cmake/vcpkg-ports/[*][*] -text")
    _fail(".gitattributes does not say '${_overlay}/** -text': a checkout that changes line "
          "endings breaks the patches in the overlay")
endif()

if(_failures)
    message(FATAL_ERROR "Tracy's port:${_failures}")
endif()
message(STATUS "OK: Tracy is built from ${_overlay}/tracy without ISA extensions")
