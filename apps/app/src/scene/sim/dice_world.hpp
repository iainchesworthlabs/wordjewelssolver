// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Dice in a box, simulated as rigid bodies, from first principles.
//
// What is modelled: each die is a rigid cube of one unit's edge and one unit's
// mass (so its moment of inertia about any axis through its centre is 1/6, and
// it has no gyroscopic term). Lengths are in die edges; with a die taken as
// 20 mm, 1 g is 490.5 edges per second squared and time is real. The dice move
// in the frame of the box, which is a floor, four walls and a lid. The box is
// moved by a hand, and the dice feel it as inertia: in the box's frame a die is
// accelerated by the *negative* of what the device's accelerometer reads (so
// gravity, a tilt, a shove and a jolt are one thing), by the centrifugal,
// Coriolis and Euler terms of the gyroscope's rotation, and by nothing else.
//
// How: sequential impulses (Catto, "Iterative dynamics with temporal
// coherence"), in small fixed steps. Each step the forces change the velocities,
// the contacts between dice, and between dice and the box, are found (a
// separating-axis test with face clipping for two cubes, the eight corners
// against a plane for the box; points a little short of touching are kept, so a
// die cannot close on a surface within a step), each contact starts from the
// impulse it carried in the step before (warm starting), a few passes solve the
// velocity constraints with the impulses accumulated and clamped (a contact can
// only push; friction stays inside the cone of the normal impulse; a fast
// impact bounces by the contact's restitution), the bodies move, and what
// overlap is left is pushed out in position alone, which adds no energy.
// Steps are fixed, in double precision, and nothing is random but the seed, so
// the same inputs give the same dice on the same build.
//
// On top of the physics there is one thing that is not physical: a guide. When
// the shaking has stopped, each die is given a cell of the board to go to (the
// assignment that makes the dice travel least), and from a moment of its own it
// is hopped and pulled there by a spring while it is also turned flat and square
// to the board. The dice still collide with one another on the way. The outcome
// of a play is decided before it starts; the physics decides how it looks.

#include <array>
#include <cstdint>
#include <vector>

#include "math3.hpp"

