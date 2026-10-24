// SPDX-License-Identifier: GPL-3.0-or-later
#include <QRectF>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "board_layout.hpp"
#include "solo_layout.hpp"

namespace {

using Catch::Approx;
using wjs::app::solo_layout;

// A window of docs/design/board.md's reference table and the tiles it lists for
// 3x3, 4x4, 5x5 and 6x6 (the page rounds them to whole dp).
struct Window {
    const char* name;
    double width;
    double height;
    bool wide;
    double area_width;
    double area_height;
    double tiles[4];
};

const std::vector<Window>& reference_windows() {
    static const std::vector<Window> windows{
        {"phone 360 x 740", 360, 740, false, 360, 493, {108, 80, 62, 51}},
        {"phone 390 x 800", 390, 800, false, 390, 533, {118, 87, 68, 56}},
        {"phone 320 x 600", 320, 600, false, 320, 400, {95, 70, 54, 44}},
        {"phone on its side 740 x 360", 740, 360, true, 493, 360, {108, 80, 62, 51}},
        {"small tablet 744 x 1133", 744, 1133, false, 744, 755, {227, 168, 133, 109}},
        {"tablet 800 x 1280", 800, 1280, false, 800, 853, {245, 182, 144, 119}},
        {"tablet 1280 x 800", 1280, 800, true, 853, 800, {245, 182, 144, 119}},
        {"desktop 1100 x 700", 1100, 700, true, 733, 700, {212, 157, 124, 102}},
        {"narrow browser 420 x 800", 420, 800, false, 420, 533, {128, 95, 74, 61}},
    };
    return windows;
}

}  // namespace

TEST_CASE("the board area is two thirds of the window on its long side", "[app][layout]") {
    for (const auto& window : reference_windows()) {
        INFO(window.name);
        const auto layout = solo_layout(window.width, window.height, 4);

        CHECK(layout.wide == window.wide);
        CHECK(layout.board_area.width == Approx(window.area_width).margin(0.5));
        CHECK(layout.board_area.height == Approx(window.area_height).margin(0.5));
        // The panel is the rest, and the two together are the window.
        if (layout.wide) {
            CHECK(layout.panel_area.x == Approx(layout.board_area.width));
            CHECK(layout.panel_area.width + layout.board_area.width == Approx(window.width));
            CHECK(layout.panel_area.height == Approx(window.height));
        } else {
            CHECK(layout.panel_area.y == Approx(layout.board_area.height));
            CHECK(layout.panel_area.height + layout.board_area.height == Approx(window.height));
            CHECK(layout.panel_area.width == Approx(window.width));
        }
    }
}

TEST_CASE("the split mirrors for a right-to-left language, the board does not",
          "[app][layout][rtl]") {
    const auto ltr = solo_layout(1280, 800, 4);
    const auto rtl = solo_layout(1280, 800, 4, true);

    // The panel takes the leading side: the left in RTL, the right in LTR.
    CHECK(ltr.panel_area.x == Approx(1280.0 * 2 / 3));
    CHECK(rtl.panel_area.x == 0);
    CHECK(rtl.panel_area.width == Approx(ltr.panel_area.width));
    CHECK(rtl.board_area.x == Approx(rtl.panel_area.width));
    CHECK(rtl.board_area.x + rtl.board_area.width == Approx(1280));
    // The board is the same board: same area size, same side, drawn from its own
    // top left. Its tiles do not mirror.
    CHECK(rtl.board_area.width == Approx(ltr.board_area.width));
    CHECK(rtl.board_rect.width == Approx(ltr.board_rect.width));
    CHECK(rtl.board_rect.x - rtl.board_area.x == Approx(ltr.board_rect.x - ltr.board_area.x));

    // Tall windows are split top and bottom, which has no direction.
    CHECK(solo_layout(800, 1280, 4, true).board_area == solo_layout(800, 1280, 4).board_area);

    // The object the screens bind to carries the flag and recomputes.
    wjs::app::SoloLayout rule;
    rule.setWidth(1280);
    rule.setHeight(800);
    rule.setRightToLeft(true);
    CHECK(rule.rightToLeft());
    CHECK(rule.panelArea().x() == 0);
    CHECK(rule.panelArea().width() == Approx(1280.0 / 3));
    CHECK(rule.boardArea().x() == Approx(1280.0 / 3));
    CHECK(rule.boardArea().width() == Approx(1280.0 * 2 / 3));
}

