// SPDX-License-Identifier: GPL-3.0-or-later
#include "dice_sim.hpp"

#include <QString>

#include <cstddef>
#include <vector>

#include "wjs/profiling.hpp"

DiceSim::DiceSim(QObject* parent) : QObject(parent) {
    reset(4, 4, 1.08, 0.6, 2.42, 1);
}

void DiceSim::reset(int rows, int columns, double pitch, double slack, double lidHeight, int seed) {
    wjs::dice::Box box;
    box.rows = rows;
    box.columns = columns;
    box.pitch = pitch;
    box.slack = slack;
    box.lidHeight = lidHeight;
    world_ = std::make_unique<wjs::dice::World>(box, wjs::dice::Tuning{},
                                                static_cast<std::uint32_t>(seed));
}

void DiceSim::setReading(double fx, double fy, double fz, double wx, double wy, double wz,
                         double ax, double ay, double az) {
    wjs::dice::Reading reading;
    reading.force = wjs::dice::Vec3{fx, fy, fz};
    reading.spin = wjs::dice::Vec3{wx, wy, wz};
    reading.spinChange = wjs::dice::Vec3{ax, ay, az};
    world_->setReading(reading);
}

void DiceSim::advance(double seconds) {
    // One zone per call, not per step: the script calls this once a frame (or
    // once a device sample), and the world takes the whole steps inside. The
    // value is the steps of this call, which is the work the frame paid for.
    auto zone = wjs::profiling::zone<"scene.step">();
    const std::int64_t before = world_->steps();
    world_->advance(seconds);
    zone.Value(static_cast<std::uint64_t>(world_->steps() - before));
}

void DiceSim::setLid(bool on) {
    world_->setLid(on);
}

int DiceSim::steps() const {
    return static_cast<int>(world_->steps());
}

void DiceSim::setTargets(const QList<int>& faces) {
    world_->setTargets(std::vector<int>(faces.begin(), faces.end()));
}

QList<int> DiceSim::assignCells() {
    const std::vector<int> cells = world_->assignCells();
    return QList<int>(cells.begin(), cells.end());
}

void DiceSim::guide(const QList<double>& delays) {
    world_->guide(std::vector<double>(delays.begin(), delays.end()));
}

void DiceSim::finish() {
    world_->finish();
}

bool DiceSim::allDone() const {
    return world_->allDone();
}

int DiceSim::doneCount() const {
    int done = 0;
    for (const wjs::dice::Die& d : world_->dice()) {
        done += (d.done || d.settled) ? 1 : 0;
    }
    return done;
}

QList<double> DiceSim::poses() const {
    QList<double> out;
    out.reserve(world_->count() * 7);
    for (const wjs::dice::Die& d : world_->dice()) {
        out << d.x.x << d.x.y << d.x.z << d.q.w << d.q.x << d.q.y << d.q.z;
    }
    return out;
}

QList<int> DiceSim::faces() const {
    QList<int> out;
    out.reserve(world_->count() * 3);
    for (const wjs::dice::Die& d : world_->dice()) {
        const wjs::dice::Upright up = wjs::dice::uprightOf(d.q);
        out << up.face << up.turns << ((d.done || d.settled) ? 1 : 0);
    }
    return out;
}

QList<double> DiceSim::takeImpacts() {
    QList<double> out;
    for (const wjs::dice::Impact& impact : world_->takeImpacts()) {
        out << impact.die << static_cast<int>(impact.surface) << impact.speed;
    }
    return out;
}

double DiceSim::time() const {
    return world_->time();
}

QString DiceSim::digest() const {
    return QString::number(world_->digest(), 16);
}
