/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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

#include "kwin_wayland_test.h"

#include "input/cursor.h"
#include "platform.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/control.h"
#include "win/move.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_activation-0");

class ActivationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSwitchToWindowToLeft();
    void testSwitchToWindowToRight();
    void testSwitchToWindowAbove();
    void testSwitchToWindowBelow();
    void testSwitchToWindowMaximized();
    void testSwitchToWindowFullScreen();

private:
    void stackScreensHorizontally();
    void stackScreensVertically();
};

void ActivationTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));

    kwinApp()->start();
    QMetaObject::invokeMethod(
        kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));
    QVERIFY(workspaceCreatedSpy.size() || workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
    waylandServer()->initWorkspace();
}

void ActivationTest::init()
{
    Test::setup_wayland_connection();

    screens()->setCurrent(0);
    input::cursor::setPos(QPoint(640, 512));
}

void ActivationTest::cleanup()
{
    Test::destroy_wayland_connection();

    stackScreensHorizontally();
}

void ActivationTest::testSwitchToWindowToLeft()
{
    // This test verifies that "Switch to Window to the Left" shortcut works.

    using namespace Wrapland::Client;

    // Prepare the test environment.
    stackScreensHorizontally();

    // Create several clients on the left screen.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active());

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active());

    win::move(client1, QPoint(300, 200));
    win::move(client2, QPoint(500, 200));

    // Create several clients on the right screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active());

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active());

    win::move(client3, QPoint(1380, 200));
    win::move(client4, QPoint(1580, 200));

    // Switch to window to the left.
    workspace()->switchWindow(Workspace::DirectionWest);
    QVERIFY(client3->control->active());

    // Switch to window to the left.
    workspace()->switchWindow(Workspace::DirectionWest);
    QVERIFY(client2->control->active());

    // Switch to window to the left.
    workspace()->switchWindow(Workspace::DirectionWest);
    QVERIFY(client1->control->active());

    // Switch to window to the left.
    workspace()->switchWindow(Workspace::DirectionWest);
    QVERIFY(client4->control->active());

    // Destroy all clients.
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface3.reset();
    QVERIFY(Test::wait_for_destroyed(client3));
    shellSurface4.reset();
    QVERIFY(Test::wait_for_destroyed(client4));
}

void ActivationTest::testSwitchToWindowToRight()
{
    // This test verifies that "Switch to Window to the Right" shortcut works.

    using namespace Wrapland::Client;

    // Prepare the test environment.
    stackScreensHorizontally();

    // Create several clients on the left screen.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active());

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active());

    win::move(client1, QPoint(300, 200));
    win::move(client2, QPoint(500, 200));

    // Create several clients on the right screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active());

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active());

    win::move(client3, QPoint(1380, 200));
    win::move(client4, QPoint(1580, 200));

    // Switch to window to the right.
    workspace()->switchWindow(Workspace::DirectionEast);
    QVERIFY(client1->control->active());

    // Switch to window to the right.
    workspace()->switchWindow(Workspace::DirectionEast);
    QVERIFY(client2->control->active());

    // Switch to window to the right.
    workspace()->switchWindow(Workspace::DirectionEast);
    QVERIFY(client3->control->active());

    // Switch to window to the right.
    workspace()->switchWindow(Workspace::DirectionEast);
    QVERIFY(client4->control->active());

    // Destroy all clients.
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface3.reset();
    QVERIFY(Test::wait_for_destroyed(client3));
    shellSurface4.reset();
    QVERIFY(Test::wait_for_destroyed(client4));
}

