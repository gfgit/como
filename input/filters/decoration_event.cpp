/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "decoration_event.h"

#include "helpers.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/touch_redirect.h"
#include "main.h"
#include "wayland_server.h"
#include "win/deco.h"
#include "win/input.h"
#include <input/qt_event.h>

#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

bool decoration_event_filter::button(button_event const& event)
{
    auto decoration = kwinApp()->input->redirect->pointer()->decoration();
    if (!decoration) {
        return false;
    }

    auto const action_result = perform_mouse_modifier_action(event, decoration->client());
    if (action_result.first) {
        return action_result.second;
    }

    auto const global_pos = kwinApp()->input->redirect->globalPointer();
    auto const local_pos = global_pos - decoration->client()->pos();

    auto qt_type = event.state == button_state::pressed ? QEvent::MouseButtonPress
                                                        : QEvent::MouseButtonRelease;
    auto qt_event = QMouseEvent(qt_type,
                                local_pos,
                                global_pos,
                                button_to_qt_mouse_button(event.key),
                                kwinApp()->input->redirect->pointer()->buttons(),
                                kwinApp()->input->redirect->keyboard()->modifiers());
    qt_event.setAccepted(false);

    QCoreApplication::sendEvent(decoration->decoration(), &qt_event);
    if (!qt_event.isAccepted() && event.state == button_state::pressed) {
        win::process_decoration_button_press(decoration->client(), &qt_event, false);
    }
    if (event.state == button_state::released) {
        win::process_decoration_button_release(decoration->client(), &qt_event);
    }
    return true;
}

bool decoration_event_filter::motion([[maybe_unused]] motion_event const& event)
{
    auto decoration = kwinApp()->input->redirect->pointer()->decoration();
    if (!decoration) {
        return false;
    }

    auto const global_pos = kwinApp()->input->redirect->globalPointer();
    auto const local_pos = global_pos - decoration->client()->pos();

    auto qt_event = QHoverEvent(QEvent::HoverMove, local_pos, local_pos);
    QCoreApplication::instance()->sendEvent(decoration->decoration(), &qt_event);
    win::process_decoration_move(decoration->client(), local_pos.toPoint(), global_pos.toPoint());
    return true;
}

bool decoration_event_filter::axis(axis_event const& event)
{
    auto decoration = kwinApp()->input->redirect->pointer()->decoration();
    if (!decoration) {
        return false;
    }

    auto window = decoration->client();

    if (event.orientation == axis_orientation::vertical) {
        // client window action only on vertical scrolling
        auto const actionResult = perform_wheel_action(event, window);
        if (actionResult.first) {
            return actionResult.second;
        }
    }

    auto qt_event = axis_to_qt_event(event);
    auto adapted_qt_event = QWheelEvent(qt_event.pos() - window->pos(),
                                        qt_event.pos(),
                                        QPoint(),
                                        qt_event.angleDelta(),
                                        qt_event.delta(),
                                        qt_event.orientation(),
                                        qt_event.buttons(),
                                        qt_event.modifiers());

    adapted_qt_event.setAccepted(false);
    QCoreApplication::sendEvent(decoration, &adapted_qt_event);

    if (adapted_qt_event.isAccepted()) {
        return true;
    }

    if ((event.orientation == axis_orientation::vertical)
        && win::titlebar_positioned_under_mouse(window)) {
        window->performMouseCommand(options->operationTitlebarMouseWheel(event.delta * -1),
                                    kwinApp()->input->redirect->pointer()->pos().toPoint());
    }
    return true;
}

bool decoration_event_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (seat->touches().is_in_progress()) {
        return false;
    }
    if (kwinApp()->input->redirect->touch()->decorationPressId() != -1) {
        // already on a decoration, ignore further touch points, but filter out
        return true;
    }
    seat->setTimestamp(time);
    auto decoration = kwinApp()->input->redirect->touch()->decoration();
    if (!decoration) {
        return false;
    }

    kwinApp()->input->redirect->touch()->setDecorationPressId(id);
    m_lastGlobalTouchPos = pos;
    m_lastLocalTouchPos = pos - decoration->client()->pos();

    QHoverEvent hoverEvent(QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
    QCoreApplication::sendEvent(decoration->decoration(), &hoverEvent);

    QMouseEvent e(QEvent::MouseButtonPress,
                  m_lastLocalTouchPos,
                  pos,
                  Qt::LeftButton,
                  Qt::LeftButton,
                  kwinApp()->input->redirect->keyboardModifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(decoration->decoration(), &e);
    if (!e.isAccepted()) {
        win::process_decoration_button_press(decoration->client(), &e, false);
    }
    return true;
}

bool decoration_event_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(time)
    auto decoration = kwinApp()->input->redirect->touch()->decoration();
    if (!decoration) {
        return false;
    }
    if (kwinApp()->input->redirect->touch()->decorationPressId() == -1) {
        return false;
    }
    if (kwinApp()->input->redirect->touch()->decorationPressId() != qint32(id)) {
        // ignore, but filter out
        return true;
    }
    m_lastGlobalTouchPos = pos;
    m_lastLocalTouchPos = pos - decoration->client()->pos();

    QHoverEvent e(QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
    QCoreApplication::instance()->sendEvent(decoration->decoration(), &e);
    win::process_decoration_move(
        decoration->client(), m_lastLocalTouchPos.toPoint(), pos.toPoint());
    return true;
}

bool decoration_event_filter::touchUp(qint32 id, quint32 time)
{
    Q_UNUSED(time);
    auto decoration = kwinApp()->input->redirect->touch()->decoration();
    if (!decoration) {
        return false;
    }
    if (kwinApp()->input->redirect->touch()->decorationPressId() == -1) {
        return false;
    }
    if (kwinApp()->input->redirect->touch()->decorationPressId() != qint32(id)) {
        // ignore, but filter out
        return true;
    }

    // send mouse up
    QMouseEvent e(QEvent::MouseButtonRelease,
                  m_lastLocalTouchPos,
                  m_lastGlobalTouchPos,
                  Qt::LeftButton,
                  Qt::MouseButtons(),
                  kwinApp()->input->redirect->keyboardModifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(decoration->decoration(), &e);
    win::process_decoration_button_release(decoration->client(), &e);

    QHoverEvent leaveEvent(QEvent::HoverLeave, QPointF(), QPointF());
    QCoreApplication::sendEvent(decoration->decoration(), &leaveEvent);

    m_lastGlobalTouchPos = QPointF();
    m_lastLocalTouchPos = QPointF();
    kwinApp()->input->redirect->touch()->setDecorationPressId(-1);
    return true;
}

}
