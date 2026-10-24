// SPDX-License-Identifier: GPL-3.0-or-later
#include "dice_roller.hpp"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <string>
#include <string_view>
#include <utility>

namespace {

// The faces of a die the scene can draw: a cube has six.
constexpr std::size_t faces_a_die = 6;

[[nodiscard]] QString resource_of(int side) {
    return QStringLiteral(":/qt/qml/ShakePrototype/dice/%1x%1.txt").arg(side);
}

}  // namespace

DiceRoller::DiceRoller(QObject* parent) : QObject(parent) {
    for (int side = 3; side <= 6; ++side) {
        QFile file(resource_of(side));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "DiceRoller: no dice for" << side << "x" << side;
            continue;
        }
        const QByteArray bytes = file.readAll();
        auto set = wjs::DiceSet::parse(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        if (!set) {
            qWarning() << "DiceRoller: the" << side << "x" << side << "dice are not readable:"
                       << std::string(wjs::to_string(set.error())).c_str();
            continue;
        }
        sets_.emplace(side, *std::move(set));
    }
}

bool DiceRoller::hasSet(int side) const {
    return sets_.find(side) != sets_.end();
}

QVariantMap DiceRoller::roll(int side, quint32 seed) const {
    const auto found = sets_.find(side);
    if (found == sets_.end()) {
        return {};
    }
    const wjs::DiceSet& dice = found->second;
    const auto rolled = wjs::roll_with_placements(dice, seed);
    if (!rolled) {
        qWarning() << "DiceRoller: the roll was refused:"
                   << std::string(wjs::to_string(rolled.error())).c_str();
        return {};
    }

    QStringList tiles;
    QVariantList faces;
    QVariantList up;
    const std::size_t count = rolled->placements.size();
    for (std::size_t tile = 0; tile < count; ++tile) {
        const wjs::Placement& placement = rolled->placements[tile];
        const auto& die = dice.die(placement.die);
        if (die.size() != faces_a_die) {
            qWarning() << "DiceRoller: a die with" << die.size() << "faces is not a cube";
            return {};
        }
        QStringList sides;
        for (const std::string& face : die) {
            sides.append(QString::fromStdString(face));
        }
        tiles.append(QString::fromStdString(
            rolled->board.tile({tile / rolled->board.columns(), tile % rolled->board.columns()})));
        faces.append(sides);
        up.append(static_cast<int>(placement.face));
    }

    QVariantMap board;
    board.insert(QStringLiteral("rows"), static_cast<int>(rolled->board.rows()));
    board.insert(QStringLiteral("columns"), static_cast<int>(rolled->board.columns()));
    board.insert(QStringLiteral("tiles"), tiles);
    board.insert(QStringLiteral("faces"), faces);
    board.insert(QStringLiteral("up"), up);
    return board;
}
