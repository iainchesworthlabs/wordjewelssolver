// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import WordJewelsSolver

// A found word's jewel: a faceted shape whose size says its tier. Its colour is
// decoration and says nothing (docs/design/application.md, "Colour and
// contrast"); the facets are drawn so it is a shape and not a coloured square.
// No text is drawn into it.
Item {
    id: root

    property string tier: "spark"          // spark | power | mega
    property int colorIndex: 0
    property color color: Theme.jewelColors[colorIndex % Theme.jewelColors.length]

    readonly property real diameter: tier === "mega" ? Theme.jewelMega
                                   : tier === "power" ? Theme.jewelPower
                                   : Theme.jewelSpark

    implicitWidth: diameter
    implicitHeight: diameter

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        // The outline: a cut stone seen from above.
        ShapePath {
            strokeColor: Theme.gold
            strokeWidth: Theme.heavy
            fillColor: root.color
            joinStyle: ShapePath.RoundJoin
            startX: root.width * 0.5
            startY: root.height * 0.04
            PathLine { x: root.width * 0.94; y: root.height * 0.36 }
            PathLine { x: root.width * 0.74; y: root.height * 0.94 }
            PathLine { x: root.width * 0.26; y: root.height * 0.94 }
            PathLine { x: root.width * 0.06; y: root.height * 0.36 }
            PathLine { x: root.width * 0.5; y: root.height * 0.04 }
        }

        // The facets: a table across the top and two lines to the point.
        ShapePath {
            strokeColor: Theme.ink
            strokeWidth: Theme.hair
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: root.width * 0.06
            startY: root.height * 0.36
            PathLine { x: root.width * 0.94; y: root.height * 0.36 }
            PathMove { x: root.width * 0.34; y: root.height * 0.36 }
            PathLine { x: root.width * 0.5; y: root.height * 0.94 }
            PathLine { x: root.width * 0.66; y: root.height * 0.36 }
        }
    }
}
