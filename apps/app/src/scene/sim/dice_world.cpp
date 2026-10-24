// SPDX-License-Identifier: GPL-3.0-or-later
#include "dice_world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace wjs::dice {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();
// A cube's centre to its furthest corner.
constexpr double kReach = 0.8660254037844386;
// How fast a closing contact has to be for the impact to be reported.
constexpr double kImpactSpeed = 3.0;

using Axes = std::array<Vec3, 3>;

double radiusAlong(const Axes& e, const Vec3& axis) {
    return 0.5 *
           (std::abs(dot(e[0], axis)) + std::abs(dot(e[1], axis)) + std::abs(dot(e[2], axis)));
}

// The rotation vector that takes q to target.
Vec3 rotationError(const Quat& target, const Quat& q) {
    return rotationVector(target * conjugate(q));
}

struct Point {
    Vec3 p;
    double depth = 0.0;
};

// Keeps the points with dot(normal, p) <= offset, cutting the polygon at the plane.
int clipPolygon(const Vec3* in, int n, const Vec3& normal, double offset, Vec3* out) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const Vec3 cur = in[i];
        const Vec3 next = in[(i + 1) % n];
        const double sc = offset - dot(normal, cur);
        const double sn = offset - dot(normal, next);
        if (sc >= 0.0) {
            out[m++] = cur;
        }
        if ((sc >= 0.0) != (sn >= 0.0)) {
            const double t = sc / (sc - sn);
            out[m++] = cur + (next - cur) * t;
        }
    }
    return m;
}

// At most four of the points, spread as far apart as they can be, the deepest first.
int reducePoints(const Point* in, int n, Point* out) {
    if (n <= 4) {
        for (int i = 0; i < n; ++i) {
            out[i] = in[i];
        }
        return n;
    }
    int deepest = 0;
    for (int i = 1; i < n; ++i) {
        if (in[i].depth > in[deepest].depth) {
            deepest = i;
        }
    }
    out[0] = in[deepest];
    int m = 1;
    while (m < 4) {
        int best = -1;
        double bestSpread = -1.0;
        for (int i = 0; i < n; ++i) {
            double nearest = kInfinity;
            for (int k = 0; k < m; ++k) {
                const Vec3 delta = in[i].p - out[k].p;
                nearest = std::min(nearest, dot(delta, delta));
            }
            if (nearest > bestSpread) {
                bestSpread = nearest;
                best = i;
            }
        }
        out[m++] = in[best];
    }
    return m;
}

Vec3 toLocal(const Die& d, const Vec3& p) {
    return rotate(conjugate(d.q), p - d.x);
}

// The velocity of the point of a die at r from its centre: v + w x r.
Vec3 pointVelocityOf(const Die& d, const Vec3& r) {
    return {d.v.x + (d.w.y * r.z - d.w.z * r.y), d.v.y + (d.w.z * r.x - d.w.x * r.z),
            d.v.z + (d.w.x * r.y - d.w.y * r.x)};
}

// An impulse at r from the centre of a die that is not home yet changes its velocity
// and its spin (its mass is 1 and its inverse moment of inertia 6). Written out by
// component, as the solver does it for every contact on every pass, and in a
// sanitised build every copy of a vector is a call.
void kick(Die& d, const Vec3& impulse, const Vec3& r) {
    if (d.done) {
        return;
    }
    d.v.x += impulse.x;
    d.v.y += impulse.y;
    d.v.z += impulse.z;
    d.w.x += (r.y * impulse.z - r.z * impulse.y) * 6.0;
    d.w.y += (r.z * impulse.x - r.x * impulse.z) * 6.0;
    d.w.z += (r.x * impulse.y - r.y * impulse.x) * 6.0;
}

}  // namespace

// ---- Faces and orientations ----------------------------------------------------------

Upright uprightOf(const Quat& q) {
    // The face's own "up" is taken inside the loop, at the index the loop is
    // on, and not after it by the index that was kept: MSVC's analyzer cannot
    // carry "less than six" through the comparison and reports C28020 at an
    // array access it is plainly inside.
    std::size_t face = 0;
    Vec3 faceUp = kFaceUp[0];
    double best = -kInfinity;
    for (std::size_t f = 0; f < kFaceNormal.size(); ++f) {
        const double up = rotate(q, kFaceNormal[f]).z;
        if (up > best) {
            best = up;
            face = f;
            faceUp = kFaceUp[f];
        }
    }
    // Which way the face's own "up" points on the board, to the nearest side.
    const Vec3 v = rotate(q, faceUp);
    int turns = 0;
    if (std::abs(v.x) > std::abs(v.y)) {
        turns = v.x > 0.0 ? 1 : 3;
    } else {
        turns = v.y > 0.0 ? 0 : 2;
    }
    return {static_cast<int>(face), turns};
}

