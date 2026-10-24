# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The privacy check's own test (libs/scan/tests/check_privacy.cmake):
#
#   cmake -DCHECK=<check_privacy.cmake> -DFIXTURES=<libs/scan/tests/privacy_fixture> \
#         -P libs/scan/tests/check_privacy_selftest.cmake
#
# A check that can only be seen to pass is not known to work. This runs it on a small tree
# for each rule it holds, which does exactly one thing that rule forbids, and requires it
# to fail, and to name that rule and that file and no other rule. It also runs it on a tree
# that does nothing wrong, which has to pass and be counted, and on a tree with nothing in
# it, which has to be refused rather than pass for lack of anything to read. A test that
# only expected the check to fail (WILL_FAIL) would pass on a syntax error in the script.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT CHECK OR NOT FIXTURES)
    message(FATAL_ERROR "Pass -DCHECK=<check_privacy.cmake> and -DFIXTURES=<privacy_fixture>.")
endif()

set(_all_rules "file output;frame capture;audio capture;network")
set(_problems "")

function(run_check root out_code out_text)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${root}" -P "${CHECK}"
        RESULT_VARIABLE _code
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    set(${out_code} "${_code}" PARENT_SCOPE)
    set(${out_text} "${_out}${_err}" PARENT_SCOPE)
endfunction()

# A tree that breaks one rule: the check fails, says which file and which rule, and no
# other rule.
function(expect_leak name rule file_fragment)
    run_check("${FIXTURES}/${name}" code text)
    set(found "")
    if(code EQUAL 0)
        set(found "${name}: the check passed a tree that breaks '${rule}'\n")
    endif()
    string(FIND "${text}" "${file_fragment}" at)
    if(at EQUAL -1)
        string(APPEND found "${name}: the failure does not name ${file_fragment}:\n${text}\n")
    endif()
    foreach(other IN LISTS _all_rules)
        string(FIND "${text}" ": ${other}: " seen)
        if(other STREQUAL rule AND seen EQUAL -1)
            string(APPEND found "${name}: the failure does not name the rule '${rule}':\n${text}\n")
        elseif(NOT other STREQUAL rule AND NOT seen EQUAL -1)
            string(APPEND found "${name}: the failure names '${other}' as well as '${rule}':\n${text}\n")
        endif()
    endforeach()
    set(_problems "${_problems}${found}" PARENT_SCOPE)
endfunction()

# `privacy_fixture` itself holds the file-output tree (libs/scan/leak.cpp), whose path
# tests/privacy/test_check_sources.cmake also names.
run_check("${FIXTURES}" code text)
if(code EQUAL 0)
    string(APPEND _problems "file output: the check passed a tree that writes a file\n")
endif()
string(FIND "${text}" "libs/scan/leak.cpp" at)
string(FIND "${text}" ": file output: " rule_at)
if(at EQUAL -1 OR rule_at EQUAL -1)
    string(APPEND _problems "file output: the failure does not name leak.cpp and the rule:\n${text}\n")
endif()
foreach(other IN ITEMS "frame capture" "audio capture" "network")
    string(FIND "${text}" ": ${other}: " seen)
    if(NOT seen EQUAL -1)
        string(APPEND _problems "file output: the failure names '${other}' too:\n${text}\n")
    endif()
endforeach()

expect_leak(audio_leak "audio capture" "apps/app/src/camera/microphone.cpp")
expect_leak(network_leak "network" "libs/platforms/windows/camera_upload.cpp")
expect_leak(frame_leak "frame capture" "apps/app/src/variants/camera-fixture/saver.cpp")

# A tree that does nothing wrong passes, and is counted.
run_check("${FIXTURES}/clean" code text)
if(NOT code EQUAL 0)
    string(APPEND _problems "clean: the check failed a tree with nothing in it that is forbidden:\n${text}\n")
endif()
string(FIND "${text}" "1 scanner source(s) checked" at)
if(at EQUAL -1)
    string(APPEND _problems "clean: the check did not count its one source:\n${text}\n")
endif()

# A root with nothing to read is refused, not passed.
run_check("${FIXTURES}/empty" code text)
if(code EQUAL 0)
    string(APPEND _problems "empty: the check passed a root with no scanner sources\n")
endif()
string(FIND "${text}" "No scanner sources found" at)
if(at EQUAL -1)
    string(APPEND _problems "empty: the refusal does not say why:\n${text}\n")
endif()

if(_problems)
    message(FATAL_ERROR "The privacy check does not hold its rules:\n${_problems}")
endif()
message(STATUS "scan.privacy.catches_a_leak: file output, frame capture, audio capture and network are each caught alone; a clean tree passes; an empty root is refused")
