/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018, 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch_redirect.h"

#include "device_redirect.h"

#include "input/event.h"
#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/touch.h"

#include "../../platform.h"
#include "base/wayland/output.h"
#include "wayland_server.h"
#include "win/input.h"
#include "workspace.h"

#include <KScreenLocker/KsldApp>

#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>
#include <cassert>

namespace KWin::input::wayland
{

void touch_redirect::init()
{
    assert(!inited());

    setInited(true);
    device_redirect_init(this);

    if (waylandServer()->hasScreenLockerIntegration()) {
        QObject::connect(
            ScreenLocker::KSldApp::self(), &ScreenLocker::KSldApp::lockStateChanged, this, [this] {
                if (!inited() || !waylandServer()->seat()->hasTouch()) {
                    return;
                }
                cancel();
                // position doesn't matter
                device_redirect_update(this);
            });
    }

    QObject::connect(workspace(), &QObject::destroyed, this, [this] { setInited(false); });
    QObject::connect(waylandServer(), &QObject::destroyed, this, [this] { setInited(false); });
}

void touch_redirect::setDecorationPressId(qint32 id)
{
    m_decorationId = id;
}

qint32 touch_redirect::decorationPressId() const
{
    return m_decorationId;
}

void touch_redirect::setInternalPressId(qint32 id)
{
    m_internalId = id;
}

qint32 touch_redirect::internalPressId() const
{
    return m_internalId;
}

QPointF touch_redirect::position() const
{
    return m_lastPosition;
}

bool touch_redirect::focusUpdatesBlocked()
{
    if (!inited()) {
        return true;
    }
    if (window_already_updated_this_cycle) {
        return true;
    }

    window_already_updated_this_cycle = true;

    if (waylandServer()->seat()->drags().is_touch_drag()) {
        return true;
    }
    if (m_touches > 1) {
        // first touch defines focus
        return true;
    }

    return false;
}

bool touch_redirect::positionValid() const
{
    assert(m_touches >= 0);

    // We can only determine a position with at least one touch point.
    return m_touches;
}

void touch_redirect::focusUpdate(Toplevel* focusOld, Toplevel* focusNow)
{
    // TODO: handle pointer grab aka popups

    if (focusOld && focusOld->control) {
        win::leave_event(focusOld);
    }

    QObject::disconnect(focus_geometry_notifier);
    focus_geometry_notifier = QMetaObject::Connection();

    if (focusNow && focusNow->control) {
        win::enter_event(focusNow, m_lastPosition.toPoint());
        workspace()->updateFocusMousePosition(m_lastPosition.toPoint());
    }

    auto seat = waylandServer()->seat();
    if (!focusNow || !focusNow->surface() || decoration()) {
        // no new surface or internal window or on decoration -> cleanup
        seat->touches().set_focused_surface(nullptr);
        return;
    }

    // TODO(romangg): Invalidate pointer focus?

    // TODO(romangg): Add input transformation API to Wrapland::Server::Seat for touch input.
    seat->touches().set_focused_surface(focusNow->surface(),
                                        -1 * focusNow->input_transform().map(focusNow->pos())
                                            + focusNow->pos());
    focus_geometry_notifier
        = QObject::connect(focusNow, &Toplevel::frame_geometry_changed, this, [this] {
              if (!focus()) {
                  return;
              }
              auto seat = waylandServer()->seat();
              if (focus()->surface() != seat->touches().get_focus().surface) {
                  return;
              }
              seat->touches().set_focused_surface_position(
                  -1 * focus()->input_transform().map(focus()->pos()) + focus()->pos());
          });
}

void touch_redirect::cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/)
{
    // nothing to do
}

void touch_redirect::cleanupDecoration(Decoration::DecoratedClientImpl* /*old*/,
                                       Decoration::DecoratedClientImpl* /*now*/)
{
    // nothing to do
}

void touch_redirect::insertId(qint32 internalId, qint32 wraplandId)
{
    m_idMapper.insert(internalId, wraplandId);
}

qint32 touch_redirect::mappedId(qint32 internalId)
{
    auto it = m_idMapper.constFind(internalId);
    if (it != m_idMapper.constEnd()) {
        return it.value();
    }
    return -1;
}

void touch_redirect::removeId(qint32 internalId)
{
    m_idMapper.remove(internalId);
}

QPointF get_abs_pos(QPointF const& pos, touch* dev)
{
    auto out = dev->output;

    if (!out) {
        auto const& outs = kwinApp()->platform->enabledOutputs();
        if (outs.empty()) {
            return QPointF();
        }
        out = static_cast<base::wayland::output*>(outs.front());
    }

    auto const& geo = out->geometry();

    return QPointF(geo.x() + geo.width() * pos.x(), geo.y() + geo.height() * pos.y());
};

void touch_redirect::process_down(touch_down_event const& event)
{
    auto const pos = get_abs_pos(event.pos, event.base.dev);
    processDown(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
    frame();
#endif
}

void touch_redirect::processDown(qint32 id,
                                 QPointF const& pos,
                                 quint32 time,
                                 input::touch* /*device*/)
{
    if (!inited()) {
        return;
    }
    m_lastPosition = pos;
    window_already_updated_this_cycle = false;
    m_touches++;
    if (m_touches == 1) {
        device_redirect_update(this);
    }
    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::touchDown, std::placeholders::_1, id, pos, time));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::touchDown, std::placeholders::_1, id, pos, time));
    window_already_updated_this_cycle = false;
}

void touch_redirect::process_up(touch_up_event const& event)
{
    processUp(event.id, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
    frame();
#endif
}

void touch_redirect::processUp(qint32 id, quint32 time, input::touch* /*device*/)
{
    if (!inited()) {
        return;
    }

    window_already_updated_this_cycle = false;

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::touchUp, std::placeholders::_1, id, time));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::touchUp, std::placeholders::_1, id, time));

    window_already_updated_this_cycle = false;
    m_touches--;

    if (m_touches == 0) {
        device_redirect_update(this);
    }
}

void touch_redirect::process_motion(touch_motion_event const& event)
{
    auto const pos = get_abs_pos(event.pos, event.base.dev);
    processMotion(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
    frame();
#endif
}

void touch_redirect::processMotion(qint32 id,
                                   QPointF const& pos,
                                   quint32 time,
                                   input::touch* /*device*/)
{
    if (!inited()) {
        return;
    }

    m_lastPosition = pos;
    window_already_updated_this_cycle = false;

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::touchMotion, std::placeholders::_1, id, pos, time));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::touchMotion, std::placeholders::_1, id, pos, time));

    window_already_updated_this_cycle = false;
}

void touch_redirect::cancel()
{
    if (!inited()) {
        return;
    }
    if (!waylandServer()->seat()->hasTouch()) {
        return;
    }
    waylandServer()->seat()->touches().cancel_sequence();
    m_idMapper.clear();
}

void touch_redirect::frame()
{
    if (!inited()) {
        return;
    }
    waylandServer()->seat()->touches().touch_frame();
}

}
