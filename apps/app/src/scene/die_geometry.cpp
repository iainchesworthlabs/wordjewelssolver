// SPDX-License-Identifier: GPL-3.0-or-later
#include "die_geometry.hpp"

#include <QByteArray>
#include <QVector3D>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dice_world.hpp"

namespace {

struct Vertex {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
};

using wjs::dice::Vec3;

// Where the border of a letter's cell begins, as a fraction of the cell
// (FaceAtlas.qml: a 10 pixel border on a 128 pixel cell), and a point inside it.
constexpr double kBorder = 10.0 / 128.0;
constexpr double kInBorder = 0.03;

// The die's block of the atlas holds its six faces, 3 cells across and 2 down
// (FaceAtlas.qml). A point of face f's cell, (cu, cv) from 0 to 1 with v up, is
// this far across the block and up it. An item's texture counts rows from the
// bottom, so the top row of cells is the upper half.
constexpr int kBlockColumns = 3;
constexpr int kBlockRows = 2;

double blockU(int face, double cu) {
    return (face % kBlockColumns + cu) / kBlockColumns;
}

double blockV(int face, double cv) {
    return (kBlockRows - 1 - face / kBlockColumns + cv) / kBlockRows;
}

// The face whose outward normal is n (one of the six axes).
int faceOf(Vec3 n) {
    for (int f = 0; f < wjs::dice::kFaceCount; ++f) {
        if (wjs::dice::dot(n, wjs::dice::kFaceNormal[static_cast<std::size_t>(f)]) > 0.5) {
            return f;
        }
    }
    return 0;
}

}  // namespace

DieGeometry::DieGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {
    rebuild();
}

void DieGeometry::setSize(qreal size) {
    if (qFuzzyCompare(size_, size)) {
        return;
    }
    size_ = size;
    rebuild();
    emit sizeChanged();
}

void DieGeometry::setBevel(qreal bevel) {
    if (qFuzzyCompare(bevel_, bevel)) {
        return;
    }
    bevel_ = bevel;
    rebuild();
    emit bevelChanged();
}

void DieGeometry::setTurns(int face, int turns) {
    if (face < 0 || face >= wjs::dice::kFaceCount) {
        return;
    }
    const int wrapped = ((turns % 4) + 4) % 4;
    if (turns_[static_cast<std::size_t>(face)] == wrapped) {
        return;
    }
    turns_[static_cast<std::size_t>(face)] = wrapped;
    rebuild();
}

void DieGeometry::clearTurns() {
    if (std::all_of(turns_.begin(), turns_.end(), [](int t) { return t == 0; })) {
        return;
    }
    turns_.fill(0);
    rebuild();
}

int DieGeometry::turns(int face) const {
    return face < 0 || face >= wjs::dice::kFaceCount ? 0 : turns_[static_cast<std::size_t>(face)];
}