Quat squareTo(const Quat& q, int face) {
    const Axes e = axesOf(q);
    int k = 0;
    if (face >= 0 && face < kFaceCount) {
        // The axis of that face is the one that is to be vertical.
        k = face / 2;
    } else {
        for (int i = 1; i < 3; ++i) {
            if (std::abs(e[static_cast<std::size_t>(i)].z) >
                std::abs(e[static_cast<std::size_t>(k)].z)) {
                k = i;
            }
        }
    }
    const int m1 = (k + 1) % 3;
    const int m2 = (k + 2) % 3;
    const Vec3 c1 = e[static_cast<std::size_t>(m1)];
    const Vec3 c2 = e[static_cast<std::size_t>(m2)];
    const bool firstAlongX = std::abs(c1.x) >= std::abs(c2.x);
    const int m = firstAlongX ? m1 : m2;
    const int n = firstAlongX ? m2 : m1;
    // Which way up the axis points: the face's side of it, if a face is asked for.
    const double up = (face >= 0 && face < kFaceCount)
                          ? (face % 2 == 0 ? 1.0 : -1.0)
                          : (e[static_cast<std::size_t>(k)].z >= 0.0 ? 1.0 : -1.0);
    const double along = e[static_cast<std::size_t>(m)].x >= 0.0 ? 1.0 : -1.0;
    std::array<Vec3, 3> column{};
    column[static_cast<std::size_t>(k)] = Vec3{0.0, 0.0, up};
    column[static_cast<std::size_t>(m)] = Vec3{along, 0.0, 0.0};
    const Vec3 ck = column[static_cast<std::size_t>(k)];
    const Vec3 cm = column[static_cast<std::size_t>(m)];
    // The third column makes the frame right-handed: (k, m, n) is an even
    // permutation of (0, 1, 2) when m follows k.
    column[static_cast<std::size_t>(n)] = (m == m1) ? cross(ck, cm) : cross(cm, ck);
    return fromRows(Vec3{column[0].x, column[1].x, column[2].x},
                    Vec3{column[0].y, column[1].y, column[2].y},
                    Vec3{column[0].z, column[1].z, column[2].z});
}

// ---- The assignment ------------------------------------------------------------------

std::vector<int> leastCostAssignment(const std::vector<double>& cost, int n) {
    const auto size = static_cast<std::size_t>(n);
    std::vector<double> u(size + 1, 0.0);
    std::vector<double> v(size + 1, 0.0);
    std::vector<int> p(size + 1, 0);
    std::vector<int> way(size + 1, 0);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(size + 1, kInfinity);
        std::vector<char> used(size + 1, 0);
        do {
            used[static_cast<std::size_t>(j0)] = 1;
            const int i0 = p[static_cast<std::size_t>(j0)];
            double delta = kInfinity;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                const auto sj = static_cast<std::size_t>(j);
                if (used[sj] != 0) {
                    continue;
                }
                const double cur = cost[static_cast<std::size_t>((i0 - 1) * n + (j - 1))] -
                                   u[static_cast<std::size_t>(i0)] - v[sj];
                if (cur < minv[sj]) {
                    minv[sj] = cur;
                    way[sj] = j0;
                }
                if (minv[sj] < delta) {
                    delta = minv[sj];
                    j1 = j;
                }
            }
            for (int j = 0; j <= n; ++j) {
                const auto sj = static_cast<std::size_t>(j);
                if (used[sj] != 0) {
                    u[static_cast<std::size_t>(p[sj])] += delta;
                    v[sj] -= delta;
                } else {
                    minv[sj] -= delta;
                }
            }
            j0 = j1;
        } while (p[static_cast<std::size_t>(j0)] != 0);
        do {
            const int j1 = way[static_cast<std::size_t>(j0)];
            p[static_cast<std::size_t>(j0)] = p[static_cast<std::size_t>(j1)];
            j0 = j1;
        } while (j0 != 0);
    }
    std::vector<int> columnOfRow(size, 0);
    for (int j = 1; j <= n; ++j) {
        columnOfRow[static_cast<std::size_t>(p[static_cast<std::size_t>(j)] - 1)] = j - 1;
    }
    return columnOfRow;
}

// ---- The world -----------------------------------------------------------------------

namespace {

// `value` within [low, high], or `fallback` if it is not a number.
double finiteWithin(double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

}  // namespace

Box sanitised(const Box& box) {
    const Box rest;
    Box out;
    out.rows = std::clamp(box.rows, 1, kMaxBoxSide);
    out.columns = std::clamp(box.columns, 1, kMaxBoxSide);
    out.pitch = finiteWithin(box.pitch, kMinPitch, kMaxPitch, rest.pitch);
    out.slack = finiteWithin(box.slack, 0.0, kMaxSlack, rest.slack);
    out.lidHeight = finiteWithin(box.lidHeight, kMinLidHeight, kMaxLidHeight, rest.lidHeight);
    return out;
}

Reading sanitised(const Reading& reading) {
    const Reading rest;
    const auto vector = [](Vec3 value, Vec3 at_rest, double limit) {
        return Vec3{finiteWithin(value.x, -limit, limit, at_rest.x),
                    finiteWithin(value.y, -limit, limit, at_rest.y),
                    finiteWithin(value.z, -limit, limit, at_rest.z)};
    };
    Reading out;
    out.force = vector(reading.force, rest.force, kMaxForce);
    out.spin = vector(reading.spin, rest.spin, kMaxSpin);
    out.spinChange = vector(reading.spinChange, rest.spinChange, kMaxSpinChange);
    return out;
}

World::World(const Box& requested, const Tuning& tuning, std::uint32_t seed)
    : box_(sanitised(requested)), tuning_(tuning) {
    const Box& box = box_;
    halfWidth_ = ((box.columns - 1) * box.pitch + 1.0) / 2.0 + box.slack;
    halfDepth_ = ((box.rows - 1) * box.pitch + 1.0) / 2.0 + box.slack;
    lidZ_ = box.lidHeight;
    rng_ = seed * 2654435761U + 0x9E3779B9U;
    dice_.resize(static_cast<std::size_t>(box.rows * box.columns));
    for (int i = 0; i < count(); ++i) {
        Die& d = dice_[static_cast<std::size_t>(i)];
        d.x = cellCentre(i);
        d.home = i;
        d.grip = 1.0 + tuning.variety * (2.0 * random() - 1.0);
        d.bounce = 1.0 + tuning.variety * (2.0 * random() - 1.0);
        d.xPrev = d.x;
        d.qPrev = d.q;
    }
}

double World::random() {
    // mulberry32
    rng_ += 0x6D2B79F5U;
    std::uint32_t t = rng_;
    t = (t ^ (t >> 15)) * (t | 1U);
    t ^= t + (t ^ (t >> 7)) * (t | 61U);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

Vec3 World::cellCentre(int cell) const {
    const int row = cell / box_.columns;
    const int column = cell % box_.columns;
    return Vec3{(column - (box_.columns - 1) / 2.0) * box_.pitch,
                ((box_.rows - 1) / 2.0 - row) * box_.pitch, 0.5};
}

void World::advance(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return;
    }
    pending_ += std::min(seconds, kMaxAdvanceSeconds);
    const double h = tuning_.substep;
    while (pending_ >= h - 1e-12) {
        pending_ -= h;
        step(h);
    }
    // The step is taken a hair early (the 1e-12), which can leave a hair less
    // than nothing.
    if (pending_ < 0.0) {
        pending_ = 0.0;
    }
}

std::vector<int> World::assignCells() {
    const int n = count();
    std::vector<double> cost(static_cast<std::size_t>(n * n));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const Vec3 c = cellCentre(j);
            const Vec3& x = dice_[static_cast<std::size_t>(i)].x;
            const double dx = c.x - x.x;
            const double dy = c.y - x.y;
            cost[static_cast<std::size_t>(i * n + j)] = dx * dx + dy * dy;
        }
    }
    const std::vector<int> cells = leastCostAssignment(cost, n);
    for (int i = 0; i < n; ++i) {
        dice_[static_cast<std::size_t>(i)].home = cells[static_cast<std::size_t>(i)];
    }
    return cells;
}

