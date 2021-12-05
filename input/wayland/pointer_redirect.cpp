/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer_redirect.h"

#include "cursor.h"
#include "cursor_image.h"
#include "device_redirect.h"

#include "input/event.h"
#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/qt_event.h"

#include "../../platform.h"
#include "decorations/decoratedclient.h"
#include "screens.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "win/input.h"
#include "win/wayland/space.h"
#include "win/x11/window.h"
#include "workspace.h"

#include <KDecoration2/Decoration>
#include <KScreenLocker/KsldApp>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_constraints_v1.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>
#include <cassert>

namespace KWin::input::wayland
{

static bool screenContainsPos(QPointF const& pos)
{
    for (int i = 0; i < Screens::self()->count(); ++i) {
        if (Screens::self()->geometry(i).contains(pos.toPoint())) {
            return true;
        }
    }
    return false;
}

static QPointF confineToBoundingBox(QPointF const& pos, QRectF const& boundingBox)
{
    return QPointF(qBound(boundingBox.left(), pos.x(), boundingBox.right() - 1.0),
                   qBound(boundingBox.top(), pos.y(), boundingBox.bottom() - 1.0));
}

pointer_redirect::pointer_redirect(input::redirect* redirect)
    : input::pointer_redirect(redirect)
{
}

void pointer_redirect::init()
{
    device_redirect_init(this);

    QObject::connect(
        Screens::self(), &Screens::changed, this, &pointer_redirect::updateAfterScreenChange);
    if (waylandServer()->hasScreenLockerIntegration()) {
        QObject::connect(
            ScreenLocker::KSldApp::self(), &ScreenLocker::KSldApp::lockStateChanged, this, [this] {
                waylandServer()->seat()->pointers().cancel_pinch_gesture();
                waylandServer()->seat()->pointers().cancel_swipe_gesture();
                device_redirect_update(this);
            });
    }

    QObject::connect(waylandServer()->seat(), &Wrapland::Server::Seat::dragEnded, this, [this] {
        // need to force a focused pointer change
        waylandServer()->seat()->pointers().set_focused_surface(nullptr);
        device_redirect_set_focus(this, nullptr);
        device_redirect_update(this);
    });

    // connect the move resize of all window
    auto setupMoveResizeConnection = [this](Toplevel* c) {
        if (!c->control) {
            return;
        }
        QObject::connect(c,
                         &Toplevel::clientStartUserMovedResized,
                         this,
                         &pointer_redirect::update_on_start_move_resize);
        QObject::connect(c, &Toplevel::clientFinishUserMovedResized, this, [this] {
            device_redirect_update(this);
        });
    };
    const auto clients = workspace()->allClientList();
    std::for_each(clients.begin(), clients.end(), setupMoveResizeConnection);
    QObject::connect(workspace(), &Workspace::clientAdded, this, setupMoveResizeConnection);
    QObject::connect(static_cast<win::wayland::space*>(workspace()),
                     &win::wayland::space::wayland_window_added,
                     this,
                     setupMoveResizeConnection);

    // warp the cursor to center of screen
    warp(Screens::self()->geometry().center());
    updateAfterScreenChange();

    auto wayland_cursor = dynamic_cast<wayland::cursor*>(input::get_cursor());
    assert(wayland_cursor);
    QObject::connect(this,
                     &pointer_redirect::decorationChanged,
                     wayland_cursor->cursor_image.get(),
                     &wayland::cursor_image::updateDecoration);
}

void pointer_redirect::update_on_start_move_resize()
{
    break_pointer_constraints(focus() ? focus()->surface() : nullptr);
    disconnect_pointer_constraints_connection();
    device_redirect_set_focus(this, nullptr);
    waylandServer()->seat()->pointers().set_focused_surface(nullptr);
}

void pointer_redirect::update_to_reset()
{
    if (internalWindow()) {
        QObject::disconnect(notifiers.internal_window);
        notifiers.internal_window = QMetaObject::Connection();
        QEvent event(QEvent::Leave);
        QCoreApplication::sendEvent(internalWindow(), &event);
        device_redirect_set_internal_window(this, nullptr);
    }
    if (decoration()) {
        QHoverEvent event(QEvent::HoverLeave, QPointF(), QPointF());
        QCoreApplication::instance()->sendEvent(decoration()->decoration(), &event);
        device_redirect_set_decoration(this, nullptr);
    }
    if (auto focus_window = focus()) {
        if (focus_window->control) {
            win::leave_event(focus_window);
        }
        QObject::disconnect(notifiers.focus_geometry);
        notifiers.focus_geometry = QMetaObject::Connection();
        break_pointer_constraints(focus_window->surface());
        disconnect_pointer_constraints_connection();
        device_redirect_set_focus(this, nullptr);
    }
    waylandServer()->seat()->pointers().set_focused_surface(nullptr);
}

void pointer_redirect::processMotion(QPointF const& pos, uint32_t time, input::pointer* device)
{
    // Events for motion_absolute_event have positioning relative to screen size.
    auto const ssize = Screens::self()->size();
    auto const rel_pos = QPointF(pos.x() / ssize.width(), pos.y() / ssize.height());

    auto event = motion_absolute_event{rel_pos, {device, time}};
    process_motion_absolute(event);
}

class PositionUpdateBlocker
{
public:
    PositionUpdateBlocker(pointer_redirect* pointer)
        : m_pointer(pointer)
    {
        s_counter++;
    }
    ~PositionUpdateBlocker()
    {
        s_counter--;
        if (s_counter == 0) {
            if (!s_scheduledPositions.isEmpty()) {
                auto const sched = s_scheduledPositions.takeFirst();
                if (sched.abs) {
                    m_pointer->process_motion_absolute({sched.pos, {nullptr, sched.time}});
                } else {
                    m_pointer->process_motion(
                        {sched.delta, sched.unaccel_delta, {nullptr, sched.time}});
                }
            }
        }
    }

