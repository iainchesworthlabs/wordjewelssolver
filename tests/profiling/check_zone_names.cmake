# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# Every zone has a name of the right shape, no name is used twice, and the
# zones a trace of the application's work is made of exist.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/profiling/check_zone_names.cmake
#
# A zone's source location is a function-local static of zone<"name">() (see
# libs/core/variants/profiling-enabled/wjs/profiling.hpp), so there is one per
# NAME: a name used at two places shows the first one's file and line, and the
# second's call is indistinguishable from it in a trace. The names are also
# what the CSV export and the profiler show and what any baseline reads, so
# they are `<layer>.<thing>`, lower case, dotted (docs/profiling.md).
#
# The names that are not zones have their own shapes and are checked too:
# frame sets are a plain name ("UI", "Capture"), thread names are lower case
# ("main", "render"), and plots are `<layer>.<thing>` as zones are. A plot
# used twice is checked, because two plots of one name are one line.
#
# The QML seam's names come from QML, not from a template argument, so they are
# found as text: `Profiler.begin("name")` and `Profiler.mark("name")` in the
# application's `.qml` and `.js` files. They share the C++ zones' namespace: a
# QML name may not also be a C++ zone's, because the cache would then make one
# name two places. A QML name may be used at more than one call site: the seam
# caches one source location for each name, which is what one name one place
# means there (the calls are one zone, whatever opens them).
#
# Every name's first part is a LAYER, and the layers are a registry in this
# file (_layer_registry): a name whose layer is not in it fails, so a new
# layer is a deliberate edit with a reason beside it, not a typo that quietly
# starts a second spelling of "store" or "solver" in the traces. The registry
# has no entry nothing uses (a layer is added with its first name), except the
# ones in _reserved_layers, which the next zones to be added are named after.
#
# Three QML names are not written as a Profiler call's argument: BoardScene
# chooses the stage's zone in stageZone() and passes it as a variable. They are
# pinned as string literals (_required_qml_literals) in the QML and JavaScript
# sources, and they take part in the shape and uniqueness checks as the other
# QML names do.
#
# Scans libs/ and apps/ for `zone<"...">`, `plot("...")`, `frame_mark("...")`,
# `thread_name("...")`, and the QML files for the Profiler calls. The variant
# headers and profiling_types.hpp are skipped (they are where the calls are
# defined, and their comments quote them), and so is every line that is a
# // comment.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

set(_zone_shape "^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)+$")
set(_frame_shape "^[A-Z][A-Za-z0-9]*$")
set(_thread_shape "^[a-z][a-z0-9_-]*$")

# --- the layer registry -------------------------------------------------------
# The first part of every zone, plot and QML name. One line a layer, with what
# the layer is; keep them sorted.
set(_layer_registry
    app        # start-up of the application: apps/app/src/main.cpp
    board      # making and restoring a board: roll, seed, verify, deal, parse
    camera     # converting a camera frame
    cli        # wjs-cli
    data       # loading and saving: dictionary, dice, phases, catalogue, store, dashboard
    dice       # the dice sets
    feedback   # the cue plan for sound and haptics
    gate       # the parent gate
    input      # a traced path as the player makes it
    motion     # the device-motion processor
    path       # path validation
    phonics    # the grapheme binder
    render     # the render thread's frame (plots)
    rules      # the game rules
    scan       # reading a board from a camera frame
    scene      # the 3D board scene, C++ and QML
    scene2d    # the 2D fallback scene kept in apps/app/qml/scene/fallback: not built, so not required
    settings   # normalising the settings
    shake      # shake detection
    solo       # Solo Play's model
    solver     # the solver (and the solver app model)
    store      # the store's own parse and serialise
    ui)        # the QML screens and the GUI thread's frame
# Layers the zones now being added are named after. A name may use one before
# it is in the registry above; move it up with its first use, so that the
# registry stays free of layers nothing uses.
set(_reserved_layers
    assist     # Mode A, reading a board from a photograph
    parent     # the parent area's edits
    session    # the game session
    sound      # the sound engine
    speech)    # the speech engine

