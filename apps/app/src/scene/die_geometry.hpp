// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QQmlEngine>
#include <QtQuick3D/QQuick3DGeometry>

#include <array>

// The mesh of one die: a cube with a small bevel on every edge and corner, six
// faces that each carry a letter of their own, and a quarter-turn setting for
// each face so the letter on whichever face ends up on top can be made to read
// upright. Positions are in the scene's units (the die's edge is `size`).
//
// The texture the die is given is its block of the board's atlas, 3 cells across
// and 2 down (FaceAtlas.qml), and face f is the cell in column f % 3 and row f / 3
// from the top: face f here is face f of the library's die. A face's letter is
// drawn in the frame (right, up) that sim/dice_world.hpp gives it; turning it by
// k quarter turns anticlockwise (seen from outside the face) is `setTurns(face,
// k)`. The bevels and corners sample the border of the cell of a face they touch,
// so they are the dark edge of the tile.
class DieGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(qreal bevel READ bevel WRITE setBevel NOTIFY bevelChanged)

public:
    explicit DieGeometry(QQuick3DObject* parent = nullptr);

    qreal size() const { return size_; }
    void setSize(qreal size);
    // The bevel's width as a fraction of the die's edge.
    qreal bevel() const { return bevel_; }
    void setBevel(qreal bevel);

    Q_INVOKABLE void setTurns(int face, int turns);
    Q_INVOKABLE void clearTurns();
    Q_INVOKABLE int turns(int face) const;

signals:
    void sizeChanged();
    void bevelChanged();

private:
    void rebuild();

    qreal size_ = 80.0;
    qreal bevel_ = 0.07;
    std::array<int, 6> turns_{};
};