    static bool isPositionBlocked()
    {
        return s_counter > 0;
    }

    static void schedule(QPointF const& pos, uint32_t time)
    {
        s_scheduledPositions.append({pos, {}, {}, time, true});
    }
    static void schedule(QPointF const& delta, QPointF const& unaccel_delta, uint32_t time)
    {
        s_scheduledPositions.append({{}, delta, unaccel_delta, time, false});
    }

private:
    static int s_counter;
    struct ScheduledPosition {
        QPointF pos;
        QPointF delta;
        QPointF unaccel_delta;
        uint32_t time;
        bool abs;
    };
    static QVector<ScheduledPosition> s_scheduledPositions;

    pointer_redirect* m_pointer;
};

int PositionUpdateBlocker::s_counter = 0;
QVector<PositionUpdateBlocker::ScheduledPosition> PositionUpdateBlocker::s_scheduledPositions;

void pointer_redirect::process_motion(motion_event const& event)
{
    if (PositionUpdateBlocker::isPositionBlocked()) {
        PositionUpdateBlocker::schedule(event.delta, event.unaccel_delta, event.base.time_msec);
        return;
    }

    PositionUpdateBlocker blocker(this);

    auto const pos = this->pos() + QPointF(event.delta.x(), event.delta.y());
    update_position(pos);
    device_redirect_update(this);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::motion, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::motion, std::placeholders::_1, event));

    process_frame();
}

void pointer_redirect::process_motion_absolute(motion_absolute_event const& event)
{
    if (PositionUpdateBlocker::isPositionBlocked()) {
        PositionUpdateBlocker::schedule(event.pos, event.base.time_msec);
        return;
    }

    auto const ssize = Screens::self()->size();
    auto const pos = QPointF(ssize.width() * event.pos.x(), ssize.height() * event.pos.y());

    PositionUpdateBlocker blocker(this);
    update_position(pos);
    device_redirect_update(this);

    auto motion_ev = motion_event({{}, {}, event.base});

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::motion, std::placeholders::_1, motion_ev));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::motion, std::placeholders::_1, motion_ev));

    process_frame();
}

void pointer_redirect::process_button(button_event const& event)
{
    if (event.state == button_state::pressed) {
        // Check focus before processing spies/filters.
        device_redirect_update(this);
    }

    update_button(event);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::button, std::placeholders::_1, event));

    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::button, std::placeholders::_1, event));

    if (event.state == button_state::released) {
        // Check focus after processing spies/filters.
        device_redirect_update(this);
    }

    process_frame();
}

void pointer_redirect::process_axis(axis_event const& event)
{
    device_redirect_update(this);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::axis, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::axis, std::placeholders::_1, event));

    process_frame();
}

void pointer_redirect::process_swipe_begin(swipe_begin_event const& event)
{
    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::swipe_begin, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::swipe_begin, std::placeholders::_1, event));
}