void ActivationTest::testSwitchToWindowAbove()
{
    // This test verifies that "Switch to Window Above" shortcut works.

    using namespace Wrapland::Client;

    // Prepare the test environment.
    stackScreensVertically();

    // Create several clients on the top screen.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active());

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active());

    win::move(client1, QPoint(200, 300));
    win::move(client2, QPoint(200, 500));

    // Create several clients on the bottom screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active());

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active());

    win::move(client3, QPoint(200, 1224));
    win::move(client4, QPoint(200, 1424));

    // Switch to window above.
    workspace()->switchWindow(Workspace::DirectionNorth);
    QVERIFY(client3->control->active());

    // Switch to window above.
    workspace()->switchWindow(Workspace::DirectionNorth);
    QVERIFY(client2->control->active());

    // Switch to window above.
    workspace()->switchWindow(Workspace::DirectionNorth);
    QVERIFY(client1->control->active());

    // Switch to window above.
    workspace()->switchWindow(Workspace::DirectionNorth);
    QVERIFY(client4->control->active());

    // Destroy all clients.
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface3.reset();
    QVERIFY(Test::wait_for_destroyed(client3));
    shellSurface4.reset();
    QVERIFY(Test::wait_for_destroyed(client4));
}

void ActivationTest::testSwitchToWindowBelow()
{
    // This test verifies that "Switch to Window Bottom" shortcut works.

    using namespace Wrapland::Client;

    // Prepare the test environment.
    stackScreensVertically();

    // Create several clients on the top screen.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active());

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active());

    win::move(client1, QPoint(200, 300));
    win::move(client2, QPoint(200, 500));

    // Create several clients on the bottom screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active());

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active());

    win::move(client3, QPoint(200, 1224));
    win::move(client4, QPoint(200, 1424));

    // Switch to window below.
    workspace()->switchWindow(Workspace::DirectionSouth);
    QVERIFY(client1->control->active());

    // Switch to window below.
    workspace()->switchWindow(Workspace::DirectionSouth);
    QVERIFY(client2->control->active());

    // Switch to window below.
    workspace()->switchWindow(Workspace::DirectionSouth);
    QVERIFY(client3->control->active());

    // Switch to window below.
    workspace()->switchWindow(Workspace::DirectionSouth);
    QVERIFY(client4->control->active());

    // Destroy all clients.
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface3.reset();
    QVERIFY(Test::wait_for_destroyed(client3));
    shellSurface4.reset();
    QVERIFY(Test::wait_for_destroyed(client4));
}

void ActivationTest::testSwitchToWindowMaximized()
{
    // This test verifies that we switch to the top-most maximized client, i.e.
    // the one that user sees at the moment. See bug 411356.

    using namespace Wrapland::Client;

    // Prepare the test environment.
    stackScreensHorizontally();

    // Create several maximized clients on the left screen.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active());
    QSignalSpy configureRequestedSpy1(shellSurface1.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy1.wait());
    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy1.wait());
    QSignalSpy geometryChangedSpy1(client1, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy1.isValid());
    shellSurface1->ackConfigure(configureRequestedSpy1.last().at(2).value<quint32>());
    Test::render(surface1, configureRequestedSpy1.last().at(0).toSize(), Qt::red);
    QVERIFY(geometryChangedSpy1.wait());
    QCOMPARE(client1->maximizeMode(), win::maximize_mode::full);

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active());
    QSignalSpy configureRequestedSpy2(shellSurface2.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy2.wait());
    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy2.wait());
    QSignalSpy geometryChangedSpy2(client2, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy2.isValid());
    shellSurface2->ackConfigure(configureRequestedSpy2.last().at(2).value<quint32>());
    Test::render(surface2, configureRequestedSpy2.last().at(0).toSize(), Qt::red);
    QVERIFY(geometryChangedSpy2.wait());

    auto const stackingOrder = workspace()->stacking_order->sorted();
    QVERIFY(index_of(stackingOrder, client1) < index_of(stackingOrder, client2));
    QCOMPARE(client1->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client2->maximizeMode(), win::maximize_mode::full);

    // Create several clients on the right screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active());

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active());

    win::move(client3, QPoint(1380, 200));
    win::move(client4, QPoint(1580, 200));

    // Switch to window to the left.
    workspace()->switchWindow(Workspace::DirectionWest);
    QVERIFY(client3->control->active());

    // Switch to window to the left.
    workspace()->switchWindow(Workspace::DirectionWest);
    QVERIFY(client2->control->active());

    // Switch to window to the left.
    workspace()->switchWindow(Workspace::DirectionWest);
    QVERIFY(client4->control->active());

    // Destroy all clients.
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface3.reset();
    QVERIFY(Test::wait_for_destroyed(client3));
    shellSurface4.reset();
    QVERIFY(Test::wait_for_destroyed(client4));
}

