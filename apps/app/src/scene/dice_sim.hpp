// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

#include <memory>

#include "dice_world.hpp"

// The dice simulation (sim/dice_world.hpp) as an object for QML. The scene's
// script (ShakeScript.js) holds one and drives it: it sets what the device
// reads, advances time, and asks where every die is.
//
// Everything crosses the boundary as plain numbers: poses() is seven numbers a
// die (x, y, z, then the quaternion w, x, y, z), in die edges.
class DiceSim : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit DiceSim(QObject* parent = nullptr);

    // A new box of rows x columns dice at rest on their cells. Lengths in die
    // edges: pitch between cell centres, slack past the outer cells, the
    // underside of the lid.
    Q_INVOKABLE void reset(int rows, int columns, double pitch, double slack, double lidHeight,
                           int seed);

    // What the device reports until the next call: the accelerometer in g
    // (gravity included), the gyroscope in rad/s and its rate of change.
    Q_INVOKABLE void setReading(double fx, double fy, double fz, double wx, double wy, double wz,
                                double ax, double ay, double az);
    Q_INVOKABLE void advance(double seconds);
    Q_INVOKABLE void setLid(bool on);

    // How many whole simulation steps the world has taken since reset(): the
    // work of a play, counted, for the scene's work budget and the trace's
    // scene.step value.
    [[nodiscard]] Q_INVOKABLE int steps() const;

    // The face of each die that is to be up when it is home (0 to 5, in the order
    // of the simulation's faces: +x, -x, +y, -y, +z, -z), or -1: one number for each
    // die. A die with a face to be up is its own die and goes to its own cell.
    Q_INVOKABLE void setTargets(const QList<int>& faces);

    // The cell each die is to go to, by least total distance. Then the guide:
    // die i starts for its cell delays[i] seconds from now.
    Q_INVOKABLE QList<int> assignCells();
    Q_INVOKABLE void guide(const QList<double>& delays);
    // Everything home, square and still.
    Q_INVOKABLE void finish();
    Q_INVOKABLE bool allDone() const;
    Q_INVOKABLE int doneCount() const;

    Q_INVOKABLE QList<double> poses() const;
    // For each die: the face that is up, the quarter turns its letter needs,
    // and whether it is home (1) or not (0).
    Q_INVOKABLE QList<int> faces() const;
    // The impacts since the last call: three numbers each: the die, the surface
    // (0 floor, 1 wall, 2 lid, 3 another die) and the closing speed.
    Q_INVOKABLE QList<double> takeImpacts();
    Q_INVOKABLE double time() const;
    Q_INVOKABLE QString digest() const;

private:
    std::unique_ptr<wjs::dice::World> world_;
};