void World::guide(const std::vector<double>& delays) {
    // The dice that have furthest to go start first, so that they cross the box
    // while it is still open; the delays given are used in order.
    const int n = count();
    std::vector<double> distance(static_cast<std::size_t>(n), 0.0);
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const Die& d = dice_[static_cast<std::size_t>(i)];
        const Vec3 home = cellCentre(d.home);
        const double dx = home.x - d.x.x;
        const double dy = home.y - d.x.y;
        distance[static_cast<std::size_t>(i)] = std::sqrt(dx * dx + dy * dy);
        order[static_cast<std::size_t>(i)] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&distance](int a, int b) {
        return distance[static_cast<std::size_t>(a)] > distance[static_cast<std::size_t>(b)];
    });
    std::vector<double> sorted = delays;
    std::sort(sorted.begin(), sorted.end());
    for (int rank = 0; rank < n; ++rank) {
        const auto i = static_cast<std::size_t>(order[static_cast<std::size_t>(rank)]);
        Die& d = dice_[i];
        const double delay = static_cast<std::size_t>(rank) < sorted.size()
                                 ? sorted[static_cast<std::size_t>(rank)]
                                 : 0.0;
        d.guideAt = time_ + delay;
        d.launched = false;
        d.done = false;
        d.calm = 0.0;
        d.stuck = 0.0;
        d.progress = -1.0;
        d.hopRise =
            std::min(tuning_.hopFar, tuning_.hopNear + 0.55 * distance[i]) * (0.9 + 0.2 * random());
    }
}

void World::setTargets(const std::vector<int>& faces) {
    for (std::size_t i = 0; i < dice_.size(); ++i) {
        dice_[i].up = i < faces.size() && faces[i] >= 0 && faces[i] < kFaceCount ? faces[i] : -1;
    }
}

void World::finish() {
    for (Die& d : dice_) {
        if (d.home < 0) {
            d.home = 0;
        }
        d.x = cellCentre(d.home);
        d.q = squareTo(d.q, d.up);
        d.v = Vec3{};
        d.w = Vec3{};
        d.xPrev = d.x;
        d.qPrev = d.q;
        d.done = true;
    }
}

bool World::allDone() const {
    return std::all_of(dice_.begin(), dice_.end(), [](const Die& d) { return d.done; });
}

std::vector<Impact> World::takeImpacts() {
    std::vector<Impact> taken;
    taken.swap(impacts_);
    return taken;
}

double World::guideWeight(const Die& d) const {
    if (d.guideAt < 0.0 || time_ < d.guideAt) {
        return 0.0;
    }
    return std::min(1.0, (time_ - d.guideAt) / tuning_.guideRamp);
}

double World::inverseMass(int i) const {
    return (i < 0 || dice_[static_cast<std::size_t>(i)].done) ? 0.0 : 1.0;
}

double World::inverseInertia(int i) const {
    return (i < 0 || dice_[static_cast<std::size_t>(i)].done) ? 0.0 : 6.0;
}

Vec3 World::pointOf(int i, const Vec3& local) const {
    const Die& d = dice_[static_cast<std::size_t>(i)];
    return d.x + rotate(d.q, local);
}

Vec3 World::pointVelocity(int i, const Vec3& r) const {
    return pointVelocityOf(dice_[static_cast<std::size_t>(i)], r);
}

void World::push(int i, const Vec3& impulseValue, const Vec3& r) {
    if (i < 0) {
        return;
    }
    Die& d = dice_[static_cast<std::size_t>(i)];
    if (d.done) {
        return;
    }
    d.x.x += impulseValue.x;
    d.x.y += impulseValue.y;
    d.x.z += impulseValue.z;
    d.q = turned(d.q, Vec3{(r.y * impulseValue.z - r.z * impulseValue.y) * 6.0,
                           (r.z * impulseValue.x - r.x * impulseValue.z) * 6.0,
                           (r.x * impulseValue.y - r.y * impulseValue.x) * 6.0});
}