void pointer_redirect::process_swipe_update(swipe_update_event const& event)
{
    device_redirect_update(this);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::swipe_update, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::swipe_update, std::placeholders::_1, event));
}

void pointer_redirect::process_swipe_end(swipe_end_event const& event)
{
    device_redirect_update(this);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::swipe_end, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::swipe_end, std::placeholders::_1, event));
}

void pointer_redirect::process_pinch_begin(pinch_begin_event const& event)
{
    device_redirect_update(this);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::pinch_begin, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::pinch_begin, std::placeholders::_1, event));
}

void pointer_redirect::process_pinch_update(pinch_update_event const& event)
{
    device_redirect_update(this);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::pinch_update, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::pinch_update, std::placeholders::_1, event));
}

void pointer_redirect::process_pinch_end(pinch_end_event const& event)
{
    device_redirect_update(this);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::pinch_end, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::pinch_end, std::placeholders::_1, event));
}

void pointer_redirect::process_frame()
{
    waylandServer()->seat()->pointers().frame();
}

bool pointer_redirect::areButtonsPressed() const
{
    for (auto state : m_buttons) {
        if (state == button_state::pressed) {
            return true;
        }
    }
    return false;
}

bool pointer_redirect::focusUpdatesBlocked()
{
    if (waylandServer()->seat()->drags().is_pointer_drag()) {
        // ignore during drag and drop
        return true;
    }
    if (waylandServer()->seat()->hasTouch()
        && waylandServer()->seat()->touches().is_in_progress()) {
        // ignore during touch operations
        return true;
    }
    if (kwinApp()->input->redirect->isSelectingWindow()) {
        return true;
    }
    if (areButtonsPressed()) {
        return true;
    }
    return false;
}

void pointer_redirect::cleanupInternalWindow(QWindow* old, QWindow* now)
{
    QObject::disconnect(notifiers.internal_window);
    notifiers.internal_window = QMetaObject::Connection();

    if (old) {
        // leave internal window
        QEvent leaveEvent(QEvent::Leave);
        QCoreApplication::sendEvent(old, &leaveEvent);
    }

    if (now) {
        notifiers.internal_window = QObject::connect(
            internalWindow(), &QWindow::visibleChanged, this, [this](bool visible) {
                if (!visible) {
                    device_redirect_update(this);
                }
            });
    }
}

void pointer_redirect::cleanupDecoration(Decoration::DecoratedClientImpl* old,
                                         Decoration::DecoratedClientImpl* now)
{
    QObject::disconnect(notifiers.decoration_geometry);
    notifiers.decoration_geometry = QMetaObject::Connection();
    workspace()->updateFocusMousePosition(position().toPoint());

    if (old) {
        // send leave event to old decoration
        QHoverEvent event(QEvent::HoverLeave, QPointF(), QPointF());
        QCoreApplication::instance()->sendEvent(old->decoration(), &event);
    }
    if (!now) {
        // left decoration
        return;
    }

    waylandServer()->seat()->pointers().set_focused_surface(nullptr);

    auto pos = m_pos - now->client()->pos();
    QHoverEvent event(QEvent::HoverEnter, pos, pos);
    QCoreApplication::instance()->sendEvent(now->decoration(), &event);
    win::process_decoration_move(now->client(), pos.toPoint(), m_pos.toPoint());

    auto window = decoration()->client();

    notifiers.decoration_geometry
        = QObject::connect(window, &Toplevel::frame_geometry_changed, this, [this, window] {
              if (window->control && (win::is_move(window) || win::is_resize(window))) {
                  // Don't update while doing an interactive move or resize.
                  return;
              }
              // ensure maximize button gets the leave event when maximizing/restore a window, see
              // BUG 385140
              auto const oldDeco = decoration();
              device_redirect_update(this);
              if (oldDeco && oldDeco == decoration() && !win::is_move(decoration()->client())
                  && !win::is_resize(decoration()->client()) && !areButtonsPressed()) {
                  // position of window did not change, we need to send HoverMotion manually
                  QPointF const p = m_pos - decoration()->client()->pos();
                  QHoverEvent event(QEvent::HoverMove, p, p);
                  QCoreApplication::instance()->sendEvent(decoration()->decoration(), &event);
              }
          });
}

