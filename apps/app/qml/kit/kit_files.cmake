# SPDX-License-Identifier: GPL-3.0-or-later
# The kit's QML files, in one place. The application's QML module, the Qt Quick
# Test module (apps/app/ui-tests), the scan preview and the parent preview (tests/) each
# embed the kit, and a kit component that uses another (AppButton uses Label,
# FocusRing and Icon) fails to load in any of them that lacks it; with one list
# a new component is added once.
#
#   include("${PROJECT_SOURCE_DIR}/apps/app/qml/kit/kit_files.cmake")
#   foreach(_file IN LISTS WJS_KIT_QML_FILES) ... qml/kit/${_file}
#
# The singletons are the tokens (Theme), the sound and touch seam (Feedback) and
# the navigation state (Navigation).

set(WJS_KIT_QML_FILES
    ActionBar.qml
    AppBar.qml
    AppButton.qml
    BoardTray.qml
    Card.qml
    Eyebrow.qml
    Feedback.qml
    FocusRing.qml
    HoldButton.qml
    Icon.qml
    Jewel.qml
    JewelBox.qml
    Label.qml
    ModeCard.qml
    Navigation.qml
    SegmentedChoice.qml
    Sheet.qml
    TabletArt.qml
    Theme.qml
    TileFace.qml
    Wordmark.qml)

set(WJS_KIT_QML_SINGLETONS
    Feedback.qml
    Navigation.qml
    Theme.qml)