void World::impulse(int i, const Vec3& impulseValue, const Vec3& r) {
    if (i < 0) {
        return;
    }
    kick(dice_[static_cast<std::size_t>(i)], impulseValue, r);
}

// ---- A step --------------------------------------------------------------------------
//
// Sequential impulses (Catto): the velocities are changed by the forces, the
// contacts are found, each is given the impulse it carried in the step before
// (warm starting), the velocity constraints are iterated with the impulses
// accumulated and clamped (a contact only pushes; friction is held inside its
// cone of the normal impulse), the bodies move, and what overlap is left is
// pushed out in position alone, which adds no energy.

void World::step(double h) {
    applyForces(h);
    findContacts();
    prepareContacts(h);
    solveContacts();
    moveBodies(h);
    correctPositions();
    finishStep(h);
}

void World::applyForces(double h) {
    const Vec3 f = reading_.force;
    const Vec3 spin = reading_.spin;
    const Vec3 change = reading_.spinChange;
    for (Die& d : dice_) {
        if (d.done) {
            continue;
        }
        // What a die feels in the box's frame: minus what the accelerometer
        // reads, the centrifugal, Coriolis and Euler terms of the box's turning,
        // and a little air.
        Vec3 a = -f * tuning_.g - cross(spin, cross(spin, d.x)) - 2.0 * cross(spin, d.v) -
                 cross(change, d.x) - d.v * tuning_.airDrag;
        // A body's own turning is unchanged in the inertial frame; relative to the
        // box it is minus the box's change of turning, and carried round by it.
        Vec3 alpha = -change - cross(spin, d.w) - d.w * tuning_.airSpinDrag;

        const double weight = guideWeight(d);
        if (weight > 0.0) {
            if (!d.launched) {
                d.launched = true;
                double rise = d.hopRise;
                // A die that is to be on a face other than the one it is on has to
                // be turned over, which it does in the air: it is thrown higher.
                if (d.up >= 0 &&
                    length(rotationError(squareTo(d.q, d.up), d.q)) > tuning_.flipAngle) {
                    rise = std::max(rise, tuning_.flipRise);
                }
                d.v.z += std::sqrt(2.0 * tuning_.g * rise);
            }
            const Vec3 home = cellCentre(d.home);
            a.x +=
                weight * (tuning_.guideStiffness * (home.x - d.x.x) - tuning_.guideDamping * d.v.x);
            a.y +=
                weight * (tuning_.guideStiffness * (home.y - d.x.y) - tuning_.guideDamping * d.v.y);
            const Vec3 error = rotationError(squareTo(d.q, d.up), d.q);
            alpha += weight * (error * tuning_.guideTurn - d.w * tuning_.guideTurnDamping);
        }
        d.v += a * h;
        d.w += alpha * h;
        if (weight > 0.0) {
            const double slide = std::sqrt(d.v.x * d.v.x + d.v.y * d.v.y);
            if (slide > tuning_.guideSpeedLimit) {
                d.v.x *= tuning_.guideSpeedLimit / slide;
                d.v.y *= tuning_.guideSpeedLimit / slide;
            }
        }
    }
}

void World::moveBodies(double h) {
    for (Die& d : dice_) {
        d.xPrev = d.x;
        d.qPrev = d.q;
        if (d.done) {
            continue;
        }
        d.x += d.v * h;
        d.q = turned(d.q, d.w * h);
    }
}

void World::addContact(Contact c) {
    double guided = guideWeight(dice_[static_cast<std::size_t>(c.a)]);
    const Vec3 pA = pointOf(c.a, c.la);
    Vec3 pB = c.lb;
    if (c.b >= 0) {
        guided = std::max(guided, guideWeight(dice_[static_cast<std::size_t>(c.b)]));
        pB = pointOf(c.b, c.lb);
    }
    // No two dice are alike; and a die being guided slides: it is not held where
    // it is by the felt.
    double grip = dice_[static_cast<std::size_t>(c.a)].grip;
    double bounce = dice_[static_cast<std::size_t>(c.a)].bounce;
    if (c.b >= 0) {
        grip = 0.5 * (grip + dice_[static_cast<std::size_t>(c.b)].grip);
        bounce = 0.5 * (bounce + dice_[static_cast<std::size_t>(c.b)].bounce);
    }
    c.friction *= grip * (1.0 - 0.98 * guided);
    c.restitution *= bounce;
    c.separation = dot(pB - pA, c.n);
    contacts_.push_back(c);
}