void pointer_redirect::focusUpdate(Toplevel* focusOld, Toplevel* focusNow)
{
    if (focusOld) {
        // Need to check on control because of Xwayland unmanaged windows.
        if (auto lead = win::lead_of_annexed_transient(focusOld); lead && lead->control) {
            win::leave_event(lead);
        }
        break_pointer_constraints(focusOld->surface());
        disconnect_pointer_constraints_connection();
    }
    QObject::disconnect(notifiers.focus_geometry);
    notifiers.focus_geometry = QMetaObject::Connection();

    if (focusNow) {
        if (auto lead = win::lead_of_annexed_transient(focusNow)) {
            win::enter_event(lead, m_pos.toPoint());
        }
        workspace()->updateFocusMousePosition(m_pos.toPoint());
    }

    if (internalWindow()) {
        // enter internal window
        auto const pos = at()->pos();
        QEnterEvent enterEvent(pos, pos, m_pos);
        QCoreApplication::sendEvent(internalWindow(), &enterEvent);
    }

    auto seat = waylandServer()->seat();
    if (!focusNow || !focusNow->surface() || decoration()) {
        // Clean up focused pointer surface if there's no client to take focus,
        // or the pointer is on a client without surface or on a decoration.
        warp_xcb_on_surface_left(nullptr);
        seat->pointers().set_focused_surface(nullptr);
        return;
    }

    // TODO: add convenient API to update global pos together with updating focused surface
    warp_xcb_on_surface_left(focusNow->surface());

    // TODO: why? in order to reset the cursor icon?
    s_cursorUpdateBlocking = true;
    seat->pointers().set_focused_surface(nullptr);
    s_cursorUpdateBlocking = false;

    seat->pointers().set_position(m_pos.toPoint());
    seat->pointers().set_focused_surface(focusNow->surface(), focusNow->input_transform());

    notifiers.focus_geometry
        = QObject::connect(focusNow, &Toplevel::frame_geometry_changed, this, [this] {
              if (!focus()) {
                  // Might happen for Xwayland clients.
                  return;
              }

              // TODO: can we check on the client instead?
              if (workspace()->moveResizeClient()) {
                  // don't update while moving
                  return;
              }
              auto seat = waylandServer()->seat();
              if (focus()->surface() != seat->pointers().get_focus().surface) {
                  return;
              }
              seat->pointers().set_focused_surface_transformation(focus()->input_transform());
          });

    notifiers.constraints = QObject::connect(focusNow->surface(),
                                             &Wrapland::Server::Surface::pointerConstraintsChanged,
                                             this,
                                             &pointer_redirect::updatePointerConstraints);
    notifiers.constraints_activated = QObject::connect(workspace(),
                                                       &Workspace::clientActivated,
                                                       this,
                                                       &pointer_redirect::updatePointerConstraints);
    updatePointerConstraints();
}

void pointer_redirect::break_pointer_constraints(Wrapland::Server::Surface* surface)
{
    // cancel pointer constraints
    if (surface) {
        auto c = surface->confinedPointer();
        if (c && c->isConfined()) {
            c->setConfined(false);
        }
        auto l = surface->lockedPointer();
        if (l && l->isLocked()) {
            l->setLocked(false);
        }
    }
    disconnect_confined_pointer_region_connection();
    constraints.confined = false;
    constraints.locked = false;
}

void pointer_redirect::disconnect_confined_pointer_region_connection()
{
    QObject::disconnect(notifiers.confined_pointer_region);
    notifiers.confined_pointer_region = QMetaObject::Connection();
}

void pointer_redirect::disconnect_locked_pointer_destroyed_connection()
{
    QObject::disconnect(notifiers.locked_pointer_destroyed);
    notifiers.locked_pointer_destroyed = QMetaObject::Connection();
}

void pointer_redirect::disconnect_pointer_constraints_connection()
{
    QObject::disconnect(notifiers.constraints);
    notifiers.constraints = QMetaObject::Connection();

    QObject::disconnect(notifiers.constraints_activated);
    notifiers.constraints_activated = QMetaObject::Connection();
}

template<typename T>
static QRegion getConstraintRegion(Toplevel* t, T* constraint)
{
    if (!t->surface()) {
        return QRegion();
    }

    QRegion constraint_region;

    if (t->surface()->state().input_is_infinite) {
        auto const client_size = win::frame_relative_client_rect(t).size();
        constraint_region = QRegion(0, 0, client_size.width(), client_size.height());
    } else {
        constraint_region = t->surface()->state().input;
    }

    if (auto const& reg = constraint->region(); !reg.isEmpty()) {
        constraint_region = constraint_region.intersected(reg);
    }

    return constraint_region.translated(win::frame_to_client_pos(t, t->pos()));
}

