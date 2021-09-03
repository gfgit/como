/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drag_and_drop.h"

#include "../touch_redirect.h"
#include "main.h"
#include "seat/session.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xwl/xwayland_interface.h"

#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

bool drag_and_drop_filter::button(button_event const& event)
{
    auto seat = waylandServer()->seat();
    if (!seat->drags().is_pointer_drag()) {
        return false;
    }
    if (seat->drags().is_touch_drag()) {
        return true;
    }
    seat->setTimestamp(event.base.time_msec);

    if (event.state == button_state::pressed) {
        seat->pointers().button_pressed(event.key);
    } else {
        seat->pointers().button_released(event.key);
    }

    return true;
}

bool drag_and_drop_filter::motion(motion_event const& event)
{
    auto seat = waylandServer()->seat();
    if (!seat->drags().is_pointer_drag()) {
        return false;
    }
    if (seat->drags().is_touch_drag()) {
        return true;
    }
    seat->setTimestamp(event.base.time_msec);

    auto const pos = kwinApp()->input->redirect->globalPointer();
    seat->pointers().set_position(pos);

    // TODO: use InputDeviceHandler::at() here and check isClient()?
    auto window = kwinApp()->input->redirect->findManagedToplevel(pos.toPoint());
    if (auto xwl = xwayland()) {
        const auto ret = xwl->dragMoveFilter(window, pos.toPoint());
        if (ret == Xwl::DragEventReply::Ignore) {
            return false;
        } else if (ret == Xwl::DragEventReply::Take) {
            return true;
        }
    }

    if (window) {
        // TODO: consider decorations
        if (window->surface() != seat->drags().get_target().surface) {
            if (window->control) {
                workspace()->activateClient(window);
            }
            seat->drags().set_target(window->surface(), window->input_transform());
        }
    } else {
        // No window at that place, if we have a surface we need to reset.
        seat->drags().set_target(nullptr);
    }

    return true;
}

bool drag_and_drop_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (seat->drags().is_pointer_drag()) {
        return true;
    }
    if (!seat->drags().is_touch_drag()) {
        return false;
    }
    if (m_touchId != id) {
        return true;
    }
    seat->setTimestamp(time);
    kwinApp()->input->redirect->touch()->insertId(id, seat->touches().touch_down(pos));
    return true;
}
bool drag_and_drop_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (seat->drags().is_pointer_drag()) {
        return true;
    }
    if (!seat->drags().is_touch_drag()) {
        return false;
    }
    if (m_touchId < 0) {
        // We take for now the first id appearing as a move after a drag
        // started. We can optimize by specifying the id the drag is
        // associated with by implementing a key-value getter in Wrapland.
        m_touchId = id;
    }
    if (m_touchId != id) {
        return true;
    }
    seat->setTimestamp(time);
    const qint32 wraplandId = kwinApp()->input->redirect->touch()->mappedId(id);
    if (wraplandId == -1) {
        return true;
    }

    seat->touches().touch_move(wraplandId, pos);

    if (Toplevel* t = kwinApp()->input->redirect->findToplevel(pos.toPoint())) {
        // TODO: consider decorations
        if (t->surface() != seat->drags().get_target().surface) {
            if (t->control) {
                workspace()->activateClient(t);
            }
            seat->drags().set_target(t->surface(), pos, t->input_transform());
        }
    } else {
        // no window at that place, if we have a surface we need to reset
        seat->drags().set_target(nullptr);
    }
    return true;
}
bool drag_and_drop_filter::touchUp(qint32 id, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (!seat->drags().is_touch_drag()) {
        return false;
    }
    seat->setTimestamp(time);
    const qint32 wraplandId = kwinApp()->input->redirect->touch()->mappedId(id);
    if (wraplandId != -1) {
        seat->touches().touch_up(wraplandId);
        kwinApp()->input->redirect->touch()->removeId(id);
    }
    if (m_touchId == id) {
        m_touchId = -1;
    }
    return true;
}

}
