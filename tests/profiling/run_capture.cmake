# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# tracy-capture with its output thrown away, for check_trace.cmake.
#
#   cmake -DCAPTURE=<tracy-capture> -DTRACE=<file> -DPORT=<n> -DSECONDS=<n>
#         -P tests/profiling/run_capture.cmake
#
# check_trace runs the program and the capture side by side, joined by a pipe
# (execute_process's way of running two commands at once). A capture that writes its
# progress into that pipe is killed by SIGPIPE (macOS) when the program at the other
# end has gone, before it saves the trace; this one writes nothing to it. It also
# stops after SECONDS whether or not the program is done, so a program that was over
# before the capture found it (Tracy is on-demand and a capture takes about 0.7 s to
# attach) costs that long and not for ever.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

foreach(_required IN ITEMS CAPTURE TRACE PORT SECONDS)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "run_capture.cmake: ${_required} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${CAPTURE}" -o "${TRACE}" -a 127.0.0.1 -p "${PORT}" -f -s "${SECONDS}"
    OUTPUT_QUIET
    ERROR_QUIET
    RESULT_VARIABLE _code)
if(NOT _code EQUAL 0)
    message(FATAL_ERROR "tracy-capture exited with ${_code}")
endif()
