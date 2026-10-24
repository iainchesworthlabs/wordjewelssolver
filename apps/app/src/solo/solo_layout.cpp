// SPDX-License-Identifier: GPL-3.0-or-later
#include "solo_layout.hpp"

#include "wjs/board.hpp"

namespace wjs::app {
namespace {

[[nodiscard]] std::size_t count(int size) {
    return size > 0 ? static_cast<std::size_t>(size) : 0;
}

}  // namespace

void SoloLayout::setWidth(qreal width) {
    if (width_ != width) {
        width_ = width;
        update();
    }
}

void SoloLayout::setHeight(qreal height) {
    if (height_ != height) {
        height_ = height;
        update();
    }
}

void SoloLayout::setBoardSize(int size) {
    if (board_size_ != size) {
        board_size_ = size;
        update();
    }
}

void SoloLayout::setRightToLeft(bool right_to_left) {
    if (right_to_left_ != right_to_left) {
        right_to_left_ = right_to_left;
        update();
    }
}

void SoloLayout::update() {
    result_ = solo_layout(width_, height_, count(board_size_), right_to_left_);
    emit changed();
}

int SoloLayout::largestOffered() const {
    int largest = 0;
    for (int size = 1; size <= static_cast<int>(Board::max_dimension); ++size) {
        if (!offers(size)) {
            break;
        }
        largest = size;
    }
    return largest;
}

qreal SoloLayout::tileFor(int size) const {
    return solo_layout(width_, height_, count(size), right_to_left_).metrics.tile;
}

bool SoloLayout::offers(int size) const {
    return tileFor(size) >= min_offered_tile;
}

qreal BoardGeometry::inset(qreal width, qreal height) const {
    return board_metrics(width, height, 1).inset;
}

qreal BoardGeometry::gap(qreal width, qreal height) const {
    return board_metrics(width, height, 1).gap;
}

qreal BoardGeometry::tile(qreal width, qreal height, int size) const {
    return board_metrics(width, height, count(size)).tile;
}

qreal BoardGeometry::side(qreal width, qreal height, int size) const {
    return board_metrics(width, height, count(size)).side;
}

}  // namespace wjs::app