void World::detectBox(int i) {
    const Die& d = dice_[static_cast<std::size_t>(i)];
    const Axes e = axesOf(d.q);
    const double within = kReach + tuning_.contactMargin;
    const bool low = d.x.z - within < 0.0;
    const bool high = lid_ && d.x.z + within > lidZ_;
    const bool left = d.x.x - within < -halfWidth_;
    const bool right = d.x.x + within > halfWidth_;
    const bool back = d.x.y - within < -halfDepth_;
    const bool front = d.x.y + within > halfDepth_;
    if (!(low || high || left || right || back || front)) {
        return;
    }
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 p = d.x + (e[0] * ((corner & 1) != 0 ? 0.5 : -0.5) +
                              e[1] * ((corner & 2) != 0 ? 0.5 : -0.5) +
                              e[2] * ((corner & 4) != 0 ? 0.5 : -0.5));
        const auto add = [&](int plane, Vec3 into, double depth, Surface surface,
                             double restitution, double friction) {
            if (depth <= -tuning_.contactMargin) {
                return;
            }
            Contact c;
            c.a = i;
            c.b = -1;
            c.key = plane * 8 + corner;
            c.n = into;
            c.la = toLocal(d, p);
            c.lb = p - into * depth;
            c.restitution = restitution;
            c.friction = friction;
            c.surface = surface;
            addContact(c);
        };
        if (low) {
            add(0, Vec3{0.0, 0.0, -1.0}, -p.z, Surface::Floor, tuning_.restitutionFloor,
                tuning_.frictionFloor);
        }
        if (high) {
            add(1, Vec3{0.0, 0.0, 1.0}, p.z - lidZ_, Surface::Lid, tuning_.restitutionLid,
                tuning_.frictionLid);
        }
        if (right) {
            add(2, Vec3{1.0, 0.0, 0.0}, p.x - halfWidth_, Surface::Wall, tuning_.restitutionWall,
                tuning_.frictionWall);
        }
        if (left) {
            add(3, Vec3{-1.0, 0.0, 0.0}, -halfWidth_ - p.x, Surface::Wall, tuning_.restitutionWall,
                tuning_.frictionWall);
        }
        if (front) {
            add(4, Vec3{0.0, 1.0, 0.0}, p.y - halfDepth_, Surface::Wall, tuning_.restitutionWall,
                tuning_.frictionWall);
        }
        if (back) {
            add(5, Vec3{0.0, -1.0, 0.0}, -halfDepth_ - p.y, Surface::Wall, tuning_.restitutionWall,
                tuning_.frictionWall);
        }
    }
}

void World::findContacts() {
    contacts_.clear();
    const int n = count();
    for (int i = 0; i < n; ++i) {
        if (!dice_[static_cast<std::size_t>(i)].done) {
            detectBox(i);
        }
    }
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (dice_[static_cast<std::size_t>(i)].done &&
                dice_[static_cast<std::size_t>(j)].done) {
                continue;
            }
            collidePair(i, j);
        }
    }
}

void World::collidePair(int ia, int ib) {
    const Die& A = dice_[static_cast<std::size_t>(ia)];
    const Die& B = dice_[static_cast<std::size_t>(ib)];
    const Vec3 d = B.x - A.x;
    const double margin = tuning_.contactMargin;
    const double reach = 2.0 * kReach + margin;
    if (dot(d, d) > reach * reach) {
        return;
    }
    const Axes a = axesOf(A.q);
    const Axes b = axesOf(B.q);

    // The separating-axis test: the six face normals and the nine edge crosses.
    double faceDepth = kInfinity;
    int faceAxis = 0;
    for (int k = 0; k < 6; ++k) {
        const Vec3 axis =
            k < 3 ? a[static_cast<std::size_t>(k)] : b[static_cast<std::size_t>(k - 3)];
        const double depth = radiusAlong(a, axis) + radiusAlong(b, axis) - std::abs(dot(d, axis));
        if (depth <= -margin) {
            return;
        }
        if (depth < faceDepth) {
            faceDepth = depth;
            faceAxis = k;
        }
    }
    double edgeDepth = kInfinity;
    int edgeA = 0;
    int edgeB = 0;
    Vec3 edgeAxis;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Vec3 axis = cross(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(j)]);
            const double len = length(axis);
            if (len < 1e-4) {
                continue;
            }
            axis = axis / len;
            const double depth =
                radiusAlong(a, axis) + radiusAlong(b, axis) - std::abs(dot(d, axis));
            if (depth <= -margin) {
                return;
            }
            if (depth < edgeDepth) {
                edgeDepth = depth;
                edgeA = i;
                edgeB = j;
                edgeAxis = axis;
            }
        }
    }

    Contact base;
    base.restitution = tuning_.restitutionDie;
    base.friction = tuning_.frictionDie;
    base.surface = Surface::Die;
    base.a = ia;
    base.b = ib;

    if (edgeDepth < faceDepth - 0.05 * std::abs(faceDepth) - 1e-6) {
        // Two edges cross: one contact, between the closest points of the two edges.
        const Vec3 n = dot(d, edgeAxis) >= 0.0 ? edgeAxis : -edgeAxis;
        Vec3 pa = A.x;
        Vec3 pb = B.x;
        for (int k = 0; k < 3; ++k) {
            if (k != edgeA) {
                const Vec3 e = a[static_cast<std::size_t>(k)];
                pa += e * (dot(e, n) >= 0.0 ? 0.5 : -0.5);
            }
            if (k != edgeB) {
                const Vec3 e = b[static_cast<std::size_t>(k)];
                pb += e * (dot(e, n) >= 0.0 ? -0.5 : 0.5);
            }
        }
        const Vec3 u = a[static_cast<std::size_t>(edgeA)];
        const Vec3 v = b[static_cast<std::size_t>(edgeB)];
        const Vec3 w0 = pa - pb;
        const double bb = dot(u, v);
        const double dd = dot(u, w0);
        const double ee = dot(v, w0);
        const double denominator = 1.0 - bb * bb;
        if (denominator < 1e-9) {
            return;
        }
        const double s = std::clamp((bb * ee - dd) / denominator, -0.5, 0.5);
        const double t = std::clamp((ee - bb * dd) / denominator, -0.5, 0.5);
        Contact c = base;
        c.n = n;
        c.la = toLocal(A, pa + u * s);
        c.lb = toLocal(B, pb + v * t);
        addContact(c);
        return;
    }

    // A face of one die against a face of the other: clip the other's nearest
    // face to the side planes of this one's.
    const bool referenceIsA = faceAxis < 3;
    const Axes& referenceAxes = referenceIsA ? a : b;
    const Axes& incidentAxes = referenceIsA ? b : a;
    const Die& reference = referenceIsA ? A : B;
    const Die& incident = referenceIsA ? B : A;
    const int referenceIndex = referenceIsA ? faceAxis : faceAxis - 3;
    const Vec3 axis = referenceAxes[static_cast<std::size_t>(referenceIndex)];
    const Vec3 outward = dot(incident.x - reference.x, axis) >= 0.0 ? axis : -axis;

    int incidentIndex = 0;
    for (int j = 1; j < 3; ++j) {
        if (std::abs(dot(incidentAxes[static_cast<std::size_t>(j)], outward)) >
            std::abs(dot(incidentAxes[static_cast<std::size_t>(incidentIndex)], outward))) {
            incidentIndex = j;
        }
    }
    const Vec3 incidentAxis = incidentAxes[static_cast<std::size_t>(incidentIndex)];
    const Vec3 incidentNormal = dot(incidentAxis, outward) > 0.0 ? -incidentAxis : incidentAxis;
    const Vec3 su = incidentAxes[static_cast<std::size_t>((incidentIndex + 1) % 3)] * 0.5;
    const Vec3 sv = incidentAxes[static_cast<std::size_t>((incidentIndex + 2) % 3)] * 0.5;
    const Vec3 faceCentre = incident.x + incidentNormal * 0.5;
    std::array<Vec3, 16> bufferA{};
    std::array<Vec3, 16> bufferB{};
    bufferA[0] = faceCentre + su + sv;
    bufferA[1] = faceCentre - su + sv;
    bufferA[2] = faceCentre - su - sv;
    bufferA[3] = faceCentre + su - sv;
    int polygonSize = 4;
    Vec3* from = bufferA.data();
    Vec3* to = bufferB.data();
    for (int k = 1; k <= 2 && polygonSize > 0; ++k) {
        const Vec3 side = referenceAxes[static_cast<std::size_t>((referenceIndex + k) % 3)];
        const double centre = dot(reference.x, side);
        polygonSize = clipPolygon(from, polygonSize, side, 0.5 + centre, to);
        std::swap(from, to);
        if (polygonSize > 0) {
            polygonSize = clipPolygon(from, polygonSize, -side, 0.5 - centre, to);
            std::swap(from, to);
        }
    }
    const Vec3 referenceCentre = reference.x + outward * 0.5;
    std::array<Point, 16> found{};
    int foundCount = 0;
    for (int i = 0; i < polygonSize; ++i) {
        const double depth = dot(referenceCentre - from[i], outward);
        if (depth > -margin) {
            found[static_cast<std::size_t>(foundCount++)] = {from[i], depth};
        }
    }
    std::array<Point, 4> kept{};
    const int keptCount = reducePoints(found.data(), foundCount, kept.data());
    for (int i = 0; i < keptCount; ++i) {
        const Point& point = kept[static_cast<std::size_t>(i)];
        Contact c = base;
        const Vec3 onSurface = point.p + outward * point.depth;
        if (referenceIsA) {
            c.n = outward;
            c.la = toLocal(A, onSurface);
            c.lb = toLocal(B, point.p);
        } else {
            c.n = -outward;
            c.la = toLocal(A, point.p);
            c.lb = toLocal(B, onSurface);
        }
        addContact(c);
    }
}