namespace wjs::dice {

// A die's faces, numbered by the local axis they face: +x, -x, +y, -y, +z, -z.
// Each has a frame (u to the right, v up, outward normal n = u x v) in which its
// letter is drawn.
inline constexpr int kFaceCount = 6;
inline constexpr std::array<Vec3, kFaceCount> kFaceNormal{{{1.0, 0.0, 0.0},
                                                           {-1.0, 0.0, 0.0},
                                                           {0.0, 1.0, 0.0},
                                                           {0.0, -1.0, 0.0},
                                                           {0.0, 0.0, 1.0},
                                                           {0.0, 0.0, -1.0}}};
inline constexpr std::array<Vec3, kFaceCount> kFaceRight{{{0.0, 1.0, 0.0},
                                                          {0.0, 0.0, 1.0},
                                                          {0.0, 0.0, 1.0},
                                                          {1.0, 0.0, 0.0},
                                                          {1.0, 0.0, 0.0},
                                                          {0.0, 1.0, 0.0}}};
inline constexpr std::array<Vec3, kFaceCount> kFaceUp{{{0.0, 0.0, 1.0},
                                                       {0.0, 1.0, 0.0},
                                                       {1.0, 0.0, 0.0},
                                                       {0.0, 0.0, 1.0},
                                                       {0.0, 1.0, 0.0},
                                                       {1.0, 0.0, 0.0}}};

// What the world accepts. It is driven from QML (DiceSim), where a number is
// whatever a script computed, so the world takes any input and makes it safe
// rather than trusting the caller: a box outside these limits is the nearest one
// inside them (see sanitised), a reading with a number that is not finite reads
// as the box at rest in that axis, and one advance() takes at most
// kMaxAdvanceSeconds of time.
inline constexpr int kMaxBoxSide = 8;     // rows and columns, at least 1
inline constexpr double kMinPitch = 1.0;  // dice that touch, no closer
inline constexpr double kMaxPitch = 4.0;
inline constexpr double kMaxSlack = 16.0;     // at least 0
inline constexpr double kMinLidHeight = 1.0;  // a die stands one edge high
inline constexpr double kMaxLidHeight = 1000.0;
inline constexpr double kMaxForce = 100.0;          // g, per axis, either way
inline constexpr double kMaxSpin = 500.0;           // rad/s
inline constexpr double kMaxSpinChange = 100000.0;  // rad/s^2
// More than this in one advance() is a stall of the caller's, not time the dice
// should live through: a second is 1,200 steps (about 80 ms of work with 16
// dice), and an unbounded number of them would hang the caller.
inline constexpr double kMaxAdvanceSeconds = 1.0;

// The box. Rows run down the board (y up on screen), columns across it (x right).
struct Box {
    int rows = 4;
    int columns = 4;
    double pitch = 1.08;      // between the centres of neighbouring cells
    double slack = 0.6;       // room past the outer cells, on every side
    double lidHeight = 2.45;  // the lid's underside above the floor
};

// What the device reports: the accelerometer's specific force in g (gravity
// included: a device lying face up reads 0, 0, 1), and the gyroscope's rate in
// rad/s and the rate of change of that.
struct Reading {
    Vec3 force{0.0, 0.0, 1.0};
    Vec3 spin;
    Vec3 spinChange;
};

// The box as the world will use it: every number finite and within the limits
// above (one that is not finite is the default's).
[[nodiscard]] Box sanitised(const Box& box);
// The reading as the world will use it: a component that is not finite is the
// box at rest, and every one is within the limits above.
[[nodiscard]] Reading sanitised(const Reading& reading);

struct Tuning {
    double g = 9810.0 / 20.0;       // 1 g, in die edges per second squared
    double substep = 1.0 / 1200.0;  // seconds
    int velocityIterations = 6;
    int positionPasses = 3;
    double slop = 0.0005;          // overlap tolerated, in edges
    double airDrag = 0.05;         // per second
    double airSpinDrag = 0.05;     // per second
    double contactSpinDrag = 1.5;  // per second, while touching: the rolling resistance of felt
    // Restitution and friction for the surfaces a die meets.
    double restitutionFloor = 0.52;
    double restitutionWall = 0.45;
    double restitutionLid = 0.38;
    double restitutionDie = 0.60;
    double frictionFloor = 0.50;
    double frictionWall = 0.20;
    double frictionLid = 0.20;
    double frictionDie = 0.30;
    double variety = 0.15;        // dice differ in grip and bounce by up to this much
    double contactMargin = 0.03;  // points this near touching are held too (speculative contacts)
    // Below this closing speed (and what the box's own acceleration adds in a step)
    // a contact does not bounce.
    double restSpeed = 0.5;
    // The guide.
    double guideRamp = 0.04;         // seconds from nothing to full
    double guideStiffness = 1200.0;  // 1/s^2 toward the cell, in the plan
    double guideDamping = 69.0;      // 1/s
    double guideTurn = 900.0;        // 1/s^2 toward flat and square
    double guideTurnDamping = 60.0;  // 1/s
    double guideSpeedLimit = 45.0;   // edges per second
    double hopNear = 0.3;            // height of the hop at a cell's distance of nothing, edges
    double hopFar = 1.6;             // and for crossing the box
    // A die that has to be turned over to be on its face (more than this, radians) is
    // thrown this high, so that it has the time in the air to turn.
    double flipAngle = 0.5;
    double flipRise = 1.7;
    double unstickRise = 1.3;      // height of the hop of a die that is stuck behind others, edges
    double stuckAfter = 0.10;      // seconds going nowhere before it hops again
    double settleDistance = 0.03;  // within this of its cell, flat, still: it is down
    double settleAngle = 0.05;     // radians
    double settleSpeed = 0.6;      // edges per second, and radians per second
    double settleCalm = 0.04;      // seconds it has to be so for
};

enum class Surface { Floor, Wall, Lid, Die };

// A collision at speed, for sounds and haptics.
struct Impact {
    int die = 0;
    Surface surface = Surface::Floor;
    double speed = 0.0;
};

struct Die {
    Vec3 x{0.0, 0.0,
           0.5};  // centre, in the box's frame: x right, y up the board, z up from the floor
    Quat q;       // orientation in the box's frame
    Vec3 v;
    Vec3 w;                 // angular velocity relative to the box
    int home = -1;          // the cell it has been given, once there is a guide
    double guideAt = -1.0;  // time at which the guide starts for it; negative for none
    bool launched = false;  // has been hopped
    bool settled = false;   // is on its cell, flat and still, for now
    bool done = false;      // is home: fixed from now on
    double calm = 0.0;
    double stuck = 0.0;       // seconds spent on the floor, short of home, going nowhere
    double stuckLimit = 0.0;  // how long before it hops again
    double progress = -1.0;   // how far from home it was when it last got closer
    double grip = 1.0;        // how much grippier than the next die this one is
    double bounce = 1.0;      // and how much bouncier: no two dice are alike
    double hopRise = 0.0;
    // The face that is to be up when it is home (a face of kFaceNormal), or -1 for
    // whichever it is nearest to: the board decides which face of a die is up, and
    // the guide turns the die to it.
    int up = -1;
    Vec3 xPrev;
    Quat qPrev;
};

// The face of a die pointing most nearly up, and how many quarter turns its
// letter has to be turned (anticlockwise as seen from outside the face) to read
// upright, with the die as square to the board as it can be made.
struct Upright {
    int face = 4;
    int turns = 0;
};
Upright uprightOf(const Quat& q);

// The nearest of the 24 orientations that put a die flat and square to the box;
// or, with a face (0 to 5, as kFaceNormal), the nearest of the four that put that
// face up.
Quat squareTo(const Quat& q, int face = -1);

class World {
public:
    World(const Box& box, const Tuning& tuning, std::uint32_t seed);

