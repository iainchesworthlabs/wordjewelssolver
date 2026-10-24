// SPDX-License-Identifier: GPL-3.0-or-later
import QtMultimedia

// Not a screen and never loaded. It exists so that Qt's QML import scanner sees
// that this application imports QtMultimedia when the camera is built in:
// CameraPreview.qml lives in the Family Assistant's static module
// (apps/app/src/variants/camera-multimedia), and the scanner reads the application's
// own QML and resources, not a linked module's. Without this, a deployed copy
// has Qt6Multimedia.dll but no qml/QtMultimedia to load, and the live view fails
// with "module QtMultimedia is not installed" (docs/building.md, "The Qt Quick
// window"). The file is a resource only when the kit has the module; the
// camera-none variant does not compile it.
QtObject {}