double World::effectiveMass(const Contact& c, const Vec3& direction) const {
    const Vec3 ra = cross(c.rA, direction);
    const Vec3 rb = cross(c.rB, direction);
    const double k = inverseMass(c.a) + inverseInertia(c.a) * dot(ra, ra) + inverseMass(c.b) +
                     inverseInertia(c.b) * dot(rb, rb);
    return k > 0.0 ? 1.0 / k : 0.0;
}

Vec3 World::relativeVelocity(const Contact& c) const {
    Vec3 u = pointVelocity(c.a, c.rA);
    if (c.b >= 0) {
        u -= pointVelocity(c.b, c.rB);
    }
    return u;
}

const World::Contact* World::previousOf(const Contact& c) const {
    // previous_ is in the order findContacts made it: the contacts with the box
    // by die, then those between dice by (a, b), each pair's together. So the
    // contacts of c's pair (or of c's die with the box) are one run, found by
    // bisection and walked in order, as a walk of the whole list would meet them.
    const auto before = [](const Contact& x, const Contact& y) {
        if ((x.b >= 0) != (y.b >= 0)) {
            return x.b < 0;
        }
        if (x.a != y.a) {
            return x.a < y.a;
        }
        return x.b < y.b;
    };
    const Contact* best = nullptr;
    double nearest = 0.05;
    for (auto it = std::lower_bound(previous_.begin(), previous_.end(), c, before);
         it != previous_.end() && it->a == c.a && it->b == c.b; ++it) {
        const Contact& old = *it;
        if (c.key >= 0) {
            if (old.key == c.key) {
                return &old;
            }
            continue;
        }
        const Vec3 da = old.la - c.la;
        const Vec3 db = old.lb - c.lb;
        const double distance = dot(da, da) + dot(db, db);
        if (distance < nearest) {
            nearest = distance;
            best = &old;
        }
    }
    return best;
}

