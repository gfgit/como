/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event.h"

namespace KWin::input
{

MouseEvent::MouseEvent(QEvent::Type type,
                       const QPointF& pos,
                       Qt::MouseButton button,
                       Qt::MouseButtons buttons,
                       Qt::KeyboardModifiers modifiers,
                       quint32 timestamp,
                       const QSizeF& delta,
                       const QSizeF& deltaNonAccelerated,
                       quint64 timestampMicroseconds,
                       pointer* device)
    : QMouseEvent(type, pos, pos, button, buttons, modifiers)
    , m_delta(delta)
    , m_deltaUnccelerated(deltaNonAccelerated)
    , m_timestampMicroseconds(timestampMicroseconds)
    , m_device(device)
{
    setTimestamp(timestamp);
}

WheelEvent::WheelEvent(const QPointF& pos,
                       qreal delta,
                       qint32 discreteDelta,
                       Qt::Orientation orientation,
                       Qt::MouseButtons buttons,
                       Qt::KeyboardModifiers modifiers,
                       redirect::PointerAxisSource source,
                       quint32 timestamp,
                       pointer* device)
    : QWheelEvent(pos,
                  pos,
                  QPoint(),
                  (orientation == Qt::Horizontal) ? QPoint(delta, 0) : QPoint(0, delta),
                  delta,
                  orientation,
                  buttons,
                  modifiers)
    , m_device(device)
    , m_orientation(orientation)
    , m_delta(delta)
    , m_discreteDelta(discreteDelta)
    , m_source(source)
{
    setTimestamp(timestamp);
}

KeyEvent::KeyEvent(QEvent::Type type,
                   Qt::Key key,
                   Qt::KeyboardModifiers modifiers,
                   quint32 code,
                   quint32 keysym,
                   const QString& text,
                   bool autorepeat,
                   quint32 timestamp,
                   keyboard* device)
    : QKeyEvent(type, key, modifiers, code, keysym, 0, text, autorepeat)
    , m_device(device)
{
    setTimestamp(timestamp);
}

SwitchEvent::SwitchEvent(State state,
                         quint32 timestamp,
                         quint64 timestampMicroseconds,
                         switch_device* device)
    : QInputEvent(QEvent::User)
    , m_state(state)
    , m_timestampMicroseconds(timestampMicroseconds)
    , m_device(device)
{
    setTimestamp(timestamp);
}

}
