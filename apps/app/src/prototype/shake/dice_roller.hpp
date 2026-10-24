// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <map>

#include "wjs/dice.hpp"

// The library's roll, in the shape the board scene takes. The prototype's
// harness uses it so that the dice on the screen are the project's own dice
// (data/dice, embedded) rolled by wjs::roll_with_placements, and not a
// stand-in: the six faces of each die and the one that is up come from the
// library, and the scene only draws them.
class DiceRoller : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit DiceRoller(QObject* parent = nullptr);

    // Whether there is a set of dice for a board of `side` by `side`.
    Q_INVOKABLE bool hasSet(int side) const;

    // The shipped dice of that size, rolled with `seed`, as
    // { rows, columns, tiles, faces, up }: for each tile, row-major, its
    // letters, the six faces of the die on it, and the index of the face that
    // is up. Empty when there is no such set or the roll is refused.
    Q_INVOKABLE QVariantMap roll(int side, quint32 seed) const;

private:
    std::map<int, wjs::DiceSet> sets_;
};
