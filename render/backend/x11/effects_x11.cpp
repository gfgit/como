/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010, 2011, 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects_x11.h"
#include "effects_mouse_interception_x11_filter.h"
#include "input/cursor.h"
#include "screenedge.h"
#include "screens.h"
#include "utils.h"
#include "win/x11/space.h"
#include "workspace.h"

#include <QDesktopWidget>

namespace KWin::render::backend::x11
{

EffectsHandlerImplX11::EffectsHandlerImplX11(render::compositor* compositor, render::scene* scene)
    : render::effects_handler_impl(compositor, scene)
{
    connect(this, &EffectsHandlerImplX11::screenGeometryChanged, this, [this](const QSize& size) {
        if (m_mouseInterceptionWindow.isValid()) {
            m_mouseInterceptionWindow.setGeometry(QRect(0, 0, size.width(), size.height()));
        }
    });
}

EffectsHandlerImplX11::~EffectsHandlerImplX11()
{
    // EffectsHandlerImpl tries to unload all effects when it's destroyed.
    // The routine that unloads effects makes some calls (indirectly) to
    // doUngrabKeyboard and doStopMouseInterception, which are virtual.
    // Given that any call to a virtual function in the destructor of a base
    // class will never go to a derived class, we have to unload effects
    // here. Yeah, this is quite a bit ugly but it's fine; someday, X11
    // will be dead (or not?).
    unloadAllEffects();
}

bool EffectsHandlerImplX11::doGrabKeyboard()
{
    bool ret = grabXKeyboard();
    if (!ret)
        return false;
    // Workaround for Qt 5.9 regression introduced with 2b34aefcf02f09253473b096eb4faffd3e62b5f4
    // we no longer get any events for the root window, one needs to call winId() on the desktop
    // window
    // TODO: change effects event handling to create the appropriate QKeyEvent without relying on Qt
    // as it's done already in the Wayland case.
    qApp->desktop()->winId();
    return ret;
}

void EffectsHandlerImplX11::doUngrabKeyboard()
{
    ungrabXKeyboard();
}

void EffectsHandlerImplX11::doStartMouseInterception(Qt::CursorShape shape)
{
    // NOTE: it is intended to not perform an XPointerGrab on X11. See documentation in
    // kwineffects.h The mouse grab is implemented by using a full screen input only window
    if (!m_mouseInterceptionWindow.isValid()) {
        const QSize& s = Screens::self()->size();
        const QRect geo(0, 0, s.width(), s.height());
        const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        const uint32_t values[] = {true,
                                   XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                       | XCB_EVENT_MASK_POINTER_MOTION};
        m_mouseInterceptionWindow.reset(Xcb::createInputWindow(geo, mask, values));
        defineCursor(shape);
    } else {
        defineCursor(shape);
    }
    m_mouseInterceptionWindow.map();
    m_mouseInterceptionWindow.raise();
    m_x11MouseInterception
        = std::make_unique<EffectsMouseInterceptionX11Filter>(m_mouseInterceptionWindow, this);
    // Raise electric border windows above the input windows
    // so they can still be triggered.
    ScreenEdges::self()->ensureOnTop();
}

void EffectsHandlerImplX11::doStopMouseInterception()
{
    m_mouseInterceptionWindow.unmap();
    m_x11MouseInterception.reset();
    win::x11::stack_screen_edges_under_override_redirect(workspace());
}

void EffectsHandlerImplX11::defineCursor(Qt::CursorShape shape)
{
    auto const c = input::get_cursor()->x11_cursor(shape);
    if (c != XCB_CURSOR_NONE) {
        m_mouseInterceptionWindow.defineCursor(c);
    }
}

void EffectsHandlerImplX11::doCheckInputWindowStacking()
{
    m_mouseInterceptionWindow.raise();
    // Raise electric border windows above the input windows
    // so they can still be triggered. TODO: Do both at once.
    ScreenEdges::self()->ensureOnTop();
}

}