    const Box& box() const { return box_; }
    const Tuning& tuning() const { return tuning_; }
    int count() const { return static_cast<int>(dice_.size()); }
    std::vector<Die>& dice() { return dice_; }
    const std::vector<Die>& dice() const { return dice_; }
    double time() const { return time_; }
    double halfWidth() const { return halfWidth_; }
    double halfDepth() const { return halfDepth_; }
    double lidZ() const { return lidZ_; }

    // The middle of a cell, with a die's height. Cells are numbered row-major.
    Vec3 cellCentre(int cell) const;

    // What the box is doing, from now until it is set again.
    void setReading(const Reading& reading) { reading_ = sanitised(reading); }
    // Whether the lid is on.
    void setLid(bool on) { lid_ = on; }

    // Moves time on. Whole steps are taken; what is left over is carried. A
    // time that is not finite or not positive is ignored, and one past
    // kMaxAdvanceSeconds is cut to it.
    void advance(double seconds);
    // How many whole steps have been taken since the world was made: the work a
    // play does, counted, which the scene's work budget holds (a step is
    // Tuning::substep of simulated time). Exact, and the same on every machine.
    [[nodiscard]] std::int64_t steps() const noexcept { return steps_; }

    // The face that each die is to be up when it is home (see Die::up): one number
    // for each die, -1 for none. A die whose face is given is a particular die (its
    // faces are not another die's), so it goes to its own cell and assignCells is
    // not asked.
    void setTargets(const std::vector<int>& faces);

    // Gives each die a cell, by least total distance in the plan, and returns
    // the cell of each die.
    std::vector<int> assignCells();
    // Starts the guide for die i at time() + delays[i]. Needs assignCells first.
    void guide(const std::vector<double>& delays);
    // Puts every die on its cell, square, at rest.
    void finish();
    bool allDone() const;

    // The impacts since the last call.
    std::vector<Impact> takeImpacts();

    // A checksum of every die's state, to compare replays.
    std::uint32_t digest() const;

    // For tests.
    double kineticEnergy() const;
    double potentialEnergy() const;
    // The deepest overlap between two dice or a die and the box now, in edges.
    double deepestOverlap() const;

private:
    struct Contact {
        int a = 0;
        int b = -1;    // -1: a fixed surface, whose point is in lb
        int key = -1;  // for the box: which plane and corner, to find it in the next step
        Vec3 n;        // from a toward b
        Vec3 la;       // the point on a, in a's frame
        Vec3 lb;       // the point on b, in b's frame (on a surface: in the box's)
        double restitution = 0.0;
        double friction = 0.0;
        Surface surface = Surface::Floor;
        // Worked out when the step begins.
        double separation = 0.0;  // along n; negative when they overlap
        double approach = 0.0;    // closing speed along n
        double target = 0.0;      // the closing speed the contact may have
        Vec3 rA;
        Vec3 rB;
        Vec3 t1;
        Vec3 t2;
        double massN = 0.0;
        double massT1 = 0.0;
        double massT2 = 0.0;
        // The impulses so far, along n and the two tangents.
        double jn = 0.0;
        double jt1 = 0.0;
        double jt2 = 0.0;
        Vec3 tangentImpulse;  // jt1 t1 + jt2 t2, kept for the next step
    };

    void step(double h);
    void applyForces(double h);
    void findContacts();
    void detectBox(int i);
    void collidePair(int a, int b);
    void addContact(Contact c);
    void prepareContacts(double h);
    void solveContacts();
    void moveBodies(double h);
    void correctPositions();
    void finishStep(double h);
    const Contact* previousOf(const Contact& c) const;
    double effectiveMass(const Contact& c, const Vec3& direction) const;
    Vec3 relativeVelocity(const Contact& c) const;
    double guideWeight(const Die& d) const;
    double inverseMass(int i) const;
    double inverseInertia(int i) const;
    Vec3 pointOf(int i, const Vec3& local) const;
    Vec3 pointVelocity(int i, const Vec3& r) const;
    void push(int i, const Vec3& impulse, const Vec3& r);
    void impulse(int i, const Vec3& impulse, const Vec3& r);
    double random();

    Box box_;
    Tuning tuning_;
    std::vector<Die> dice_;
    Reading reading_;
    bool lid_ = true;
    double halfWidth_ = 0.0;
    double halfDepth_ = 0.0;
    double lidZ_ = 0.0;
    double time_ = 0.0;
    double pending_ = 0.0;
    std::int64_t steps_ = 0;
    std::uint32_t rng_ = 0;
    std::vector<Contact> contacts_;
    std::vector<Contact> previous_;
    std::vector<Impact> impacts_;
};

// The assignment of n rows to n columns of a cost matrix (row-major) that costs
// least, as the column of each row (the Hungarian method).
std::vector<int> leastCostAssignment(const std::vector<double>& cost, int n);

}  // namespace wjs::dice
