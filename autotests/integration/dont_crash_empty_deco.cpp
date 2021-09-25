/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "input/cursor.h"
#include "kwin_wayland_test.h"
#include "platform.h"
#include "render/compositor.h"
#include "scene.h"
#include "screenedge.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"
#include <kwineffects.h>

#include "win/deco.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <KDecoration2/Decoration>

#include <linux/input.h>

namespace KWin
{

class DontCrashEmptyDecorationTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void testBug361551();
};

void DontCrashEmptyDecorationTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());
    kwinApp()->platform->setInitialWindowSize(QSize(1280, 1024));

    // this test needs to enforce OpenGL compositing to get into the crashy condition
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    kwinApp()->start();
    QMetaObject::invokeMethod(
        kwinApp()->platform, "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    QVERIFY(startup_spy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));

    auto scene = render::compositor::self()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGL2Compositing);
}

void DontCrashEmptyDecorationTest::init()
{
    screens()->setCurrent(0);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void DontCrashEmptyDecorationTest::testBug361551()
{
    // this test verifies that resizing an X11 window to an invalid size does not result in crash on
    // unmap when the DecorationRenderer gets copied to the Deleted there a repaint is scheduled and
    // the resulting texture is invalid if the window size is invalid

    // create an xcb window
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    QVERIFY(!xcb_connection_has_error(c));

    xcb_window_t w = xcb_generate_id(c);
    xcb_create_window(c,
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
                      0,
                      0,
                      10,
                      10,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      0,
                      nullptr);
    xcb_map_window(c, w);
    xcb_flush(c);

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(win::decoration(client));

    // let's set a stupid geometry
    client->setFrameGeometry(QRect(0, 0, 0, 0));
    QCOMPARE(client->frameGeometry(), QRect(0, 0, 0, 0));

    // and destroy the window again
    xcb_unmap_window(c, w);
    xcb_destroy_window(c, w);
    xcb_flush(c);
    xcb_disconnect(c);

    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::DontCrashEmptyDecorationTest)
#include "dont_crash_empty_deco.moc"
