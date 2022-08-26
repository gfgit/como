/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/activation.h"

namespace KWin::win::x11
{

template<typename Win>
void update_urgency(Win* win)
{
    if (win->info->urgency()) {
        set_demands_attention(win, true);
    }
}

template<typename Win>
void cancel_focus_out_timer(Win* win)
{
    if (win->focus_out_timer) {
        win->focus_out_timer->stop();
    }
}

}