void pointer_redirect::setEnableConstraints(bool set)
{
    if (constraints.enabled == set) {
        return;
    }
    constraints.enabled = set;
    updatePointerConstraints();
}

bool pointer_redirect::isConstrained() const
{
    return constraints.confined || constraints.locked;
}

void pointer_redirect::updatePointerConstraints()
{
    if (!focus()) {
        return;
    }
    const auto s = focus()->surface();
    if (!s) {
        return;
    }
    if (s != waylandServer()->seat()->pointers().get_focus().surface) {
        return;
    }
    const bool canConstrain = constraints.enabled && focus() == workspace()->activeClient();
    const auto cf = s->confinedPointer();
    if (cf) {
        if (cf->isConfined()) {
            if (!canConstrain) {
                cf->setConfined(false);
                constraints.confined = false;
                disconnect_confined_pointer_region_connection();
            }
            return;
        }
        const QRegion r = getConstraintRegion(focus(), cf.data());
        if (canConstrain && r.contains(m_pos.toPoint())) {
            cf->setConfined(true);
            constraints.confined = true;
            notifiers.confined_pointer_region = QObject::connect(
                cf.data(), &Wrapland::Server::ConfinedPointerV1::regionChanged, this, [this] {
                    if (!focus()) {
                        return;
                    }
                    const auto s = focus()->surface();
                    if (!s) {
                        return;
                    }
                    const auto cf = s->confinedPointer();
                    if (!getConstraintRegion(focus(), cf.data()).contains(m_pos.toPoint())) {
                        // pointer no longer in confined region, break the confinement
                        cf->setConfined(false);
                        constraints.confined = false;
                    } else {
                        if (!cf->isConfined()) {
                            cf->setConfined(true);
                            constraints.confined = true;
                        }
                    }
                });
            return;
        }
    } else {
        constraints.confined = false;
        disconnect_confined_pointer_region_connection();
    }
    const auto lock = s->lockedPointer();
    if (lock) {
        if (lock->isLocked()) {
            if (!canConstrain) {
                const auto hint = lock->cursorPositionHint();
                lock->setLocked(false);
                constraints.locked = false;
                disconnect_locked_pointer_destroyed_connection();
                if (!(hint.x() < 0 || hint.y() < 0) && focus()) {
                    // TODO(romangg): different client offset for Xwayland clients?
                    processMotion(win::frame_to_client_pos(focus(), focus()->pos()) + hint,
                                  waylandServer()->seat()->timestamp());
                }
            }
            return;
        }
        const QRegion r = getConstraintRegion(focus(), lock.data());
        if (canConstrain && r.contains(m_pos.toPoint())) {
            lock->setLocked(true);
            constraints.locked = true;

            // The client might cancel pointer locking from its side by unbinding the
            // LockedPointerV1. In this case the cached cursor position hint must be fetched before
            // the resource goes away
            notifiers.locked_pointer_destroyed = QObject::connect(
                lock.data(),
                &Wrapland::Server::LockedPointerV1::resourceDestroyed,
                this,
                [this, lock]() {
                    const auto hint = lock->cursorPositionHint();
                    if (hint.x() < 0 || hint.y() < 0 || !focus()) {
                        return;
                    }
                    // TODO(romangg): different client offset for Xwayland clients?
                    auto globalHint = win::frame_to_client_pos(focus(), focus()->pos()) + hint;
                    processMotion(globalHint, waylandServer()->seat()->timestamp());
                });
            // TODO: connect to region change - is it needed at all? If the pointer is locked it's
            // always in the region
        }
    } else {
        constraints.locked = false;
        disconnect_locked_pointer_destroyed_connection();
    }
}

