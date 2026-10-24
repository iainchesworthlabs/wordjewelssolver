// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The small amount of vector and rotation arithmetic the dice simulation needs.
// Double precision throughout: the simulation is a fixed-step integration that
// is replayed, and rounding that differs between runs would show as a different
// shake.

#include <array>
#include <cmath>

namespace wjs::dice {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
constexpr Vec3 operator-(const Vec3& a) {
    return {-a.x, -a.y, -a.z};
}
constexpr Vec3 operator*(const Vec3& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
constexpr Vec3 operator*(double s, const Vec3& a) {
    return {a.x * s, a.y * s, a.z * s};
}
constexpr Vec3 operator/(const Vec3& a, double s) {
    return {a.x / s, a.y / s, a.z / s};
}
constexpr Vec3& operator+=(Vec3& a, const Vec3& b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}
constexpr Vec3& operator-=(Vec3& a, const Vec3& b) {
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}
constexpr double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const Vec3& a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalized(const Vec3& a) {
    const double l = length(a);
    return l > 0.0 ? a / l : Vec3{};
}

// A unit quaternion: the rotation of a body's own frame into the box's.
struct Quat {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

constexpr Quat operator*(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
constexpr Quat conjugate(const Quat& q) {
    return {q.w, -q.x, -q.y, -q.z};
}
inline Quat normalized(const Quat& q) {
    const double l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return l > 0.0 ? Quat{q.w / l, q.x / l, q.y / l, q.z / l} : Quat{};
}

// v rotated by q.
inline Vec3 rotate(const Quat& q, const Vec3& v) {
    // With u the vector part and t = 2 u x v, the result is v + w t + u x t, written out
    // by component: the same operations in the same order as the vector form, without the copies
    // of every intermediate vector, which a sanitised build makes calls of.
    const double tx = 2.0 * (q.y * v.z - q.z * v.y);
    const double ty = 2.0 * (q.z * v.x - q.x * v.z);
    const double tz = 2.0 * (q.x * v.y - q.y * v.x);
    return {v.x + tx * q.w + (q.y * tz - q.z * ty), v.y + ty * q.w + (q.z * tx - q.x * tz),
            v.z + tz * q.w + (q.x * ty - q.y * tx)};
}

// q turned by the rotation vector `theta` (an axis times an angle), about the
// box's axes: the exact rotation, so that a spinning body keeps its spin.
inline Quat turned(const Quat& q, const Vec3& theta) {
    const double angle = length(theta);
    if (angle < 1e-12) {
        return q;
    }
    const double s = std::sin(0.5 * angle) / angle;
    return normalized(Quat{std::cos(0.5 * angle), theta.x * s, theta.y * s, theta.z * s} * q);
}

// The rotation vector (axis times angle) of a rotation, the short way round.
inline Vec3 rotationVector(Quat q) {
    if (q.w < 0.0) {
        q = Quat{-q.w, -q.x, -q.y, -q.z};
    }
    const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-12) {
        return Vec3{2.0 * q.x, 2.0 * q.y, 2.0 * q.z};
    }
    const double scale = 2.0 * std::atan2(s, q.w) / s;
    return Vec3{q.x * scale, q.y * scale, q.z * scale};
}

// The rotation that has the given rows: it takes a vector v to
// (row0 . v, row1 . v, row2 . v). The rows must be orthonormal and right-handed.
inline Quat fromRows(const Vec3& row0, const Vec3& row1, const Vec3& row2) {
    // Shepperd's method, on the matrix whose rows these are.
    const double m00 = row0.x, m01 = row0.y, m02 = row0.z;
    const double m10 = row1.x, m11 = row1.y, m12 = row1.z;
    const double m20 = row2.x, m21 = row2.y, m22 = row2.z;
    const double trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q = {0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q = {(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s};
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s};
    }
    return normalized(q);
}

// The three axes of a body: the columns of its rotation matrix.
inline std::array<Vec3, 3> axesOf(const Quat& q) {
    return {rotate(q, Vec3{1.0, 0.0, 0.0}), rotate(q, Vec3{0.0, 1.0, 0.0}),
            rotate(q, Vec3{0.0, 0.0, 1.0})};
}

}  // namespace wjs::dice