void DieGeometry::rebuild() {
    const double edge = 0.5 * size_;
    const double flat = (0.5 - bevel_) * size_;
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;

    const auto add = [&vertices](Vec3 p, Vec3 n, double u, double v) {
        vertices.push_back({static_cast<float>(p.x), static_cast<float>(p.y),
                            static_cast<float>(p.z), static_cast<float>(n.x),
                            static_cast<float>(n.y), static_cast<float>(n.z), static_cast<float>(u),
                            static_cast<float>(v)});
        return static_cast<std::uint16_t>(vertices.size() - 1);
    };
    const auto triangle = [&indices](std::uint16_t a, std::uint16_t b, std::uint16_t c) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    };
    // Four corners of a quad, turned so that it faces `outward`.
    const auto quad = [&](const std::uint16_t (&corner)[4], const Vec3(&position)[4],
                          Vec3 outward) {
        const Vec3 geometric =
            wjs::dice::cross(position[1] - position[0], position[2] - position[0]);
        if (wjs::dice::dot(geometric, outward) >= 0.0) {
            triangle(corner[0], corner[1], corner[2]);
            triangle(corner[0], corner[2], corner[3]);
        } else {
            triangle(corner[0], corner[2], corner[1]);
            triangle(corner[0], corner[3], corner[2]);
        }
    };

    // The six flat faces, each with its letter's cell laid on it, turned by its quarter turns.
    for (int f = 0; f < wjs::dice::kFaceCount; ++f) {
        const auto face = static_cast<std::size_t>(f);
        const Vec3 n = wjs::dice::kFaceNormal[face];
        const Vec3 right = wjs::dice::kFaceRight[face];
        const Vec3 up = wjs::dice::kFaceUp[face];
        const int k = turns_[face];
        const double cs = k == 0 ? 1.0 : (k == 2 ? -1.0 : 0.0);
        const double sn = k == 1 ? 1.0 : (k == 3 ? -1.0 : 0.0);
        std::uint16_t corner[4];
        Vec3 position[4];
        for (int c = 0; c < 4; ++c) {
            const double sa = (c == 1 || c == 2) ? 1.0 : -1.0;
            const double sb = c >= 2 ? 1.0 : -1.0;
            position[c] = n * edge + right * (sa * flat) + up * (sb * flat);
            // The cell's own frame, turned: where this corner falls on it.
            const double tx = sa * cs + sb * sn;
            const double ty = -sa * sn + sb * cs;
            corner[c] = add(position[c], n, blockU(f, 0.5 + tx * (0.5 - kBorder)),
                            blockV(f, 0.5 + ty * (0.5 - kBorder)));
        }
        quad(corner, position, n);
    }

    // The twelve bevels, each between two faces and sampling the cell's border.
    for (int f1 = 0; f1 < wjs::dice::kFaceCount; ++f1) {
        for (int f2 = f1 + 1; f2 < wjs::dice::kFaceCount; ++f2) {
            const Vec3 n1 = wjs::dice::kFaceNormal[static_cast<std::size_t>(f1)];
            const Vec3 n2 = wjs::dice::kFaceNormal[static_cast<std::size_t>(f2)];
            if (wjs::dice::dot(n1, n2) != 0.0) {
                continue;
            }
            const Vec3 along = wjs::dice::cross(n1, n2);
            const Vec3 position[4] = {
                n1 * edge + n2 * flat + along * flat, n1 * edge + n2 * flat - along * flat,
                n2 * edge + n1 * flat - along * flat, n2 * edge + n1 * flat + along * flat};
            const Vec3 normal[4] = {n1, n1, n2, n2};
            std::uint16_t corner[4];
            for (int c = 0; c < 4; ++c) {
                const int face = faceOf(normal[c]);
                corner[c] =
                    add(position[c], normal[c], blockU(face, kInBorder), blockV(face, kInBorder));
            }
            quad(corner, position, n1 + n2);
        }
    }

    // The eight corners: a triangle between the three faces that meet there.
    for (int corner = 0; corner < 8; ++corner) {
        const double sx = (corner & 1) != 0 ? 1.0 : -1.0;
        const double sy = (corner & 2) != 0 ? 1.0 : -1.0;
        const double sz = (corner & 4) != 0 ? 1.0 : -1.0;
        const Vec3 position[3] = {Vec3{sx * edge, sy * flat, sz * flat},
                                  Vec3{sx * flat, sy * edge, sz * flat},
                                  Vec3{sx * flat, sy * flat, sz * edge}};
        const Vec3 normal[3] = {Vec3{sx, 0.0, 0.0}, Vec3{0.0, sy, 0.0}, Vec3{0.0, 0.0, sz}};
        const int fa = faceOf(normal[0]);
        const int fb = faceOf(normal[1]);
        const int fc = faceOf(normal[2]);
        const std::uint16_t a =
            add(position[0], normal[0], blockU(fa, kInBorder), blockV(fa, kInBorder));
        const std::uint16_t b =
            add(position[1], normal[1], blockU(fb, kInBorder), blockV(fb, kInBorder));
        const std::uint16_t c =
            add(position[2], normal[2], blockU(fc, kInBorder), blockV(fc, kInBorder));
        const Vec3 geometric =
            wjs::dice::cross(position[1] - position[0], position[2] - position[0]);
        if (wjs::dice::dot(geometric, Vec3{sx, sy, sz}) >= 0.0) {
            triangle(a, b, c);
        } else {
            triangle(a, c, b);
        }
    }

    clear();
    setStride(static_cast<int>(sizeof(Vertex)));
    setPrimitiveType(PrimitiveType::Triangles);
    addAttribute(Attribute::PositionSemantic, static_cast<int>(offsetof(Vertex, px)),
                 Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, static_cast<int>(offsetof(Vertex, nx)),
                 Attribute::F32Type);
    addAttribute(Attribute::TexCoord0Semantic, static_cast<int>(offsetof(Vertex, u)),
                 Attribute::F32Type);
    addAttribute(Attribute::IndexSemantic, 0, Attribute::U16Type);
    setBounds(
        QVector3D(static_cast<float>(-edge), static_cast<float>(-edge), static_cast<float>(-edge)),
        QVector3D(static_cast<float>(edge), static_cast<float>(edge), static_cast<float>(edge)));
    setVertexData(QByteArray(reinterpret_cast<const char*>(vertices.data()),
                             static_cast<qsizetype>(vertices.size() * sizeof(Vertex))));
    setIndexData(QByteArray(reinterpret_cast<const char*>(indices.data()),
                            static_cast<qsizetype>(indices.size() * sizeof(std::uint16_t))));
    update();
}
