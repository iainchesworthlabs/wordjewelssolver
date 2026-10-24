# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# check_install.cmake
#
# The install rules as a system: install the built tree into a throw-away
# prefix, check the layout a package promises (the programs, the dictionary, the
# licence and the third-party notices), and run the installed wjs-cli from that
# prefix so the installed dictionary is the one it reads.
#
# `package.config` reads the generated scripts; this runs them. Both are wanted:
# one catches a dropped install() rule at configure time, the other catches an
# install rule that names a file the build did not produce.
#
#   -DBUILD_DIR=<build> -DWORK_DIR=<build>/system/install
#   -DCONFIG=<Debug|Release|...> -DEXE_SUFFIX=<.exe or empty>
#   -DEXPECT_CLI=<ON|OFF> -DEXPECT_APP=<ON|OFF>
#   -DAPP_RELATIVE=<where the installed application is, from the prefix>
#   -DRUNTIME_DIR=<the build's bin directory>
#
# RUNTIME_DIR is put on PATH for the installed program. A Debug build links the
# sanitizer runtime and CMake's wjs_deploy_sanitizer_runtime copies it beside the
# build's programs, not into the install prefix; the installed program is being
# checked for its layout and its data, not for carrying a development runtime.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.28)

foreach(_required IN ITEMS BUILD_DIR WORK_DIR CONFIG EXE_SUFFIX)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "check_install.cmake: ${_required} is required")
    endif()
endforeach()

set(_prefix "${WORK_DIR}/prefix")
file(REMOVE_RECURSE "${_prefix}")
file(MAKE_DIRECTORY "${_prefix}")

set(_install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${_prefix}")
if(NOT CONFIG STREQUAL "")
    list(APPEND _install_command --config "${CONFIG}")
endif()

execute_process(
    COMMAND ${_install_command}
    RESULT_VARIABLE _install_code
    OUTPUT_VARIABLE _install_out
    ERROR_VARIABLE _install_err
    TIMEOUT 300)
if(NOT _install_code EQUAL 0)
    message(FATAL_ERROR
        "system.install: cmake --install failed (exit ${_install_code})\n"
        "stdout:\n${_install_out}\nstderr:\n${_install_err}")
endif()

function(expect_file path what)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "system.install: ${what} is missing at ${path}")
    endif()
endfunction()

set(_bin "${_prefix}/bin")
set(_cli "${_bin}/wjs-cli${EXE_SUFFIX}")
# The application is an ordinary executable in the bindir on Windows and Linux
# and a bundle on macOS; the platform fragment names the directory its data and
# executable share, and tests/CMakeLists.txt passes it, so there is no OS test
# here.
set(_app "${_bin}/WordJewelsSolver${EXE_SUFFIX}")
if(DEFINED APP_RELATIVE AND NOT "${APP_RELATIVE}" STREQUAL "")
    set(_app "${_prefix}/${APP_RELATIVE}")
endif()
set(_dictionary "${_bin}/dictionaries/en.txt")

if(EXPECT_CLI)
    expect_file("${_cli}" "the installed command-line solver")
    expect_file("${_dictionary}" "the installed dictionary")
endif()
if(EXPECT_APP)
    expect_file("${_app}" "the installed application")
endif()
# Both programs' shared data: the licence and the third-party notices are
# installed beside them (cmake/Notices.cmake).
expect_file("${_bin}/LICENSE.txt" "the licence text")
expect_file("${_bin}/NOTICES.txt" "the third-party notices")

if(EXPECT_CLI)
    # The ASan runtime (a Debug build) lives beside the build's programs, not in
    # the installed tree. cmake -E env's path_list_prepend puts that directory
    # on PATH with the host's own separator, so there is no OS conditional here
    # (tests/../tools/checks/check_platform_isolation.ps1 forbids one) and
    # nothing is copied into the prefix.
    set(_cli_command "${_cli}" CAT ORE DGS)
    if(DEFINED RUNTIME_DIR AND NOT "${RUNTIME_DIR}" STREQUAL "" AND EXISTS "${RUNTIME_DIR}")
        set(_cli_command "${CMAKE_COMMAND}" -E env
            "--modify" "PATH=path_list_prepend:${RUNTIME_DIR}" --
            "${_cli}" CAT ORE DGS)
    endif()
    # The README board, solved by the installed program with the installed word
    # list. The exact count depends on the shipped dictionary; what matters is
    # that it ran, read that dictionary and printed a result.
    execute_process(
        COMMAND ${_cli_command}
        RESULT_VARIABLE _cli_code
        OUTPUT_VARIABLE _cli_out
        ERROR_VARIABLE _cli_err
        TIMEOUT 60)
    if(NOT _cli_code EQUAL 0)
        message(FATAL_ERROR
            "system.install: the installed wjs-cli failed (exit ${_cli_code})\n"
            "stdout:\n${_cli_out}\nstderr:\n${_cli_err}")
    endif()
    if(NOT _cli_out MATCHES "[0-9]+ words, [0-9]+ points")
        message(FATAL_ERROR
            "system.install: the installed wjs-cli printed no summary\n${_cli_out}")
    endif()
    string(FIND "${_cli_out}" "CARE" _care)
    if(_care EQUAL -1)
        message(FATAL_ERROR
            "system.install: the installed wjs-cli did not find CARE from the "
            "installed dictionary\n${_cli_out}")
    endif()
endif()

message(STATUS "system.install: installed ${_prefix} and ran the installed programs")
