find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(wjs_unit_tests
    unit/board_test.cpp
    unit/dice_data_test.cpp
    unit/dice_test.cpp
    unit/dictionary_test.cpp
    unit/game_rules_test.cpp
    unit/grapheme_binder_test.cpp
    unit/path_test.cpp
    unit/paths_test.cpp
    unit/phase_data_test.cpp
    unit/profiling_test.cpp
    unit/scoring_test.cpp
    unit/shake_test.cpp
    unit/seeding_test.cpp
    unit/solver_test.cpp)

# Whether the profiling calls are meant to have Tracy behind them in this
# build: the option CMake was given, passed in as a value (profiling_test.cpp).
target_compile_definitions(wjs_unit_tests PRIVATE
    "WJS_TEST_EXPECT_PROFILING=$<BOOL:${WJS_ENABLE_TRACY}>")

target_link_libraries(wjs_unit_tests
    PRIVATE
        wjs::lib
        wjs::platform
        wjs::warnings
        wjs::sanitizers
        wjs::coverage
        Catch2::Catch2WithMain)

# The shipped data, by its path in this checkout, for the cases about the data
# itself (dice_data_test.cpp), as the performance tier does for the dictionary.
target_compile_definitions(wjs_unit_tests PRIVATE
    "WJS_DATA_DIRECTORY=\"${PROJECT_SOURCE_DIR}/data\"")

wjs_deploy_sanitizer_runtime(wjs_unit_tests)

catch_discover_tests(wjs_unit_tests)

# --- Performance tests: work budgets and a few relative timings -------------
# wjs_perf_tests holds the tier that fails a change that makes the solver
# meaningfully more expensive, the way a unit test fails a change that makes it
# wrong: a budget on what the solver and the loader do (visits, words, trie
# nodes), exact on every machine, and wall-time checks against a calibration
# workload that only run on request. Its own binary so that it can be built and
# run alone and so that nothing else's static state or allocator churn is in
# the way of a stopwatch. wjs::lib only: no Qt and no platform layer.
# docs/testing.md, "Performance tests".
add_executable(wjs_perf_tests
    perf/corpus.cpp
    perf/measure.cpp
    perf/metrics.cpp
    perf/report_test.cpp
    perf/timing_test.cpp
    perf/work_test.cpp)

target_link_libraries(wjs_perf_tests
    PRIVATE
        wjs::lib
        wjs::warnings
        wjs::sanitizers
        wjs::coverage
        Catch2::Catch2WithMain)