set(_failures "")
function(_fail text)
    set(_failures "${_failures}\n  ${text}" PARENT_SCOPE)
endfunction()

# Fails unless the name's first part is a registered layer; remembers which
# layers were used. A macro, so that it fails and remembers in the caller's scope.
set(_layers_used "")
macro(_check_layer where kind name)
    string(REGEX REPLACE "\\..*$" "" _layer "${name}")
    if(_layer IN_LIST _layer_registry OR _layer IN_LIST _reserved_layers)
        list(APPEND _layers_used "${_layer}")
    else()
        _fail("${where}: ${kind} '${name}' has the layer '${_layer}', which is not in "
              "_layer_registry (tests/profiling/check_zone_names.cmake); add it there "
              "with what it is, or use a layer that exists")
    endif()
endmacro()

set(_zone_names "")
set(_qml_names "")
set(_plot_names "")
set(_frame_names "")
set(_thread_names "")
set(_zones 0)
set(_qml_scopes 0)
set(_plots 0)
set(_frames 0)
set(_threads 0)

# --- the C++ calls ------------------------------------------------------------

file(GLOB_RECURSE _sources
    "${REPO_ROOT}/libs/*.cpp" "${REPO_ROOT}/libs/*.hpp" "${REPO_ROOT}/libs/*.mm"
    "${REPO_ROOT}/apps/*.cpp" "${REPO_ROOT}/apps/*.hpp" "${REPO_ROOT}/apps/*.mm")

set(_production "")
foreach(_source IN LISTS _sources)
    file(RELATIVE_PATH _relative "${REPO_ROOT}" "${_source}")
    if(NOT _relative MATCHES "/(tests|ui-tests|bench)/")
        list(APPEND _production "${_source}")
    endif()
endforeach()
set(_sources ${_production})
foreach(_source IN LISTS _sources)
    file(RELATIVE_PATH _relative "${REPO_ROOT}" "${_source}")
    if(_relative MATCHES "^libs/core/variants/" OR _relative STREQUAL "libs/core/include/wjs/profiling_types.hpp")
        continue()
    endif()

    file(STRINGS "${_source}" _lines)
    set(_number 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _number "${_number} + 1")
        if(_line MATCHES "^[ \t]*//")
            continue()
        endif()
        set(_where "${_relative}:${_number}")

        if(_line MATCHES "zone<\"([^\"]*)\">")
            set(_name "${CMAKE_MATCH_1}")
            math(EXPR _zones "${_zones} + 1")
            if(NOT _name MATCHES "${_zone_shape}")
                _fail("${_where}: zone '${_name}' is not <layer>.<thing>, lower case and dotted")
            else()
                _check_layer("${_where}" "zone" "${_name}")
            endif()
            if("${_name}" IN_LIST _zone_names)
                _fail("${_where}: zone '${_name}' is used again; one name is one place")
            endif()
            list(APPEND _zone_names "${_name}")
        elseif(_line MATCHES "plot[ \t]*\\(\"([^\"]*)\"")
            set(_name "${CMAKE_MATCH_1}")
            math(EXPR _plots "${_plots} + 1")
            if(NOT _name MATCHES "${_zone_shape}")
                _fail("${_where}: plot '${_name}' is not <layer>.<thing>, lower case and dotted")
            else()
                _check_layer("${_where}" "plot" "${_name}")
            endif()
            if("${_name}" IN_LIST _plot_names)
                _fail("${_where}: plot '${_name}' is used again; one line is one name")
            endif()
            list(APPEND _plot_names "${_name}")
        elseif(_line MATCHES "frame_mark[ \t]*\\(\"([^\"]*)\"")
            set(_name "${CMAKE_MATCH_1}")
            math(EXPR _frames "${_frames} + 1")
            if(NOT _name MATCHES "${_frame_shape}")
                _fail("${_where}: frame set '${_name}' is not a plain name")
            endif()
            if("${_name}" IN_LIST _frame_names)
                _fail("${_where}: frame set '${_name}' is used again")
            endif()
            list(APPEND _frame_names "${_name}")
        elseif(_line MATCHES "thread_name[ \t]*\\(\"([^\"]*)\"")
            set(_name "${CMAKE_MATCH_1}")
            math(EXPR _threads "${_threads} + 1")
            if(NOT _name MATCHES "${_thread_shape}")
                _fail("${_where}: thread name '${_name}' is not lower case")
            endif()
            # Not checked for repeats: "main" is the main thread of every
            # program in the tree, and each is the one thread of its program.
            list(APPEND _thread_names "${_name}")
        endif()
    endforeach()
