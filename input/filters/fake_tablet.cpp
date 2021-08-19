/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "fake_tablet.h"

#include "../pointer_redirect.h"
#include "input/logging.h"
#include "main.h"
#include "wayland_server.h"
#include "workspace.h"
#include <input/qt_event.h>

#include <QKeyEvent>

namespace KWin::input
{

bool fake_tablet_filter::tabletToolEvent(QTabletEvent* event)
{
    if (!workspace()) {
        return false;
    }

    auto get_event = [&event](button_state state) {
        return button_event{qt_mouse_button_to_button(Qt::LeftButton),
                            state,
                            {nullptr, static_cast<uint32_t>(event->timestamp())}};
    };

    switch (event->type()) {
    case QEvent::TabletMove:
    case QEvent::TabletEnterProximity:
        kwinApp()->input->redirect->pointer()->processMotion(event->globalPosF(),
                                                             event->timestamp());
        break;
    case QEvent::TabletPress:
        kwinApp()->input->redirect->pointer()->process_button(get_event(button_state::pressed));
        break;
    case QEvent::TabletRelease:
        kwinApp()->input->redirect->pointer()->process_button(get_event(button_state::released));
        break;
    case QEvent::TabletLeaveProximity:
        break;
    default:
        qCWarning(KWIN_INPUT) << "Unexpected tablet event type" << event;
        break;
    }
    waylandServer()->simulateUserActivity();
    return true;
}

}
