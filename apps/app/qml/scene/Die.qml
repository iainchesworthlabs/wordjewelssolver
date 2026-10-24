// SPDX-License-Identifier: GPL-3.0-or-later
// qmllint disable import
// qmllint disable unresolved-type
// Qt's qmltypes do not name QQuick3DGeometry, the C++ base DieGeometry needs, so
// the linter cannot resolve DieGeometry's prototype or the type it is used as.
// The two directives above cover exactly those two warnings and nothing else.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D

// One die of the board scene: a lit, bevelled cube that carries its die's six
// faces, one on each side, and a flat shadow on the floor. A dead jewel is the
// same cube with the atlas's jewel on every side (FaceAtlas.qml). BoardScene
// places it every frame; nothing here moves by itself.
//
// The mesh is DieGeometry's (one for each die, so that the letter on each face
// can be turned a quarter turn at a time, for the face that ends up on top to
// read upright) and the six faces are the die's block of the board's atlas (3
// cells across and 2 down, a cell to a face), picked with the texture's own
// offset and scale, so a board of any size is one texture.
//
// The material is paper, not plastic: matte, warm, and with a little of the
// face's own cream as light, so the box's shade cannot turn the paper grey or
// blue. The scene is a QML module of its own and cannot import Theme; the
// factor's warmth is Theme.die's #FFF7E3 (1, 0.969, 0.890) at about a seventh.
Node {
    id: slot

    required property int index
    property Item atlas
    property int columns: 1
    property int rows: 1
    property real size: 80
    // The block of the atlas this die shows: its own, which is its die's, unless
    // the board gave no faces and the dice are interchangeable (then it is the
    // cell of the tile at the place the die goes to).
    property int cell: index

    // What BoardScene moves: the die itself (relative to its cell's centre on
    // the floor) and its shadow.
    property alias body: body
    property alias shadow: shadow
    property alias geometry: dieGeometry
    // The cube itself, which BoardScene turns to the face that is up (the staged
    // script's dice; the simulated ones are turned by `body`).
    property alias cube: cube

    Model {
        id: shadow

        source: "#Sphere"
        // A flat disc: the sphere squashed to a plate, and not lit.
        scale: Qt.vector3d(0.016 * slot.size, 0.016 * slot.size, 0.02)
        position: Qt.vector3d(0, 0, 0.2)
        castsShadows: false
        receivesShadows: false
        opacity: 0.0
        materials: DefaultMaterial {
            diffuseColor: "#000000"
            lighting: DefaultMaterial.NoLighting
        }
    }

    // The die's block of the board's atlas, read twice below: once as the face's
    // colour, and once as the little warm light the face gives off itself.
    Texture {
        id: faceTexture

        sourceItem: slot.atlas
        scaleU: 1 / slot.columns
        scaleV: 1 / slot.rows
        positionU: (slot.cell % slot.columns) / slot.columns
        // A sourceItem's rows count from the bottom of the texture.
        positionV: (slot.rows - 1 - Math.floor(slot.cell / slot.columns)) / slot.rows
    }

    // The flat board's cream paper: no metal, only a touch of sheen, and a face
    // that is mostly its own colour so the shade stays warm.
    PrincipledMaterial {
        id: faceMaterial

        roughness: 0.92
        metalness: 0.0
        specularAmount: 0.06
        baseColorMap: faceTexture
        // The cream of the face, at a seventh: enough to keep the paper cream
        // in the box's shade, and its ink and edge stay dark because they are
        // dark in the map. (Theme.die #FFF7E3 as the factor's warmth.)
        emissiveMap: faceTexture
        emissiveFactor: Qt.vector3d(0.15, 0.145, 0.134)
    }

    Node {
        id: body

        Model {
            id: cube

            geometry: DieGeometry {
                id: dieGeometry

                size: slot.size
            }
            castsShadows: false
            receivesShadows: false
            materials: [faceMaterial]
        }
    }
}