void ActivationTest::testSwitchToWindowFullScreen()
{
    // This test verifies that we switch to the top-most fullscreen client, i.e.
    // the one that user sees at the moment. See bug 411356.

    using namespace Wrapland::Client;

    // Prepare the test environment.
    stackScreensVertically();

    // Create several maximized clients on the top screen.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active());
    QSignalSpy configureRequestedSpy1(shellSurface1.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy1.wait());
    workspace()->slotWindowFullScreen();
    QVERIFY(configureRequestedSpy1.wait());
    QSignalSpy geometryChangedSpy1(client1, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy1.isValid());
    shellSurface1->ackConfigure(configureRequestedSpy1.last().at(2).value<quint32>());
    Test::render(surface1, configureRequestedSpy1.last().at(0).toSize(), Qt::red);
    QVERIFY(geometryChangedSpy1.wait());

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active());
    QSignalSpy configureRequestedSpy2(shellSurface2.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy2.wait());
    workspace()->slotWindowFullScreen();
    QVERIFY(configureRequestedSpy2.wait());
    QSignalSpy geometryChangedSpy2(client2, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy2.isValid());
    shellSurface2->ackConfigure(configureRequestedSpy2.last().at(2).value<quint32>());
    Test::render(surface2, configureRequestedSpy2.last().at(0).toSize(), Qt::red);
    QVERIFY(geometryChangedSpy2.wait());

    auto const stackingOrder = workspace()->stacking_order->sorted();
    QVERIFY(index_of(stackingOrder, client1) < index_of(stackingOrder, client2));
    QVERIFY(client1->control->fullscreen());
    QVERIFY(client2->control->fullscreen());

    // Create several clients on the bottom screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active());

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active());

    win::move(client3, QPoint(200, 1224));
    win::move(client4, QPoint(200, 1424));

    // Switch to window above.
    workspace()->switchWindow(Workspace::DirectionNorth);
    QVERIFY(client3->control->active());

    // Switch to window above.
    workspace()->switchWindow(Workspace::DirectionNorth);
    QVERIFY(client2->control->active());

    // Switch to window above.
    workspace()->switchWindow(Workspace::DirectionNorth);
    QVERIFY(client4->control->active());

    // Destroy all clients.
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface3.reset();
    QVERIFY(Test::wait_for_destroyed(client3));
    shellSurface4.reset();
    QVERIFY(Test::wait_for_destroyed(client4));
}

void ActivationTest::stackScreensHorizontally()
{
    const QVector<QRect> screenGeometries{
        QRect(0, 0, 1280, 1024),
        QRect(1280, 0, 1280, 1024),
    };

    const QVector<int> screenScales{
        1,
        1,
    };

    QMetaObject::invokeMethod(kwinApp()->platform(),
                              "setVirtualOutputs",
                              Qt::DirectConnection,
                              Q_ARG(int, screenGeometries.count()),
                              Q_ARG(QVector<QRect>, screenGeometries),
                              Q_ARG(QVector<int>, screenScales));
}

void ActivationTest::stackScreensVertically()
{
    const QVector<QRect> screenGeometries{
        QRect(0, 0, 1280, 1024),
        QRect(0, 1024, 1280, 1024),
    };

    const QVector<int> screenScales{
        1,
        1,
    };

    QMetaObject::invokeMethod(kwinApp()->platform(),
                              "setVirtualOutputs",
                              Qt::DirectConnection,
                              Q_ARG(int, screenGeometries.count()),
                              Q_ARG(QVector<QRect>, screenGeometries),
                              Q_ARG(QVector<int>, screenScales));
}

}

WAYLANDTEST_MAIN(KWin::ActivationTest)
#include "activation_test.moc"
