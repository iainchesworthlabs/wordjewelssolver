// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// The size matrix over the kit's own components (KitGallery) and a Sheet over a
// screen (SheetGallery): 900x620, 900x1200, 1024x768, 2400x1200 and 150% text.
// Every interactive item is at least 48 dp each way, wholly inside the window,
// and carries no truncated label. A screen stream adds its own suite beside this
// one - copy it, name its screen as the subject - or adds the screen here.
SizeMatrixCase {
    name: "SizeMatrixKit"
    subjects: [galleryComponent, sheetComponent]

    Component { id: galleryComponent; KitGallery { } }
    Component { id: sheetComponent; SheetGallery { } }
}
