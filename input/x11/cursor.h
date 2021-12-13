/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/cursor.h"

#include <memory>

namespace KWin::input::x11
{
class xfixes_cursor_event_filter;

class KWIN_EXPORT cursor : public input::cursor
{
    Q_OBJECT
public:
    cursor(bool xInputSupport = false);
    ~cursor() override;

    PlatformCursorImage platform_image() const override;

    void schedule_poll()
    {
        m_needsPoll = true;
    }

    /**
     * @internal
     *
     * Called from X11 event handler.
     */
    void notify_cursor_changed();

protected:
    xcb_cursor_t x11_cursor(input::cursor_shape shape) override;
    xcb_cursor_t x11_cursor(const QByteArray& name) override;

    void do_set_pos() override;
    void do_get_pos() override;

    void do_start_mouse_polling() override;
    void do_stop_mouse_polling() override;

    void do_start_image_tracking() override;
    void do_stop_image_tracking() override;

    void do_show() override;
    void do_hide() override;

private:
    /**
     * Because of QTimer's and the impossibility to get events for all mouse
     * movements (at least I haven't figured out how) the position needs
     * to be also refetched after each return to the event loop.
     */
    void reset_time_stamp();
    void mouse_polled();
    void about_to_block();

    xcb_cursor_t create_cursor(const QByteArray& name);
    QHash<QByteArray, xcb_cursor_t> m_cursors;
    xcb_timestamp_t m_timeStamp;
    uint16_t m_buttonMask;
    QTimer* m_resetTimeStampTimer;
    QTimer* m_mousePollingTimer;
    bool m_hasXInput;
    bool m_needsPoll;

    std::unique_ptr<xfixes_cursor_event_filter> m_xfixesFilter;
};

}
