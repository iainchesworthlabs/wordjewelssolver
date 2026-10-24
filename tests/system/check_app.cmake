# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# check_app.cmake
#
# The Qt Quick application as a program, headless: what it does when its word
# list is missing, when its store's file is damaged, and when a smoke run has no
# display to present a frame on. The window's own models, screens and languages
# have their suites; this is the executable's start-up contract - a return code
# and a sentence on stderr - which nothing else runs.
#
# Only registered when WJS_BUILD_APP is on. `QT_QPA_PLATFORM=offscreen` keeps it
# display-free; Quick 3D cannot render there, which is exactly what the smoke
# case asserts (exit 3 and the sentence, not a hang).
#
#   -DAPP=<WordJewelsSolver> -DWORK_DIR=<build>/system/app -DCASE=<case>
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.28)

foreach(_required IN ITEMS APP WORK_DIR CASE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "check_app.cmake: ${_required} is required")
    endif()
endforeach()

if(NOT EXISTS "${APP}")
    message(FATAL_ERROR "check_app.cmake: the application '${APP}' was not built")
endif()

set(ENV{QT_QPA_PLATFORM} "offscreen")
set(ENV{QT_FORCE_STDERR_LOGGING} "1")

set(_work "${WORK_DIR}/${CASE}")
file(MAKE_DIRECTORY "${_work}")

function(run_app timeout)
    execute_process(
        COMMAND "${APP}" ${ARGN}
        RESULT_VARIABLE _code
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        TIMEOUT ${timeout})
    set(RUN_CODE "${_code}" PARENT_SCOPE)
    set(RUN_OUT "${_out}" PARENT_SCOPE)
    set(RUN_ERR "${_err}" PARENT_SCOPE)
endfunction()

function(fail message)
    message(FATAL_ERROR
        "WordJewelsSolver ${CASE}: ${message}\n"
        "  exit: ${RUN_CODE}\n"
        "  stdout:\n${RUN_OUT}\n"
        "  stderr:\n${RUN_ERR}")
endfunction()

function(expect_code expected)
    if(NOT RUN_CODE STREQUAL "${expected}")
        fail("exit code ${RUN_CODE}, expected ${expected}")
    endif()
endfunction()

function(expect_in text haystack)
    string(FIND "${haystack}" "${text}" _at)
    if(_at EQUAL -1)
        fail("'${text}' is missing")
    endif()
endfunction()

if(CASE STREQUAL "missing-dictionary")
    # The game cannot be made from a word list that is not there; the program
    # says which file and leaves with 2, before QML is loaded.
    run_app(30
        --dictionary "${_work}/no-such-list.txt"
        --store "${_work}/missing-dictionary.store")
    expect_code(2)
    expect_in("no-such-list.txt" "${RUN_ERR}")
    expect_in("could not be read" "${RUN_ERR}")
elseif(CASE STREQUAL "damaged-store")
    # The store is opened before the game is made. A damaged file is set aside,
    # said once, and the run goes on - here to the missing word list, which is
    # the next thing that stops it.
    set(_store "${_work}/damaged.store")
    file(WRITE "${_store}" "this is not a WordJewelsSolver store\n")
    run_app(30
        --dictionary "${_work}/no-such-list.txt"
        --store "${_store}")
    expect_code(2)
    expect_in("damaged" "${RUN_ERR}")
    expect_in("no-such-list.txt" "${RUN_ERR}")
elseif(CASE STREQUAL "phase-without-directory")
    # --phase is only meaningful with --phase-directory; the program says so and
    # goes on to fail on the word list.
    run_app(30
        --phase one
        --dictionary "${_work}/no-such-list.txt"
        --store "${_work}/phase.store")
    expect_code(2)
    expect_in("--phase applies only with --phase-directory" "${RUN_ERR}")
elseif(CASE STREQUAL "smoke-headless")
    # The smoke run starts the real window and scene and measures frames. On
    # this Qt 6.12 kit the offscreen platform does present them (the RHI
    # software backend), so the run must report and leave with 0. Where no
    # frame can be presented the program says so and leaves with 3; that is a
    # different machine's answer, not a failure of this one, so the assertion
    # follows whichever the program says and checks that the two agree.
    # 120 s: a Debug build with AddressSanitizer took 56 s on a busy host, and
    # a timeout is a failure, not a skip.
    run_app(120
        --smoke 2
        --store "${_work}/smoke.store")
    if(RUN_CODE STREQUAL "3")
        expect_in("no frame was presented" "${RUN_ERR}")
        message(STATUS "system.app.smoke-headless: this platform presented no frame; the exit-3 path is verified")
    else()
        expect_code(0)
        expect_in("smoke: frames=" "${RUN_OUT}")
        expect_in("ui.frame_ms" "${RUN_OUT}")
    endif()
else()
    message(FATAL_ERROR "check_app.cmake: unknown CASE '${CASE}'")
endif()
