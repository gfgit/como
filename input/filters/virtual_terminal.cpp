/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtual_terminal.h"

#include "input/xkb.h"
#include "main.h"
#include "seat/session.h"

#include <QKeyEvent>

namespace KWin::input
{

bool virtual_terminal_filter::keyEvent(QKeyEvent* event)
{
    // really on press and not on release? X11 switches on press.
    if (event->type() == QEvent::KeyPress && !event->isAutoRepeat()) {
        auto const keysym = event->nativeVirtualKey();
        if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
            kwinApp()->session->switchVirtualTerminal(keysym - XKB_KEY_XF86Switch_VT_1 + 1);
            return true;
        }
    }
    return false;
}

}
