// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QRectF>

#include "board_layout.hpp"

namespace wjs::app {

// The layout rule (board_layout.hpp) for QML: give it the window and the board
// size and read where the board area, the panel and the board are. A screen
// writes no size of its own.
class SoloLayout : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qreal width READ width WRITE setWidth NOTIFY changed)
    Q_PROPERTY(qreal height READ height WRITE setHeight NOTIFY changed)
    Q_PROPERTY(int boardSize READ boardSize WRITE setBoardSize NOTIFY changed)
    // The interface's direction: Hebrew and Yiddish are right to left, and the
    // panel then takes the leading side of a wide window instead of the trailing
    // one. The board's tiles never mirror.
    Q_PROPERTY(bool rightToLeft READ rightToLeft WRITE setRightToLeft NOTIFY changed)

    // Wide: the board in the leading two thirds, the panel in the trailing third.
    // Tall: the board in the top two thirds, the panel below.
    Q_PROPERTY(bool wide READ wide NOTIFY changed)
    Q_PROPERTY(QRectF boardArea READ boardArea NOTIFY changed)
    Q_PROPERTY(QRectF panelArea READ panelArea NOTIFY changed)
    Q_PROPERTY(QRectF boardRect READ boardRect NOTIFY changed)
    Q_PROPERTY(qreal inset READ inset NOTIFY changed)
    Q_PROPERTY(qreal gap READ gap NOTIFY changed)
    Q_PROPERTY(qreal tile READ tile NOTIFY changed)
    // The largest board whose tile is at least 48 dp in this window: a size above it
    // is not shown (not shown disabled).
    Q_PROPERTY(int largestOffered READ largestOffered NOTIFY changed)

public:
    explicit SoloLayout(QObject* parent = nullptr) : QObject(parent) {}

    [[nodiscard]] qreal width() const { return width_; }
    [[nodiscard]] qreal height() const { return height_; }
    [[nodiscard]] int boardSize() const { return board_size_; }
    [[nodiscard]] bool rightToLeft() const { return right_to_left_; }
    void setWidth(qreal width);
    void setHeight(qreal height);
    void setBoardSize(int size);
    void setRightToLeft(bool right_to_left);

    [[nodiscard]] bool wide() const { return result_.wide; }
    [[nodiscard]] QRectF boardArea() const { return rect(result_.board_area); }
    [[nodiscard]] QRectF panelArea() const { return rect(result_.panel_area); }
    [[nodiscard]] QRectF boardRect() const { return rect(result_.board_rect); }
    [[nodiscard]] qreal inset() const { return result_.metrics.inset; }
    [[nodiscard]] qreal gap() const { return result_.metrics.gap; }
    [[nodiscard]] qreal tile() const { return result_.metrics.tile; }
    [[nodiscard]] int largestOffered() const;

    // The tile of an n by n board in this window, and whether that size is
    // offered: a size is shown only if its tile is at least 48 dp.
    Q_INVOKABLE qreal tileFor(int size) const;
    Q_INVOKABLE bool offers(int size) const;

signals:
    void changed();

private:
    static QRectF rect(const LayoutRect& r) { return {r.x, r.y, r.width, r.height}; }
    void update();

    qreal width_ = 0;
    qreal height_ = 0;
    int board_size_ = 3;
    bool right_to_left_ = false;
    SoloLayoutResult result_;
};

// The inset, gap and tile for an area of any size, for the board scene, which
// sits in the board area and draws its tiles by the same rule.
class BoardGeometry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit BoardGeometry(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE qreal inset(qreal width, qreal height) const;
    Q_INVOKABLE qreal gap(qreal width, qreal height) const;
    Q_INVOKABLE qreal tile(qreal width, qreal height, int size) const;
    Q_INVOKABLE qreal side(qreal width, qreal height, int size) const;
};

}  // namespace wjs::app