void pointer_redirect::warp_xcb_on_surface_left(Wrapland::Server::Surface* newSurface)
{
    auto xc = waylandServer()->xWaylandConnection();
    if (!xc) {
        // No XWayland, no point in warping the x cursor
        return;
    }
    const auto c = kwinApp()->x11Connection();
    if (!c) {
        return;
    }
    static bool s_hasXWayland119 = xcb_get_setup(c)->release_number >= 11900000;
    if (s_hasXWayland119) {
        return;
    }
    if (newSurface && newSurface->client() == xc) {
        // new window is an X window
        return;
    }
    auto s = waylandServer()->seat()->pointers().get_focus().surface;
    if (!s || s->client() != xc) {
        // pointer was not on an X window
        return;
    }
    // warp pointer to 0/0 to trigger leave events on previously focused X window
    xcb_warp_pointer(c, XCB_WINDOW_NONE, kwinApp()->x11RootWindow(), 0, 0, 0, 0, 0, 0),
        xcb_flush(c);
}

QPointF pointer_redirect::apply_pointer_confinement(const QPointF& pos) const
{
    if (!focus()) {
        return pos;
    }
    auto s = focus()->surface();
    if (!s) {
        return pos;
    }
    auto cf = s->confinedPointer();
    if (!cf) {
        return pos;
    }
    if (!cf->isConfined()) {
        return pos;
    }

    const QRegion confinementRegion = getConstraintRegion(focus(), cf.data());
    if (confinementRegion.contains(pos.toPoint())) {
        return pos;
    }
    QPointF p = pos;
    // allow either x or y to pass
    p = QPointF(m_pos.x(), pos.y());
    if (confinementRegion.contains(p.toPoint())) {
        return p;
    }
    p = QPointF(pos.x(), m_pos.y());
    if (confinementRegion.contains(p.toPoint())) {
        return p;
    }

    return m_pos;
}

void pointer_redirect::update_position(const QPointF& pos)
{
    if (constraints.locked) {
        // locked pointer should not move
        return;
    }
    // verify that at least one screen contains the pointer position
    QPointF p = pos;
    if (!screenContainsPos(p)) {
        const QRectF unitedScreensGeometry = Screens::self()->geometry();
        p = confineToBoundingBox(p, unitedScreensGeometry);
        if (!screenContainsPos(p)) {
            const QRectF currentScreenGeometry
                = Screens::self()->geometry(Screens::self()->number(m_pos.toPoint()));
            p = confineToBoundingBox(p, currentScreenGeometry);
        }
    }
    p = apply_pointer_confinement(p);
    if (p == m_pos) {
        // didn't change due to confinement
        return;
    }
    // verify screen confinement
    if (!screenContainsPos(p)) {
        return;
    }
    m_pos = p;
    Q_EMIT kwinApp()->input->redirect->globalPointerChanged(m_pos);
}

void pointer_redirect::update_button(button_event const& event)
{
    m_buttons[event.key] = event.state;

    // update Qt buttons
    qt_buttons = Qt::NoButton;
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it) {
        if (it.value() == button_state::released) {
            continue;
        }
        qt_buttons |= button_to_qt_mouse_button(it.key());
    }

    Q_EMIT kwinApp()->input->redirect->pointerButtonStateChanged(event.key, event.state);
}

void pointer_redirect::warp(QPointF const& pos)
{
    processMotion(pos, waylandServer()->seat()->timestamp());
}

QPointF pointer_redirect::pos() const
{
    return m_pos;
}

Qt::MouseButtons pointer_redirect::buttons() const
{
    return qt_buttons;
}

void pointer_redirect::updateAfterScreenChange()
{
    if (screenContainsPos(m_pos)) {
        // pointer still on a screen
        return;
    }
    // pointer no longer on a screen, reposition to closes screen
    const QPointF pos
        = Screens::self()->geometry(Screens::self()->number(m_pos.toPoint())).center();
    // TODO: better way to get timestamps
    processMotion(pos, waylandServer()->seat()->timestamp());
}

QPointF pointer_redirect::position() const
{
    return m_pos.toPoint();
}

void pointer_redirect::setEffectsOverrideCursor(Qt::CursorShape shape)
{
    // current pointer focus window should get a leave event
    device_redirect_update(this);
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->setEffectsOverrideCursor(shape);
}

void pointer_redirect::removeEffectsOverrideCursor()
{
    // cursor position might have changed while there was an effect in place
    device_redirect_update(this);
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->removeEffectsOverrideCursor();
}

void pointer_redirect::setWindowSelectionCursor(QByteArray const& shape)
{
    // send leave to current pointer focus window
    update_to_reset();
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->setWindowSelectionCursor(shape);
}

void pointer_redirect::removeWindowSelectionCursor()
{
    device_redirect_update(this);
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->removeWindowSelectionCursor();
}

}