endforeach()

# --- the QML seam -------------------------------------------------------------

file(GLOB_RECURSE _qml_sources "${REPO_ROOT}/apps/*.qml" "${REPO_ROOT}/apps/*.js"
    "${REPO_ROOT}/libs/*.qml" "${REPO_ROOT}/libs/*.js")
set(_production "")
foreach(_source IN LISTS _qml_sources)
    file(RELATIVE_PATH _relative "${REPO_ROOT}" "${_source}")
    if(NOT _relative MATCHES "/(tests|ui-tests|bench)/")
        list(APPEND _production "${_source}")
    endif()
endforeach()
set(_qml_sources ${_production})
foreach(_source IN LISTS _qml_sources)
    file(RELATIVE_PATH _relative "${REPO_ROOT}" "${_source}")
    file(STRINGS "${_source}" _lines)
    set(_number 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _number "${_number} + 1")
        if(_line MATCHES "^[ \t]*//")
            continue()
        endif()
        if(NOT _line MATCHES "Profiler[ \t]*\\.[ \t]*(begin|mark)[ \t]*\\([ \t]*\"([^\"]*)\"")
            continue()
        endif()
        set(_kind "${CMAKE_MATCH_1}")
        set(_name "${CMAKE_MATCH_2}")
        set(_where "${_relative}:${_number} (Profiler.${_kind})")
        math(EXPR _qml_scopes "${_qml_scopes} + 1")
        if(NOT _name MATCHES "${_zone_shape}")
            _fail("${_where}: '${_name}' is not <layer>.<thing>, lower case and dotted")
        else()
            _check_layer("${_where}" "QML scope" "${_name}")
        endif()
        if("${_name}" IN_LIST _zone_names)
            _fail("${_where}: '${_name}' is also a C++ zone; one name is one place")
        endif()
        list(APPEND _qml_names "${_name}")
    endforeach()
endforeach()

# --- the required names --------------------------------------------------------
# "Profiling everywhere" is held here, not by convention: the zones, plots and
# QML scopes a trace of the application's work is made of must exist, so
# deleting an instrumented call site is a failed test, as deleting a unit test
# is. A name is added here with the code it times, and removed only with a
# reason in the commit that says where the work went.
set(_required_zones
    app.fonts
    app.load_qml
    app.startup
    assist.confirm
    assist.solve
    board.deal
    board.deal_playable
    board.parse
    board.restore
    board.roll
    board.seed
    board.verify
    camera.convert_frame
    cli.run
    data.build_dashboard
    data.load_catalogue
    data.load_dictionary
    data.load_phase
    data.load_store
    data.save_store
    dice.load_sets
    feedback.play
    feedback.plan
    gate.refresh
    input.submit
    input.trace
    motion.push
    parent.edit
    path.validate
    phonics.bind
    phonics.sound_it_out
    rules.classify
    scan.box_blur
    scan.bright_mask
    scan.convert_working
    scan.decode_frame
    scan.find_blobs
    scan.find_board
    scan.process_frame
    scan.read_glyphs
    scan.read_tiles
    scan.warp_board
    scene.step
    session.finish
    settings.normalize
    shake.detect
    solo.again
    solo.blocks
    solo.help
    solo.history
    solo.path
    solo.submit
    solo.words_with_start
    solver.solve
    sound.play
    sound.sequence
    speech.say
    store.parse
    store.serialize
    store.session_record
    store.session_snapshot)