void World::prepareContacts(double h) {
    const std::size_t impactsBefore = impacts_.size();
    // A contact that is pressed by the box's own acceleration is not an impact.
    const double threshold = tuning_.restSpeed + 2.0 * tuning_.g * length(reading_.force) * h;
    for (Contact& c : contacts_) {
        c.rA = rotate(dice_[static_cast<std::size_t>(c.a)].q, c.la);
        c.rB = c.b >= 0 ? rotate(dice_[static_cast<std::size_t>(c.b)].q, c.lb) : Vec3{};
        c.approach = dot(relativeVelocity(c), c.n);
        const Vec3 helper = std::abs(c.n.x) < 0.7 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
        c.t1 = normalized(cross(c.n, helper));
        c.t2 = cross(c.n, c.t1);
        c.massN = effectiveMass(c, c.n);
        c.massT1 = effectiveMass(c, c.t1);
        c.massT2 = effectiveMass(c, c.t2);

        // How fast the contact may close: as far as it is from touching in a
        // step; and if it is a fast impact, it rebounds.
        c.target = c.separation > 0.0 ? c.separation / h : 0.0;
        if (c.approach > threshold && c.separation <= c.approach * h) {
            c.target = std::min(c.target, -c.restitution * c.approach);
            bool reported = false;
            for (std::size_t k = impactsBefore; k < impacts_.size(); ++k) {
                if (impacts_[k].die == c.a && impacts_[k].surface == c.surface) {
                    impacts_[k].speed = std::max(impacts_[k].speed, c.approach);
                    reported = true;
                    break;
                }
            }
            if (!reported && c.approach > kImpactSpeed) {
                impacts_.push_back({c.a, c.surface, c.approach});
            }
        }
    }

    // Start from what each contact carried in the step before. This is done
    // once every closing speed above has been taken, so that one contact's
    // carried impulse is not another's impact.
    for (Contact& c : contacts_) {
        if (const Contact* old = previousOf(c)) {
            c.jn = old->jn;
            c.jt1 = dot(old->tangentImpulse, c.t1);
            c.jt2 = dot(old->tangentImpulse, c.t2);
            const Vec3 carried = c.n * c.jn + c.t1 * c.jt1 + c.t2 * c.jt2;
            impulse(c.a, -carried, c.rA);
            impulse(c.b, carried, c.rB);
        }
    }
}

void World::solveContacts() {
    const int n = static_cast<int>(contacts_.size());
    for (int iteration = 0; iteration < tuning_.velocityIterations; ++iteration) {
        for (int k = 0; k < n; ++k) {
            Contact& c = contacts_[static_cast<std::size_t>((iteration & 1) != 0 ? n - 1 - k : k)];
            // The two bodies, looked up once for the contact's three constraints.
            Die& bodyA = dice_[static_cast<std::size_t>(c.a)];
            Die* bodyB = c.b >= 0 ? &dice_[static_cast<std::size_t>(c.b)] : nullptr;
            const auto closingOf = [&](const Vec3& direction) {
                const Vec3 u = pointVelocityOf(bodyA, c.rA);
                if (bodyB == nullptr) {
                    return dot(u, direction);
                }
                const Vec3 other = pointVelocityOf(*bodyB, c.rB);
                return dot(Vec3{u.x - other.x, u.y - other.y, u.z - other.z}, direction);
            };
            const auto applyTo = [&](const Vec3& j) {
                kick(bodyA, Vec3{-j.x, -j.y, -j.z}, c.rA);
                if (bodyB != nullptr) {
                    kick(*bodyB, j, c.rB);
                }
            };

            // Along the surface, up to the cone of the normal impulse.
            const double limit = c.friction * c.jn;
            const auto slide = [&](const Vec3& t, double mass, double& accumulated) {
                const double change = closingOf(t) * mass;
                const double next = std::clamp(accumulated + change, -limit, limit);
                const double delta = next - accumulated;
                accumulated = next;
                applyTo(Vec3{t.x * delta, t.y * delta, t.z * delta});
            };
            slide(c.t1, c.massT1, c.jt1);
            slide(c.t2, c.massT2, c.jt2);

            // Along the normal: a contact only pushes.
            const double closing = closingOf(c.n);
            const double next = std::max(c.jn + (closing - c.target) * c.massN, 0.0);
            const double delta = next - c.jn;
            c.jn = next;
            applyTo(Vec3{c.n.x * delta, c.n.y * delta, c.n.z * delta});
        }
    }
}

// What overlap remains is pushed out in position only.
void World::correctPositions() {
    for (int pass = 0; pass < tuning_.positionPasses; ++pass) {
        for (const Contact& c : contacts_) {
            // The two bodies, looked up once for the contact.
            Die& bodyA = dice_[static_cast<std::size_t>(c.a)];
            Die* bodyB = c.b >= 0 ? &dice_[static_cast<std::size_t>(c.b)] : nullptr;
            const Vec3 rA = rotate(bodyA.q, c.la);
            const Vec3 pA{bodyA.x.x + rA.x, bodyA.x.y + rA.y, bodyA.x.z + rA.z};
            Vec3 rB;
            Vec3 pB = c.lb;
            if (bodyB != nullptr) {
                rB = rotate(bodyB->q, c.lb);
                pB = Vec3{bodyB->x.x + rB.x, bodyB->x.y + rB.y, bodyB->x.z + rB.z};
            }
            const double over =
                -dot(Vec3{pB.x - pA.x, pB.y - pA.y, pB.z - pA.z}, c.n) - tuning_.slop;
            if (over <= 0.0) {
                continue;
            }
            const Vec3 rAn = cross(rA, c.n);
            const Vec3 rBn = cross(rB, c.n);
            // A die that is home does not move (it weighs infinitely much); a surface neither.
            const double w = (bodyA.done ? 0.0 : 1.0) + (bodyA.done ? 0.0 : 6.0) * dot(rAn, rAn) +
                             (bodyB == nullptr || bodyB->done ? 0.0 : 1.0) +
                             (bodyB == nullptr || bodyB->done ? 0.0 : 6.0) * dot(rBn, rBn);
            if (w <= 0.0) {
                continue;
            }
            const double along = over / w;
            const Vec3 p{c.n.x * along, c.n.y * along, c.n.z * along};
            push(c.a, Vec3{-p.x, -p.y, -p.z}, rA);
            push(c.b, p, rB);
        }
    }
}

