# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# Sanitizers.cmake
#
# Defines an INTERFACE target `wjs::sanitizers` that, for Debug builds,
# turns on AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) or
# AddressSanitizer (MSVC), plus standard-library hardening assertions. All
# flags are guarded by $<CONFIG:Debug>, so Release builds are unaffected.
#
# Link it PRIVATE into every first-party target (library, app, tests) so both
# the instrumentation (compile) and the runtime (link) are applied.
# ---------------------------------------------------------------------------

option(WJS_ENABLE_SANITIZERS "Enable ASan/UBSan for Debug builds" ON)
option(WJS_SANITIZER_OPTIMIZE
    "Build first-party code with /O1 in a Debug build that has MSVC's AddressSanitizer on" ON)

# ThreadSanitizer is the other runtime: it cannot be combined with AddressSanitizer,
# so the tsan presets turn WJS_ENABLE_SANITIZERS off and this on. Built at -O1 with
# frame pointers, as the other sanitizer builds are. Debug only, like them.
option(WJS_ENABLE_TSAN "Enable ThreadSanitizer for Debug builds (GCC/Clang; excludes ASan/UBSan)" OFF)

add_library(wjs_sanitizers INTERFACE)
add_library(wjs::sanitizers ALIAS wjs_sanitizers)

if(WJS_ENABLE_TSAN)
    if(WJS_ENABLE_SANITIZERS)
        message(FATAL_ERROR "WJS_ENABLE_TSAN cannot be combined with WJS_ENABLE_SANITIZERS "
            "(ThreadSanitizer and AddressSanitizer are separate runtimes). Use a tsan preset.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "WJS_ENABLE_TSAN needs GCC or Clang.")
    endif()
    target_compile_options(wjs_sanitizers INTERFACE
        "$<$<CONFIG:Debug>:-fsanitize=thread;-fno-omit-frame-pointer;-fno-sanitize-recover=all;-O1>")
    target_link_options(wjs_sanitizers INTERFACE "$<$<CONFIG:Debug>:-fsanitize=thread>")
endif()

if(WJS_ENABLE_SANITIZERS)
    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang-cl: LLVM's bundled ASan runtime on Windows requires the
        # release CRT, which is incompatible with this project's debug-CRT
        # vcpkg/Qt dependencies. Real MSVC (the elseif(MSVC) branch) already
        # supports ASan together with the debug CRT natively.
        message(STATUS
            "ASan/UBSan disabled for clang-cl: LLVM's Windows ASan runtime "
            "requires the release CRT, which is incompatible with this "
            "project's debug-CRT vcpkg/Qt dependencies. Use windows-msvc for "
            "sanitizer coverage.")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(WJS_SANITIZER_FLAGS
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all)
        target_compile_options(wjs_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:${WJS_SANITIZER_FLAGS}>")
        target_link_options(wjs_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
    elseif(MSVC)
        # MSVC ships AddressSanitizer; UBSan is not available.
        target_compile_options(wjs_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:/fsanitize=address>")
        # MSVC ASan turns on STL container-overflow annotations, which
        # require every linked library to also be ASan-instrumented, or the
        # link fails with LNK2038 mismatches. vcpkg builds its dependencies
        # without ASan, so disable the STL annotations wholesale to match.
        target_compile_definitions(wjs_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:_DISABLE_STL_ANNOTATION>")
        # /RTC (runtime checks) and incremental linking are incompatible with
        # ASan, so remove /RTC from the Debug flags and disable incremental link.
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        # Optimised, with the sanitizer on: ASan turns every access to memory into
        # a check and every copy of a struct into a call, and unoptimised code has
        # a great many of both (a value kept in the stack and read again, a small
        # function not inlined). /O1 takes the dice simulation from 1.57 s to 0.79 s
        # a play with the same results, with ASan finding what it found. Debug
        # information is kept; to step through the code in a debugger, configure
        # with -DWJS_SANITIZER_OPTIMIZE=OFF.
        if(WJS_SANITIZER_OPTIMIZE)
            # CMake adds /RTC1 to a Debug target by itself (policy CMP0184), apart
            # from the flags above, and cl refuses /O1 together with /RTC1.
            set(CMAKE_MSVC_RUNTIME_CHECKS "")
            string(REGEX REPLACE "/O[12dgisx]|/Ob[0-3]" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
            string(STRIP "${CMAKE_CXX_FLAGS_DEBUG} /O1" CMAKE_CXX_FLAGS_DEBUG)
        endif()
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}"
            CACHE STRING "Flags used by the CXX compiler during Debug builds." FORCE)
        target_link_options(wjs_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    endif()
endif()

# Debug hardening: turn on libstdc++/libc++ precondition assertions. Harmless
# where the standard library does not recognise the macro (e.g. MSVC STL).
target_compile_definitions(wjs_sanitizers INTERFACE
    "$<$<AND:$<CONFIG:Debug>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>>:_GLIBCXX_ASSERTIONS>")

# ---------------------------------------------------------------------------
# The ASan runtime (clang_rt.asan_dynamic-*.dll) isn't in a system directory,
# so executables linked against wjs::sanitizers fail to start with
# STATUS_DLL_NOT_FOUND unless run from a Developer Command Prompt. Copy it
# next to the target so it runs standalone, matching what windeployqt does
# for Qt's own DLLs.
# ---------------------------------------------------------------------------
function(wjs_deploy_sanitizer_runtime target)
    if(WJS_ENABLE_SANITIZERS AND MSVC)
        set(_wjs_asan_search_dirs "")

        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
                OUTPUT_VARIABLE _wjs_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(_wjs_clang_resource_dir)
                list(APPEND _wjs_asan_search_dirs "${_wjs_clang_resource_dir}/lib/windows")
            endif()
            unset(_wjs_clang_resource_dir)
        else()
            get_filename_component(_wjs_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            list(APPEND _wjs_asan_search_dirs "${_wjs_msvc_bin_dir}")
            unset(_wjs_msvc_bin_dir)
        endif()

        set(_wjs_asan_dll "")
        foreach(_wjs_asan_dir IN LISTS _wjs_asan_search_dirs)
            file(GLOB _wjs_asan_candidates "${_wjs_asan_dir}/clang_rt.asan_dynamic-*.dll")
            if(_wjs_asan_candidates)
                list(GET _wjs_asan_candidates 0 _wjs_asan_dll)
                break()
            endif()
        endforeach()

        if(_wjs_asan_dll)
            # A separate POST_BUILD copy per target can race when independent
            # targets link in parallel. Make the copy a single shared build
            # edge instead, so ninja only ever runs it once.
            if(NOT TARGET wjs_asan_runtime_deploy)
                get_filename_component(_wjs_asan_dll_name "${_wjs_asan_dll}" NAME)
                set(_wjs_asan_dll_dest
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_wjs_asan_dll_name}")
                add_custom_command(
                    OUTPUT "${_wjs_asan_dll_dest}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_wjs_asan_dll}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                    DEPENDS "${_wjs_asan_dll}"
                    COMMENT "Copying ASan runtime into ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
                add_custom_target(wjs_asan_runtime_deploy
                    DEPENDS "${_wjs_asan_dll_dest}")
                unset(_wjs_asan_dll_name)
                unset(_wjs_asan_dll_dest)
            endif()
            add_dependencies(${target} wjs_asan_runtime_deploy)
        else()
            message(WARNING
                "ASan is enabled but its runtime DLL was not found (searched: "
                "${_wjs_asan_search_dirs}); ${target} may fail to start "
                "with STATUS_DLL_NOT_FOUND outside a Developer Command Prompt.")
        endif()

        unset(_wjs_asan_search_dirs)
        unset(_wjs_asan_dll)
        unset(_wjs_asan_candidates)
    endif()
endfunction()
