/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"

#include "win/fullscreen.h"

namespace KWin::win
{

template<>
void update_fullscreen_impl(x11::window* win, bool full)
{
    if (full) {
        win->info->setState(NET::FullScreen, NET::FullScreen);
        update_fullscreen_enable(win);
        if (win->info->fullscreenMonitors().isSet()) {
            win->setFrameGeometry(x11::fullscreen_monitors_area(win->info->fullscreenMonitors()));
        }
    } else {
        win->info->setState(NET::States(), NET::FullScreen);
        update_fullscreen_disable(win);
    }
}

}
