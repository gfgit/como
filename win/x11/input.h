/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "win/deco.h"

namespace KWin::win::x11
{

template<typename Win>
void update_input_window(Win* win, QRect const& frame_geo)
{
    if (!Xcb::Extensions::self()->isShapeInputAvailable()) {
        return;
    }

    QRegion region;

    auto const has_border = !win->user_no_border && !win->geometry_update.fullscreen;

    if (has_border && win::decoration(win)) {
        auto const& borders = win::decoration(win)->resizeOnlyBorders();
        auto const left = borders.left();
        auto const top = borders.top();
        auto const right = borders.right();
        auto const bottom = borders.bottom();
        if (left != 0 || top != 0 || right != 0 || bottom != 0) {
            region = QRegion(-left,
                             -top,
                             win::decoration(win)->size().width() + left + right,
                             win::decoration(win)->size().height() + top + bottom);
            region = region.subtracted(win::decoration(win)->rect());
        }
    }

    if (region.isEmpty()) {
        win->xcb_windows.input.reset();
        return;
    }

    auto bounds = region.boundingRect();
    win->input_offset = bounds.topLeft();

    // Move the bounding rect to screen coordinates
    bounds.translate(frame_geo.topLeft());

    // Move the region to input window coordinates
    region.translate(-win->input_offset);

    if (!win->xcb_windows.input.isValid()) {
        auto const mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        uint32_t const values[] = {true,
                                   XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                       | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                       | XCB_EVENT_MASK_POINTER_MOTION};
        win->xcb_windows.input.create(bounds, XCB_WINDOW_CLASS_INPUT_ONLY, mask, values);
        if (win->mapping == mapping_state::mapped) {
            win->xcb_windows.input.map();
        }
    } else {
        win->xcb_windows.input.setGeometry(bounds);
    }

    auto const rects = Xcb::regionToRects(region);
    xcb_shape_rectangles(connection(),
                         XCB_SHAPE_SO_SET,
                         XCB_SHAPE_SK_INPUT,
                         XCB_CLIP_ORDERING_UNSORTED,
                         win->xcb_windows.input,
                         0,
                         0,
                         rects.count(),
                         rects.constData());
}

template<typename Win>
bool perform_mouse_command(Win* win, Options::MouseCommand command, QPoint const& globalPos)
{
    bool replay = false;
    switch (command) {
    case Options::MouseShade:
        win->toggleShade();
        win->cancel_shade_hover_timer();
        break;
    case Options::MouseSetShade:
        win->setShade(win::shade::normal);
        win->cancel_shade_hover_timer();
        break;
    case Options::MouseUnsetShade:
        win->setShade(win::shade::none);
        win->cancel_shade_hover_timer();
        break;
    default:
        return static_cast<Toplevel*>(win)->Toplevel::performMouseCommand(command, globalPos);
    }
    return replay;
}

}
