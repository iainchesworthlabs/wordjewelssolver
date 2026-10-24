// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// The keyboard focus ring: 2 px of the focus colour, a 3 px gap outside the
// thing it rings, drawn by the thing itself (AppButton, ModeCard, HoldButton) so
// every focusable object shows focus the same way. Put it as a child, set
// `radius` to the parent's radius, and bind `visible` to "focused and not by the
// pointer".
Rectangle {
    id: ring

    // The parent's own radius; the ring is concentric with it.
    property real inner: Theme.radius

    anchors.fill: parent
    anchors.margins: -(Theme.focusGap + Theme.focusWidth)
    radius: ring.inner + Theme.focusGap + Theme.focusWidth
    color: "transparent"
    border.width: Theme.focusWidth
    border.color: Theme.focusRing
    visible: false
    // Not a thing to tap or to read out.
    Accessible.ignored: true
}
