// SPDX-License-Identifier: GPL-3.0-or-later
// TEST SUPPORT, not part of the application: the scene module's Profiler
// singleton (apps/app/src/scene/profiler.*) with no profiler behind it.
//
// The screens under test call Profiler.begin()/end() around a moment, and the
// real singleton is in WordJewelsSolver.Scene because the scene and the
// application both import it. The Quick Test suites replace that whole module
// with this stub (apps/app/ui-tests/scene_stub), so this file gives the calls an
// implementation that does nothing: QML has no preprocessor, and no suite is
// about a Tracy zone (apps/app/tests/unit/shake_script_test.cpp and the profiling tier are).
pragma Singleton
import QtQuick

QtObject {
    function begin(name) {
    }

    function end() {
    }

    function value(number) {
    }

    function mark(name) {
    }
}