set(_required_plots
    render.frame_ms
    render.record_ms
    solver.visits
    ui.animate_ms
    ui.first_frame_ms
    ui.frame_ms
    ui.sync_ms)
set(_required_qml
    scene.play
    scene.prepare
    scene.show
    ui.screen
    ui.trace)
# The stage's zone is chosen by BoardScene.stageZone() and passed to
# Profiler.begin() as a variable, so the call is not a literal to find. The
# names are pinned as string literals in the QML and JavaScript instead.
set(_required_qml_literals
    scene.drop
    scene.settle
    scene.shake)
foreach(_name IN LISTS _required_zones)
    if(NOT _name IN_LIST _zone_names)
        _fail("the required zone '${_name}' is gone; instrumentation was removed")
    endif()
endforeach()
foreach(_name IN LISTS _required_plots)
    if(NOT _name IN_LIST _plot_names)
        _fail("the required plot '${_name}' is gone")
    endif()
endforeach()
foreach(_name IN LISTS _required_qml)
    if(NOT _name IN_LIST _qml_names)
        _fail("the required QML scope '${_name}' is gone")
    endif()
endforeach()
# The frame sets and the named threads a trace is read by: the window's frames and
# the camera's, and the threads the work runs on (the scanner's read has one of
# its own, so it is not mistaken for the window's).
foreach(_name IN ITEMS Capture UI)
    if(NOT _name IN_LIST _frame_names)
        _fail("the required frame set '${_name}' is gone")
    endif()
endforeach()
foreach(_name IN ITEMS main render scan)
    if(NOT _name IN_LIST _thread_names)
        _fail("the required thread name '${_name}' is gone")
    endif()
endforeach()

# The pinned literals: a quoted name on a line that is not a comment, in the
# QML or JavaScript. They then take part in what the other QML names do.
foreach(_name IN LISTS _required_qml_literals)
    set(_found FALSE)
    foreach(_source IN LISTS _qml_sources)
        file(STRINGS "${_source}" _lines REGEX "\"${_name}\"")
        foreach(_line IN LISTS _lines)
            if(NOT _line MATCHES "^[ 	]*//")
                set(_found TRUE)
            endif()
        endforeach()
    endforeach()
    if(NOT _found)
        _fail("the required QML scope '${_name}' is gone: no QML or JavaScript line has "
              "the literal \"${_name}\" (BoardScene.stageZone() chooses it)")
        continue()
    endif()
    if(NOT _name MATCHES "${_zone_shape}")
        _fail("'${_name}' is not <layer>.<thing>, lower case and dotted")
    else()
        _check_layer("_required_qml_literals" "QML scope" "${_name}")
    endif()
    if("${_name}" IN_LIST _zone_names)
        _fail("'${_name}' is also a C++ zone; one name is one place")
    endif()
    list(APPEND _qml_names "${_name}")
endforeach()

# The registry has nothing in it that nothing uses.
list(REMOVE_DUPLICATES _layers_used)
foreach(_layer IN LISTS _layer_registry)
    if(NOT _layer IN_LIST _layers_used)
        _fail("the layer '${_layer}' is in _layer_registry and no zone, plot or QML scope "
              "uses it; remove it, or move it to _reserved_layers if a name is coming")
    endif()
endforeach()

# --- result -------------------------------------------------------------------

if(_failures)
    message(FATAL_ERROR "Profiling names:${_failures}")
endif()
list(REMOVE_DUPLICATES _qml_names)
list(LENGTH _qml_names _qml_named)
message(STATUS
    "OK: ${_zones} C++ zones, ${_qml_scopes} QML scopes (${_qml_named} names), ${_plots} plots, "
    "${_frames} frame sets, ${_threads} thread names, each named once")