void World::finishStep(double h) {
    // Rolling resistance of the felt, on a die that is touching something.
    const double spinDrag = 1.0 / (1.0 + tuning_.contactSpinDrag * h);
    std::vector<char> touching(dice_.size(), 0);
    for (Contact& c : contacts_) {
        c.tangentImpulse = c.t1 * c.jt1 + c.t2 * c.jt2;
        if (c.separation < 0.002) {
            touching[static_cast<std::size_t>(c.a)] = 1;
            if (c.b >= 0) {
                touching[static_cast<std::size_t>(c.b)] = 1;
            }
        }
    }
    for (std::size_t i = 0; i < dice_.size(); ++i) {
        if (touching[i] != 0 && !dice_[i].done) {
            dice_[i].w = dice_[i].w * spinDrag;
        }
    }
    previous_.swap(contacts_);
    contacts_.clear();

    // A die that is held up short of its cell by others that want its place (two
    // that have to change sides, say) hops over them. So does one that has landed
    // on the wrong face: a die lying flat is held to the floor by its weight more
    // firmly than the guide turns it, so it is thrown again to turn over in the air.
    for (Die& d : dice_) {
        if (d.done || !d.launched || guideWeight(d) < 1.0) {
            continue;
        }
        const Vec3 home = cellCentre(d.home);
        const double toHome =
            std::sqrt((home.x - d.x.x) * (home.x - d.x.x) + (home.y - d.x.y) * (home.y - d.x.y));
        const bool wrongFace =
            d.up >= 0 && length(rotationError(squareTo(d.q, d.up), d.q)) > tuning_.flipAngle;
        // On the floor, short of home or on the wrong face, and not getting any closer.
        if ((toHome > 0.08 || wrongFace) && d.x.z < 0.7) {
            if (d.progress < 0.0 || toHome < d.progress - 0.05) {
                d.progress = toHome;
                d.stuck = 0.0;
            }
            d.stuck += h;
            // Two dice that are stuck against one another must not hop together,
            // or they land as they were: each waits its own time.
            if (d.stuckLimit <= 0.0) {
                d.stuckLimit = tuning_.stuckAfter * (0.7 + 0.9 * random());
            }
            if (d.stuck > d.stuckLimit) {
                d.stuck = 0.0;
                d.progress = -1.0;
                d.stuckLimit = tuning_.stuckAfter * (0.7 + 0.9 * random());
                const double rise = wrongFace ? tuning_.flipRise : tuning_.unstickRise;
                d.v.z += std::sqrt(2.0 * tuning_.g * rise * (0.85 + 0.4 * random()));
            }
        } else {
            d.stuck = 0.0;
            d.progress = -1.0;
        }
    }

    // A die that has come home is flat and still on its cell. When every die is,
    // they are put exactly there and stay: nothing is fixed before then, so that
    // a die can still make room for one that has not got in.
    bool all = true;
    for (Die& d : dice_) {
        if (d.done) {
            continue;
        }
        bool settled = false;
        if (d.launched && guideWeight(d) >= 1.0) {
            const Vec3 home = cellCentre(d.home);
            const double dx = home.x - d.x.x;
            const double dy = home.y - d.x.y;
            const double dz = 0.5 - d.x.z;
            const Vec3 error = rotationError(squareTo(d.q, d.up), d.q);
            const bool near = std::sqrt(dx * dx + dy * dy) < tuning_.settleDistance &&
                              std::abs(dz) < tuning_.settleDistance &&
                              length(error) < tuning_.settleAngle;
            const bool still =
                length(d.v) < tuning_.settleSpeed && length(d.w) < tuning_.settleSpeed;
            d.calm = (near && still) ? d.calm + h : 0.0;
            settled = d.calm >= tuning_.settleCalm;
        }
        d.settled = settled;
        all = all && settled;
    }
    if (all && !dice_.empty()) {
        finish();
    }
    steps_ += 1;
    time_ = static_cast<double>(steps_) * h;
}

// ---- Checks --------------------------------------------------------------------------

std::uint32_t World::digest() const {
    std::uint32_t sum = 0x811C9DC5U;
    const auto mix = [&sum](double value) {
        const auto scaled = static_cast<std::int64_t>(std::llround(value * 1e4));
        for (int byte = 0; byte < 8; ++byte) {
            sum = (sum ^ static_cast<std::uint32_t>(
                             (static_cast<std::uint64_t>(scaled) >> (8 * byte)) & 0xFFU)) *
                  0x01000193U;
        }
    };
    for (const Die& d : dice_) {
        mix(d.x.x);
        mix(d.x.y);
        mix(d.x.z);
        mix(d.q.w);
        mix(d.q.x);
        mix(d.q.y);
        mix(d.q.z);
        mix(d.v.x);
        mix(d.v.y);
        mix(d.v.z);
    }
    return sum;
}

double World::kineticEnergy() const {
    double energy = 0.0;
    for (const Die& d : dice_) {
        energy += 0.5 * dot(d.v, d.v) + 0.5 * dot(d.w, d.w) / 6.0;
    }
    return energy;
}

double World::potentialEnergy() const {
    double energy = 0.0;
    for (const Die& d : dice_) {
        energy += tuning_.g * d.x.z;
    }
    return energy;
}

double World::deepestOverlap() const {
    World copy(*this);
    copy.findContacts();
    double deepest = 0.0;
    for (const Contact& c : copy.contacts_) {
        deepest = std::max(deepest, -c.separation);
    }
    return deepest;
}

}  // namespace wjs::dice
