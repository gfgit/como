/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"
#include "input/logging.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "main.h"
#include "win/wayland/space.h"

#include <Wrapland/Server/kde_idle.h>

namespace KWin::input
{

/**
 * Useful when there's no proper tablet support on the clients
 */
class fake_tablet_filter : public event_filter
{
public:
    explicit fake_tablet_filter(input::redirect& redirect)
        : redirect{redirect}
    {
    }

    bool tabletToolEvent(QTabletEvent* event) override
    {
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
            kwinApp()->input->redirect->pointer()->process_button(
                get_event(button_state::released));
            break;
        case QEvent::TabletLeaveProximity:
            break;
        default:
            qCWarning(KWIN_INPUT) << "Unexpected tablet event type" << event;
            break;
        }
        static_cast<win::wayland::space&>(redirect.space).kde_idle->simulateUserActivity();

        return true;
    }

private:
    input::redirect& redirect;
};

}
