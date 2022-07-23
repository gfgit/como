/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/output_helpers.h"
#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/wayland/platform.h"
#include "main.h"

#include <Wrapland/Server/seat.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QTimer>

namespace KWin::input
{

class dpms_filter : public event_filter
{
public:
    explicit dpms_filter(wayland::platform* input)
        : event_filter()
        , input{input}
    {
    }

    bool button(button_event const& /*event*/) override
    {
        notify();
        return true;
    }

    bool motion(motion_event const& /*event*/) override
    {
        notify();
        return true;
    }

    bool axis(axis_event const& /*event*/) override
    {
        notify();
        return true;
    }

    bool key(key_event const& /*event*/) override
    {
        notify();
        return true;
    }

    bool touch_down(touch_down_event const& event) override
    {
        if (m_touchPoints.isEmpty()) {
            if (!m_doubleTapTimer.isValid()) {
                // This is the first tap.
                m_doubleTapTimer.start();
            } else {
                if (m_doubleTapTimer.elapsed() < qApp->doubleClickInterval()) {
                    m_secondTap = true;
                } else {
                    // Took too long. Let's consider it a new click.
                    m_doubleTapTimer.restart();
                }
            }
        } else {
            // Not a double tap.
            m_doubleTapTimer.invalidate();
            m_secondTap = false;
        }
        m_touchPoints << event.id;
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        m_touchPoints.removeAll(event.id);
        if (m_touchPoints.isEmpty() && m_doubleTapTimer.isValid() && m_secondTap) {
            if (m_doubleTapTimer.elapsed() < qApp->doubleClickInterval()) {
                waylandServer()->seat()->setTimestamp(event.base.time_msec);
                notify();
            }
            m_doubleTapTimer.invalidate();
            m_secondTap = false;
        }
        return true;
    }

    bool touch_motion(touch_motion_event const& /*event*/) override
    {
        // Ignore the event.
        return true;
    }

private:
    void notify()
    {
        // Queued to not modify the list of event filters while filtering.
        QTimer::singleShot(0, input, [this] { input->turn_outputs_on(); });
    }

    wayland::platform* input;
    QElapsedTimer m_doubleTapTimer;
    QVector<int32_t> m_touchPoints;
    bool m_secondTap = false;
};

}