TEST_CASE("the tile of each size in each reference window is the page's", "[app][layout]") {
    for (const auto& window : reference_windows()) {
        for (std::size_t i = 0; i < 4; ++i) {
            INFO(window.name << ", " << (i + 3) << "x" << (i + 3));
            const auto layout = solo_layout(window.width, window.height, i + 3);
            // The page lists whole dp, so a half dp is the rounding.
            CHECK(layout.metrics.tile == Approx(window.tiles[i]).margin(0.6));
        }
    }
}

TEST_CASE("a square window is wide, and the width decides ties", "[app][layout]") {
    const auto layout = solo_layout(800, 800, 4);

    CHECK(layout.wide);
    CHECK(layout.board_area.width == Approx(800.0 * 2 / 3));
}

TEST_CASE("the inset and the gap are small under 600 dp and roomy from it", "[app][layout]") {
    const auto small = wjs::app::board_metrics(599, 900, 4);
    const auto roomy = wjs::app::board_metrics(600, 900, 4);

    CHECK(small.inset == 12);
    CHECK(small.gap == 6);
    CHECK(roomy.inset == 24);
    CHECK(roomy.gap == 8);
}

TEST_CASE("the board is the largest square that fits its area, centred", "[app][layout]") {
    const auto layout = solo_layout(1280, 800, 4);
    const auto& rect = layout.board_rect;

    CHECK(rect.width == rect.height);
    // The area is 853 x 800: the shorter side less two insets of 24.
    CHECK(rect.width == Approx(752));
    CHECK(rect.width == Approx(4 * layout.metrics.tile + 3 * layout.metrics.gap));
    CHECK(rect.x + rect.width / 2 == Approx(layout.board_area.x + layout.board_area.width / 2));
    CHECK(rect.y + rect.height / 2 == Approx(layout.board_area.y + layout.board_area.height / 2));
}

TEST_CASE("a size is offered only if its tile is at least 48 dp", "[app][layout]") {
    // A 320 x 600 phone: the 6x6 tile is 44 dp and is not shown (not shown disabled).
    const auto phone = solo_layout(320, 600, 6);
    CHECK(phone.metrics.tile < wjs::app::min_offered_tile);
    CHECK(solo_layout(320, 600, 5).metrics.tile >= wjs::app::min_offered_tile);

    wjs::app::SoloLayout rule;
    rule.setWidth(320);
    rule.setHeight(600);
    CHECK(rule.offers(3));
    CHECK(rule.offers(4));
    CHECK(rule.offers(5));
    CHECK_FALSE(rule.offers(6));
    CHECK(rule.largestOffered() == 5);
}

TEST_CASE("the MVP's sizes are offered in every reference window", "[app][layout]") {
    for (const auto& window : reference_windows()) {
        INFO(window.name);
        CHECK(solo_layout(window.width, window.height, 3).metrics.tile >= 48);
        CHECK(solo_layout(window.width, window.height, 4).metrics.tile >= 48);
    }
}

TEST_CASE("an area too small for a tile has no tile and no board", "[app][layout]") {
    const auto metrics = wjs::app::board_metrics(10, 10, 4);

    CHECK(metrics.tile == 0);
    CHECK(metrics.side == 0);
    CHECK(wjs::app::board_metrics(300, 300, 0).tile == 0);
}

TEST_CASE("the layout object recomputes when the window or the size changes", "[app][layout]") {
    wjs::app::SoloLayout rule;
    int changes = 0;
    QObject::connect(&rule, &wjs::app::SoloLayout::changed, [&] { ++changes; });

    rule.setWidth(1280);
    rule.setHeight(800);
    rule.setBoardSize(4);

    CHECK(changes == 3);
    CHECK(rule.wide());
    CHECK(rule.tile() == Approx(182).margin(0.6));
    CHECK(rule.boardArea() == QRectF(0, 0, 1280.0 * 2 / 3, 800));
    CHECK(rule.panelArea().x() == Approx(1280.0 * 2 / 3));

    rule.setBoardSize(4);  // unchanged: nothing to say
    CHECK(changes == 3);

    rule.setWidth(800);
    rule.setHeight(1280);
    CHECK_FALSE(rule.wide());
    CHECK(rule.panelArea().y() == Approx(1280.0 * 2 / 3));
}

TEST_CASE("the geometry the scene draws by is the same rule", "[app][layout]") {
    wjs::app::BoardGeometry geometry;
    const auto layout = solo_layout(1280, 800, 4);

    CHECK(geometry.inset(853, 800) == layout.metrics.inset);
    CHECK(geometry.gap(853, 800) == layout.metrics.gap);
    CHECK(geometry.tile(853, 800, 4) == Approx(layout.metrics.tile).margin(0.01));
    CHECK(geometry.side(853, 800, 4) == Approx(layout.board_rect.width).margin(0.01));
}
