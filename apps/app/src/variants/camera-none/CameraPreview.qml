// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The live view a kit without Qt Multimedia has: nothing to draw. It is the same
// type name and the same property as the multimedia CameraPreview.qml, chosen by
// CMake (apps/app/CMakeLists.txt), so ScanScreen.qml is one file in either build.
// The preview is never shown here: CameraSource reports "no_camera", so the ready
// screen says so and "Type in your board" is the way on (docs/design/game.md,
// "No camera").
Item {
    // The ScanModel's CameraSource. Present so the screen's binding is the same
    // in both variants; there is no sink to hand over.
    required property var camera

    objectName: "cameraPreview"
}
