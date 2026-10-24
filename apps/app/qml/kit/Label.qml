// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// The one text of the application. A screen says what a piece of text IS (its
// `role`) and the kit decides how it looks: face, size, weight, line height,
// tracking and colour all come from Theme, scale with the parent's Text size
// setting, and cannot be forgotten - a bare Text falls back to the system face,
// which is how the Dashboard once came to mix Segoe UI with Archivo.
//
//   display  Archivo ExtraBold 40, tight           the home hero, the cleared summary
//   title    Archivo SemiBold 32                    a screen's title
//   heading  Archivo SemiBold 20                    a card's or section's title
//   body     Archivo 16                             a parent's sentence
//   label    Archivo SemiBold 16                    a parent's button
//   child    Andika Bold 24                         a child's button or panel title
//   word     Andika 24                              a found word, a hint
//   mono     IBM Plex Mono 12, capitals, 0.08 em    counts, board size, section eyebrow
//   caption  Archivo 14                             a hint, a time stamp
//
// `learning: true` sets a body, label or caption in Andika, the face for
// beginning readers, for a sentence a child reads. A decorative Label (an icon's
// twin, a repeated name) is kept out of the accessibility tree.
Text {
    id: root

    property string role: "body"
    property bool learning: false
    property bool decorative: false
    // Letter spacing in em; the role's own unless set. Positive tracking is
    // ignored in a right-to-left language.
    property real tracking: spec.tracking
    // The size in pixels; set this, not font.pixelSize, to override the role's (the
    // tracking is in em of it, and reading font.pixelSize back would loop).
    property real size: spec.size
    // Line height as a multiple of the size. Qt's proportional mode multiplies the
    // font's own line spacing (Andika's is about 1.4 em), so the height is fixed in
    // pixels instead: 24 px of type at 1.5 is a 36 px line.
    property real lineRatio: spec.line

    readonly property var spec: specFor(root.role, root.learning)
    readonly property bool isHeading: root.role === "display" || root.role === "title" || root.role === "heading"

    function specFor(role, learning) {
        switch (role) {
        case "display":
            return {face: Theme.uiFont, size: Theme.displaySize, weight: Font.ExtraBold, line: Theme.lineDisplay,
                    tracking: Theme.trackDisplay, caps: Font.MixedCase, color: Theme.ink};
        case "title":
            return {face: Theme.uiFont, size: Theme.titleSize, weight: Font.DemiBold, line: Theme.lineTitle,
                    tracking: Theme.trackNone, caps: Font.MixedCase, color: Theme.ink};
        case "heading":
            return {face: Theme.uiFont, size: Theme.headingSize, weight: Font.DemiBold, line: Theme.lineHeading,
                    tracking: Theme.trackNone, caps: Font.MixedCase, color: Theme.ink};
        case "label":
            return {face: learning ? Theme.fontFamily : Theme.uiFont, size: Theme.labelSize, weight: Font.DemiBold,
                    line: Theme.lineHeading, tracking: Theme.trackNone, caps: Font.MixedCase, color: Theme.ink};
        case "child":
            return {face: Theme.fontFamily, size: Theme.childLabelSize, weight: Font.Bold, line: Theme.lineChild,
                    tracking: Theme.trackNone, caps: Font.MixedCase, color: Theme.ink};
        case "word":
            return {face: Theme.fontFamily, size: Theme.wordSize, weight: Font.Normal, line: Theme.lineWord,
                    tracking: Theme.trackNone, caps: Font.MixedCase, color: Theme.ink};
        case "mono":
            return {face: Theme.monoFont, size: Theme.monoSize, weight: Font.Medium, line: Theme.lineMono,
                    tracking: Theme.trackMono, caps: Font.AllUppercase, color: Theme.inkSecondary};
        case "caption":
            return {face: learning ? Theme.fontFamily : Theme.uiFont, size: Theme.captionSize, weight: Font.Normal,
                    line: Theme.lineCaption, tracking: Theme.trackNone, caps: Font.MixedCase,
                    color: Theme.inkSecondary};
        default:
            return {face: learning ? Theme.fontFamily : Theme.uiFont, size: Theme.bodySize, weight: Font.Normal,
                    line: Theme.lineBody, tracking: Theme.trackNone, caps: Font.MixedCase, color: Theme.ink};
        }
    }

    color: spec.color
    font.family: spec.face
    font.pixelSize: root.size
    font.weight: spec.weight
    font.capitalization: spec.caps
    font.letterSpacing: (root.tracking > 0 && Theme.rightToLeft) ? 0 : root.tracking * root.size
    lineHeight: root.lineRatio * root.size
    lineHeightMode: Text.FixedHeight

    Accessible.ignored: root.decorative
    Accessible.role: root.isHeading ? Accessible.Heading : Accessible.StaticText
    Accessible.name: root.text
}
