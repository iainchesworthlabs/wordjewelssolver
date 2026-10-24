# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# check_cli.cmake
#
# The command-line solver as a program: its options, its exit codes and what it
# prints. This is the system tier's window on wjs-cli - nothing else in the tree
# runs the executable - and it uses a twelve-word word list written here, so the
# expected words and points are the README's and do not move with the shipped
# dictionary.
#
# Invoked once per case by ctest (`system.cli.<case>`):
#   -DCLI=<wjs-cli> -DDICTIONARY=<data/dictionaries/en.txt>
#   -DWORK_DIR=<build>/system/cli -DCASE=<case>
#
# The cases run in parallel under `ctest -j`, so each writes under
# <WORK_DIR>/<case>: they would otherwise rewrite one word list under each other.
#
# The exit codes are the program's contract: 0 solved, 1 usage, 2 dictionary,
# 3 board.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.28)

foreach(_required IN ITEMS CLI DICTIONARY WORK_DIR CASE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "check_cli.cmake: ${_required} is required")
    endif()
endforeach()

if(NOT EXISTS "${CLI}")
    message(FATAL_ERROR "check_cli.cmake: the program '${CLI}' was not built")
endif()

set(_work "${WORK_DIR}/${CASE}")
file(MAKE_DIRECTORY "${_work}")
set(_words "${_work}/words.txt")
file(WRITE "${_words}" "CAT\nCAR\nCARE\nARE\nTAR\nTEA\nDOG\nCOD\nROD\nCATS\nREST\nAT\nQUIT\n")

# The README example, from the words above: exactly these nine are traceable and
# at least three letters long, and each scores one under Boggle rules.
set(_board CAT ORE DGS)

