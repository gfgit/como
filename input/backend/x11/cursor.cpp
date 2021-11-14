/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor.h"
#include "utils.h"
#include "xcbutils.h"
#include "xfixes_cursor_event_filter.h"

#include <QAbstractEventDispatcher>
#include <QTimer>

#include <xcb/xcb_cursor.h>

namespace KWin::input::backend::x11
{

cursor::cursor(bool xInputSupport)
    : input::cursor()
    , m_timeStamp(XCB_TIME_CURRENT_TIME)
    , m_buttonMask(0)
    , m_resetTimeStampTimer(new QTimer(this))
    , m_mousePollingTimer(new QTimer(this))
    , m_hasXInput(xInputSupport)
    , m_needsPoll(false)
{
    m_resetTimeStampTimer->setSingleShot(true);
    QObject::connect(m_resetTimeStampTimer, &QTimer::timeout, this, &cursor::reset_time_stamp);
    // TODO: How often do we really need to poll?
    m_mousePollingTimer->setInterval(50);
    QObject::connect(m_mousePollingTimer, &QTimer::timeout, this, &cursor::mouse_polled);

    QObject::connect(this, &input::cursor::theme_changed, this, [this] { m_cursors.clear(); });

    if (m_hasXInput) {
        QObject::connect(qApp->eventDispatcher(),
                         &QAbstractEventDispatcher::aboutToBlock,
                         this,
                         &cursor::about_to_block);
    }

#ifndef KCMRULES
    QObject::connect(kwinApp(), &Application::startup_finished, this, [this] {
        if (Xcb::Extensions::self()->isFixesAvailable()) {
            m_xfixesFilter = std::make_unique<xfixes_cursor_event_filter>(this);
        }
    });
#endif
}

PlatformCursorImage cursor::platform_image() const
{
    auto c = kwinApp()->x11Connection();

    QScopedPointer<xcb_xfixes_get_cursor_image_reply_t, QScopedPointerPodDeleter> cursor(
        xcb_xfixes_get_cursor_image_reply(c, xcb_xfixes_get_cursor_image_unchecked(c), nullptr));
    if (cursor.isNull()) {
        return PlatformCursorImage();
    }

    QImage qcursorimg((uchar*)xcb_xfixes_get_cursor_image_cursor_image(cursor.data()),
                      cursor->width,
                      cursor->height,
                      QImage::Format_ARGB32_Premultiplied);

    // deep copy of image as the data is going to be freed
    return PlatformCursorImage(qcursorimg.copy(), QPoint(cursor->xhot, cursor->yhot));
}

cursor::~cursor()
{
}

void cursor::do_set_pos()
{
    auto const& pos = current_pos();
    xcb_warp_pointer(connection(), XCB_WINDOW_NONE, rootWindow(), 0, 0, 0, 0, pos.x(), pos.y());
    // call default implementation to emit signal
    input::cursor::do_set_pos();
}

void cursor::do_get_pos()
{
    if (m_timeStamp != XCB_TIME_CURRENT_TIME && m_timeStamp == xTime()) {
        // time stamps did not change, no need to query again
        return;
    }
    m_timeStamp = xTime();
    Xcb::Pointer pointer(rootWindow());
    if (pointer.isNull()) {
        return;
    }
    m_buttonMask = pointer->mask;
    update_pos(pointer->root_x, pointer->root_y);
    m_resetTimeStampTimer->start(0);
}

void cursor::reset_time_stamp()
{
    m_timeStamp = XCB_TIME_CURRENT_TIME;
}

void cursor::about_to_block()
{
    if (m_needsPoll) {
        mouse_polled();
        m_needsPoll = false;
    }
}

void cursor::do_start_mouse_polling()
{
    if (!m_hasXInput) {
        m_mousePollingTimer->start();
    }
}

void cursor::do_stop_mouse_polling()
{
    if (!m_hasXInput) {
        m_mousePollingTimer->stop();
    }
}

void cursor::do_start_image_tracking()
{
    xcb_xfixes_select_cursor_input(
        connection(), rootWindow(), XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
}

void cursor::do_stop_image_tracking()
{
    xcb_xfixes_select_cursor_input(connection(), rootWindow(), 0);
}

void cursor::do_show()
{
    xcb_xfixes_show_cursor(kwinApp()->x11Connection(), kwinApp()->x11RootWindow());
}

void cursor::do_hide()
{
    xcb_xfixes_hide_cursor(kwinApp()->x11Connection(), kwinApp()->x11RootWindow());
}

void cursor::mouse_polled()
{
    static auto lastPos = current_pos();
    static uint16_t lastMask = m_buttonMask;
    do_get_pos(); // Update if needed
    if (lastPos != current_pos() || lastMask != m_buttonMask) {
        Q_EMIT mouse_changed(current_pos(),
                             lastPos,
                             x11ToQtMouseButtons(m_buttonMask),
                             x11ToQtMouseButtons(lastMask),
                             x11ToQtKeyboardModifiers(m_buttonMask),
                             x11ToQtKeyboardModifiers(lastMask));
        lastPos = current_pos();
        lastMask = m_buttonMask;
    }
}

xcb_cursor_t cursor::x11_cursor(input::cursor_shape shape)
{
    return x11_cursor(shape.name());
}

xcb_cursor_t cursor::x11_cursor(const QByteArray& name)
{
    auto it = m_cursors.constFind(name);
    if (it != m_cursors.constEnd()) {
        return it.value();
    }
    return create_cursor(name);
}

xcb_cursor_t cursor::create_cursor(const QByteArray& name)
{
    if (name.isEmpty()) {
        return XCB_CURSOR_NONE;
    }
    xcb_cursor_context_t* ctx;
    if (xcb_cursor_context_new(connection(), defaultScreen(), &ctx) < 0) {
        return XCB_CURSOR_NONE;
    }
    xcb_cursor_t cursor = xcb_cursor_load_cursor(ctx, name.constData());
    if (cursor == XCB_CURSOR_NONE) {
        const auto& names = alternative_names(name);
        for (auto cit = names.begin(); cit != names.end(); ++cit) {
            cursor = xcb_cursor_load_cursor(ctx, (*cit).constData());
            if (cursor != XCB_CURSOR_NONE) {
                break;
            }
        }
    }
    if (cursor != XCB_CURSOR_NONE) {
        m_cursors.insert(name, cursor);
    }
    xcb_cursor_context_free(ctx);
    return cursor;
}

void cursor::notify_cursor_changed()
{
    if (!is_image_tracking()) {
        // cursor change tracking is currently disabled, so don't emit signal
        return;
    }
    Q_EMIT image_changed();
}

}