target_include_directories(wjs_perf_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/perf")

# What a build is, as far as a stopwatch goes (perf/perf_build.hpp). Values
# CMake passes in, not preprocessor conditionals: the configuration is a
# generator expression so that a multi-config generator is right per
# configuration, and the instrumented flag is the options that put coverage or
# Tracy in (the sanitizers only ever apply to Debug, which is not an optimised
# configuration). The word list is the shipped one, by absolute path.
target_compile_definitions(wjs_perf_tests PRIVATE
    "WJS_PERF_OPTIMISED=$<IF:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>,1,0>"
    "WJS_PERF_INSTRUMENTED=$<IF:$<OR:$<BOOL:${WJS_ENABLE_COVERAGE}>,$<BOOL:${WJS_ENABLE_TRACY}>>,1,0>"
    "WJS_PERF_BUILD_TYPE=\"$<CONFIG>\""
    "WJS_PERF_DICTIONARY=\"${PROJECT_SOURCE_DIR}/data/dictionaries/en.txt\"")

# The one environment variable the tier reads, WJS_PERF_TIMING (measure.cpp),
# is read with std::getenv, which MSVC's runtime deprecates in favour of a
# call that only it has. It is a read of one variable at start-up; this says
# so to the runtime rather than to a source file.
target_compile_definitions(wjs_perf_tests PRIVATE _CRT_SECURE_NO_WARNINGS)

wjs_deploy_sanitizer_runtime(wjs_perf_tests)

# Two registrations of one binary, because a stopwatch is the one test that
# cannot share the machine: [timing] cases are RUN_SERIAL, so `ctest -j` runs
# them alone, and they skip themselves unless WJS_PERF_TIMING is set (the
# *-perf test presets set it; docs/testing.md says why they are opt-in). The
# budget cases are plain counts and run in parallel with everything else, in
# every optimised build and skipped in the others. The hidden [.perf-report]
# cases are in neither: they are a tool, not a test. Every case carries the
# `perf` label, so `ctest -L perf` is the whole tier.
catch_discover_tests(wjs_perf_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
    TEST_SPEC "[perf]~[timing]")
catch_discover_tests(wjs_perf_tests DISCOVERY_MODE PRE_TEST ADD_TAGS_AS_LABELS
    TEST_SPEC "[timing]"
    PROPERTIES RUN_SERIAL TRUE)

# The application's model, built from its source so the test does not need the
# QML module (QML_ELEMENT registration is inert without it). Qt is only here
# when the application is.
if(WJS_BUILD_APP)
    add_executable(wjs_app_tests
        app/solver_model_test.cpp
        "${PROJECT_SOURCE_DIR}/src/app/solver_model.cpp")
    set_target_properties(wjs_app_tests PROPERTIES AUTOMOC ON)
    target_include_directories(wjs_app_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
    target_link_libraries(wjs_app_tests
        PRIVATE
            wjs::lib
            wjs::platform
            Qt6::Core
            Qt6::Qml
            wjs::warnings
            wjs::sanitizers
            wjs::coverage
            Catch2::Catch2WithMain)
    wjs_deploy_sanitizer_runtime(wjs_app_tests)
    # Qt's DLLs have to be beside the binary before catch_discover_tests runs it.
    wjs_platform_deploy_qt_runtime(wjs_app_tests "")
    catch_discover_tests(wjs_app_tests)
endif()

# Every platform fragment satisfies the platform contract, on whichever host
# this runs (a build only includes the one fragment for its own platform).
add_test(NAME platform.fragments
    COMMAND "${CMAKE_COMMAND}"
        "-DREPO_ROOT=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/platform/check_fragments.cmake")

# Nothing that ships has Tracy in it: only the *-profiling presets turn the
# option on, it defaults to OFF, and the manifest feature that installs it is
# neither a base dependency nor a default feature (docs/profiling.md). Runs on
# every host and in every CI leg, since it only reads the presets.
add_test(NAME profiling.shipped_presets
    COMMAND "${CMAKE_COMMAND}"
        "-DREPO_ROOT=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/profiling/check_presets.cmake")

# Every zone is named <layer>.<thing> and once: one name is one source location.
add_test(NAME profiling.zone_names
    COMMAND "${CMAKE_COMMAND}"
        "-DREPO_ROOT=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/profiling/check_zone_names.cmake")

# No preprocessor conditionals or defines, no OS headers outside the platform
# section, no OS tests in CMake outside the selector. CI runs it as its own job
# too; this is the same check, so a developer sees it fail locally.
find_program(WJS_PWSH NAMES pwsh)
if(WJS_PWSH)
    # The word lists: lowercase a-z, sorted, unique, and every category and
    # tier word is in the dictionary.
    # The project's dice are what tools/data/make_dice.ps1 makes from the
    # dictionary as it is now: the source hash and the dice both have to match.
    add_test(NAME data.dice
        COMMAND "${WJS_PWSH}" -NoProfile -File
            "${PROJECT_SOURCE_DIR}/tools/data/make_dice.ps1"
            -Root "${PROJECT_SOURCE_DIR}" -Check)

    add_test(NAME data.wordlists
        COMMAND "${WJS_PWSH}" -NoProfile -File
            "${PROJECT_SOURCE_DIR}/tools/data/check_wordlists.ps1"
            -Root "${PROJECT_SOURCE_DIR}")

    # The phonics data under data/phonics, and the checker that reads it. There
    # is none yet: the files have to be written and reviewed by a person
    # qualified to do it. With the review gate on (a release configuration),
    # a file without its review header fails here as well as in the build.
    set(WJS_PHONICS_CHECK_ARGS "")
    if(WJS_REQUIRE_REVIEWED_PHONICS)
        list(APPEND WJS_PHONICS_CHECK_ARGS -RequireReviewed)
    endif()
    add_test(NAME data.phonics
        COMMAND "${WJS_PWSH}" -NoProfile -File
            "${PROJECT_SOURCE_DIR}/tools/data/check_phonics.ps1"
            -Root "${PROJECT_SOURCE_DIR}" ${WJS_PHONICS_CHECK_ARGS})

    # The checker held to a table of cases on throw-away data: the review gate
    # must refuse what it should, so the gate itself is tested.
    add_test(NAME data.phonics.checker
        COMMAND "${WJS_PWSH}" -NoProfile -File
            "${PROJECT_SOURCE_DIR}/tools/data/test_check_phonics.ps1")

    add_test(NAME platform.isolation
        COMMAND "${WJS_PWSH}" -NoProfile -File
            "${PROJECT_SOURCE_DIR}/tools/checks/check_platform_isolation.ps1"
            -Root "${PROJECT_SOURCE_DIR}")
endif()