function(run_cli)
    execute_process(
        COMMAND "${CLI}" ${ARGN}
        RESULT_VARIABLE _code
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    set(RUN_CODE "${_code}" PARENT_SCOPE)
    set(RUN_OUT "${_out}" PARENT_SCOPE)
    set(RUN_ERR "${_err}" PARENT_SCOPE)
endfunction()

function(fail message)
    message(FATAL_ERROR
        "wjs-cli ${CASE}: ${message}\n"
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

function(expect_not_in text haystack)
    string(FIND "${haystack}" "${text}" _at)
    if(NOT _at EQUAL -1)
        fail("'${text}' should not be there")
    endif()
endfunction()

function(expect_match pattern haystack)
    if(NOT "${haystack}" MATCHES "${pattern}")
        fail("'${pattern}' does not match")
    endif()
endfunction()

function(expect_count text haystack expected)
    string(REGEX MATCHALL "${text}" _matches "${haystack}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL expected)
        fail("'${text}' occurs ${_count} time(s), expected ${expected}")
    endif()
endfunction()

if(CASE STREQUAL "help")
    run_cli(--help)
    expect_code(0)
    expect_in("Usage: wjs-cli" "${RUN_OUT}")
    expect_in("--dictionary" "${RUN_OUT}")
    expect_in("--scoring" "${RUN_OUT}")
    expect_in("--repeat" "${RUN_OUT}")
elseif(CASE MATCHES "^output-every-time-[1-4]$")
    # The program's whole output reaches the caller on every run, not on most. In the
    # Debug build (AddressSanitizer and the profiler's client) the flush at exit was
    # lost about one run in ten, so --version and --help sometimes left 0 with nothing
    # printed; one run proves nothing, so each is run 60 times, which a loss of
    # one in ten does not survive. The program flushes its output itself now (apps/cli/src/main.cpp).
    # The 60 runs are four cases of 15 (output-every-time-1 to -4), so that ctest runs the
    # four at once: a run starts the program, and under the sanitizers that is the cost.
    foreach(_run RANGE 1 15)
        run_cli(--version)
        expect_code(0)
        expect_match("wjs-cli [0-9]+\\.[0-9]+\\.[0-9]+" "${RUN_OUT}")
        run_cli(--help)
        expect_code(0)
        expect_in("Usage: wjs-cli" "${RUN_OUT}")
    endforeach()
elseif(CASE STREQUAL "version")
    run_cli(--version)
    expect_code(0)
    expect_match("wjs-cli [0-9]+\\.[0-9]+\\.[0-9]+" "${RUN_OUT}")
elseif(CASE STREQUAL "solve")
    run_cli(--dictionary "${_words}" ${_board})
    expect_code(0)
    expect_in("9 words, 9 points" "${RUN_OUT}")
    expect_in("CARE" "${RUN_OUT}")
    expect_in("TEA" "${RUN_OUT}")
    expect_not_in("CATS" "${RUN_OUT}")
    # One result line a word, each with its path.
    expect_count("[A-Z]+[ \t]+[0-9]+[ \t]+r[0-9]c[0-9]" "${RUN_OUT}" 9)
elseif(CASE STREQUAL "min-length")
    # Four letters or more: CARE is the only one of the nine the board can spell.
    run_cli(--dictionary "${_words}" --min-length 4 ${_board})
    expect_code(0)
    expect_in("CARE" "${RUN_OUT}")
    expect_in("1 words, 1 points" "${RUN_OUT}")
    expect_not_in("CAT " "${RUN_OUT}")
elseif(CASE STREQUAL "rarity")
    run_cli(--dictionary "${_words}" --scoring rarity ${_board})
    expect_code(0)
    expect_in("CARE" "${RUN_OUT}")
    expect_in("9 words," "${RUN_OUT}")
    expect_not_in("9 points" "${RUN_OUT}")
elseif(CASE STREQUAL "repeat")
    run_cli(--dictionary "${_words}" --repeat 3 ${_board})
    expect_code(0)
    # The repeats are for a clock or a profiler: one result is printed.
    expect_count("9 words, 9 points" "${RUN_OUT}" 1)
elseif(CASE STREQUAL "qu")
    # A Qu tile is two letters: QUIT is four letters over three tiles.
    run_cli(--dictionary "${_words}" "Qu I T")
    expect_code(0)
    expect_in("QUIT" "${RUN_OUT}")
    expect_in("1 words, 1 points" "${RUN_OUT}")
elseif(CASE STREQUAL "unknown-option")
    run_cli(--bogus ${_board})
    expect_code(1)
    expect_in("unknown option '--bogus'" "${RUN_ERR}")
    expect_in("Usage: wjs-cli" "${RUN_ERR}")
elseif(CASE STREQUAL "missing-option-value")
    run_cli(--dictionary)
    expect_code(1)
    expect_in("--dictionary needs a value" "${RUN_ERR}")
elseif(CASE STREQUAL "bad-min-length")
    run_cli(--min-length nope ${_board})
    expect_code(1)
    expect_in("'nope' is not a length" "${RUN_ERR}")
elseif(CASE STREQUAL "bad-repeat")
    run_cli(--repeat 0 ${_board})
    expect_code(1)
    expect_in("not a count of one or more" "${RUN_ERR}")
elseif(CASE STREQUAL "bad-scoring")
    run_cli(--scoring nope ${_board})
    expect_code(1)
    expect_in("'nope' is not a scoring scheme" "${RUN_ERR}")
elseif(CASE STREQUAL "no-board")
    run_cli()
    expect_code(1)
    expect_in("Usage: wjs-cli" "${RUN_ERR}")
elseif(CASE STREQUAL "board-error")
    run_cli(--dictionary "${_words}" CAT OR)
    expect_code(3)
    expect_in("not all the same length" "${RUN_ERR}")
elseif(CASE STREQUAL "dictionary-error")
    run_cli(--dictionary "${_work}/no-such-dictionary.txt" ${_board})
    expect_code(2)
    expect_in("no-such-dictionary.txt" "${RUN_ERR}")
else()
    message(FATAL_ERROR "check_cli.cmake: unknown CASE '${CASE}'")
endif()
