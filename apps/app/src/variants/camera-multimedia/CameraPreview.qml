// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtMultimedia

// The live view of Mode A's camera, shown only while ScanModel says the camera
// is running. The picture is the device's own frames, drawn by Qt Multimedia's
// VideoOutput; nothing here records, keeps or sends one (docs/design/scanner.md,
// "Privacy, built in and checked").
//
// The sink is Qt Multimedia's, and CameraSource deliberately names no Qt type in
// its header, so the hand-over is a call rather than a property binding: the
// source sets this item's videoSink. A camera that fails to start never reaches
// this file; the ready screen says why and offers the typed board instead.
VideoOutput {
    id: preview

    // The ScanModel's CameraSource. `var` because this module's code is the same
    // one a kit without Qt Multimedia compiles, where the source has no sink.
    required property var camera

    objectName: "cameraPreview"
    fillMode: VideoOutput.PreserveAspectCrop

    Component.onCompleted: {
        if (preview.camera) {
            preview.camera.attachPreview(preview)
        }
    }
}
