/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/active_window.h"
#include "win/control.h"
#include "win/move.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>

namespace KWin
{

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
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void ActivationTest::init()
{
    Test::setup_wayland_connection();
    Test::cursor()->set_pos(QPoint(640, 512));
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
    QVERIFY(client1->control->active);

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active);

    win::move(client1, QPoint(300, 200));
    win::move(client2, QPoint(500, 200));

    // Create several clients on the right screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active);

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active);

    win::move(client3, QPoint(1380, 200));
    win::move(client4, QPoint(1580, 200));

    // Switch to window to the left.
    win::activate_window_direction(*Test::app()->base->space, win::direction::west);
    QVERIFY(client3->control->active);

    // Switch to window to the left.
    win::activate_window_direction(*Test::app()->base->space, win::direction::west);
    QVERIFY(client2->control->active);

    // Switch to window to the left.
    win::activate_window_direction(*Test::app()->base->space, win::direction::west);
    QVERIFY(client1->control->active);

    // Switch to window to the left.
    win::activate_window_direction(*Test::app()->base->space, win::direction::west);
    QVERIFY(client4->control->active);

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
    QVERIFY(client1->control->active);

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active);

    win::move(client1, QPoint(300, 200));
    win::move(client2, QPoint(500, 200));

    // Create several clients on the right screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active);

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active);

    win::move(client3, QPoint(1380, 200));
    win::move(client4, QPoint(1580, 200));

    // Switch to window to the right.
    win::activate_window_direction(*Test::app()->base->space, win::direction::east);
    QVERIFY(client1->control->active);

    // Switch to window to the right.
    win::activate_window_direction(*Test::app()->base->space, win::direction::east);
    QVERIFY(client2->control->active);

    // Switch to window to the right.
    win::activate_window_direction(*Test::app()->base->space, win::direction::east);
    QVERIFY(client3->control->active);

    // Switch to window to the right.
    win::activate_window_direction(*Test::app()->base->space, win::direction::east);
    QVERIFY(client4->control->active);

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
    QVERIFY(client1->control->active);

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active);

    win::move(client1, QPoint(200, 300));
    win::move(client2, QPoint(200, 500));

    // Create several clients on the bottom screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active);

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active);

    win::move(client3, QPoint(200, 1224));
    win::move(client4, QPoint(200, 1424));

    // Switch to window above.
    win::activate_window_direction(*Test::app()->base->space, win::direction::north);
    QVERIFY(client3->control->active);

    // Switch to window above.
    win::activate_window_direction(*Test::app()->base->space, win::direction::north);
    QVERIFY(client2->control->active);

    // Switch to window above.
    win::activate_window_direction(*Test::app()->base->space, win::direction::north);
    QVERIFY(client1->control->active);

    // Switch to window above.
    win::activate_window_direction(*Test::app()->base->space, win::direction::north);
    QVERIFY(client4->control->active);

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
    QVERIFY(client1->control->active);

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active);

    win::move(client1, QPoint(200, 300));
    win::move(client2, QPoint(200, 500));

    // Create several clients on the bottom screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active);

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active);

    win::move(client3, QPoint(200, 1224));
    win::move(client4, QPoint(200, 1424));

    // Switch to window below.
    win::activate_window_direction(*Test::app()->base->space, win::direction::south);
    QVERIFY(client1->control->active);

    // Switch to window below.
    win::activate_window_direction(*Test::app()->base->space, win::direction::south);
    QVERIFY(client2->control->active);

    // Switch to window below.
    win::activate_window_direction(*Test::app()->base->space, win::direction::south);
    QVERIFY(client3->control->active);

    // Switch to window below.
    win::activate_window_direction(*Test::app()->base->space, win::direction::south);
    QVERIFY(client4->control->active);

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
    QVERIFY(client1->control->active);

    QSignalSpy configureRequestedSpy1(shellSurface1.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy1.isValid());

    QVERIFY(configureRequestedSpy1.wait());
    win::active_window_maximize(*Test::app()->base->space);

    QVERIFY(configureRequestedSpy1.wait());

    QSignalSpy geometryChangedSpy1(client1->qobject.get(),
                                   &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy1.isValid());

    shellSurface1->ackConfigure(configureRequestedSpy1.last().front().value<quint32>());
    Test::render(surface1, shellSurface1->get_configure_data().size, Qt::red);

    QVERIFY(geometryChangedSpy1.wait());
    QCOMPARE(client1->maximizeMode(), win::maximize_mode::full);

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active);

    QSignalSpy configureRequestedSpy2(shellSurface2.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy2.isValid());

    QVERIFY(configureRequestedSpy2.wait());
    win::active_window_maximize(*Test::app()->base->space);

    QVERIFY(configureRequestedSpy2.wait());

    QSignalSpy geometryChangedSpy2(client2->qobject.get(),
                                   &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy2.isValid());

    shellSurface2->ackConfigure(configureRequestedSpy2.last().front().value<quint32>());
    Test::render(surface2, shellSurface2->get_configure_data().size, Qt::red);

    QVERIFY(geometryChangedSpy2.wait());

    auto const stackingOrder = Test::app()->base->space->stacking.order.stack;
    QVERIFY(index_of(stackingOrder, Test::space::window_t(client1))
            < index_of(stackingOrder, Test::space::window_t(client2)));
    QCOMPARE(client1->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client2->maximizeMode(), win::maximize_mode::full);

    // Create several clients on the right screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active);

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active);

    win::move(client3, QPoint(1380, 200));
    win::move(client4, QPoint(1580, 200));

    // Switch to window to the left.
    win::activate_window_direction(*Test::app()->base->space, win::direction::west);
    QVERIFY(client3->control->active);

    // Switch to window to the left.
    win::activate_window_direction(*Test::app()->base->space, win::direction::west);
    QVERIFY(client2->control->active);

    // Switch to window to the left.
    win::activate_window_direction(*Test::app()->base->space, win::direction::west);
    QVERIFY(client4->control->active);

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
    QVERIFY(client1->control->active);

    QSignalSpy configureRequestedSpy1(shellSurface1.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy1.isValid());

    QVERIFY(configureRequestedSpy1.wait());
    win::active_window_set_fullscreen(*Test::app()->base->space);

    QVERIFY(configureRequestedSpy1.wait());

    QSignalSpy geometryChangedSpy1(client1->qobject.get(),
                                   &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy1.isValid());

    shellSurface1->ackConfigure(configureRequestedSpy1.last().front().value<quint32>());
    Test::render(surface1, shellSurface1->get_configure_data().size, Qt::red);
    QVERIFY(geometryChangedSpy1.wait());

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->control->active);

    QSignalSpy configureRequestedSpy2(shellSurface2.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy2.isValid());

    QVERIFY(configureRequestedSpy2.wait());
    win::active_window_set_fullscreen(*Test::app()->base->space);

    QVERIFY(configureRequestedSpy2.wait());

    QSignalSpy geometryChangedSpy2(client2->qobject.get(),
                                   &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy2.isValid());

    shellSurface2->ackConfigure(configureRequestedSpy2.last().front().value<quint32>());
    Test::render(surface2, shellSurface2->get_configure_data().size, Qt::red);

    QVERIFY(geometryChangedSpy2.wait());

    auto const stackingOrder = Test::app()->base->space->stacking.order.stack;
    QVERIFY(index_of(stackingOrder, Test::space::window_t(client1))
            < index_of(stackingOrder, Test::space::window_t(client2)));
    QVERIFY(client1->control->fullscreen);
    QVERIFY(client2->control->fullscreen);

    // Create several clients on the bottom screen.
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    QVERIFY(client3);
    QVERIFY(client3->control->active);

    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
    QVERIFY(client4);
    QVERIFY(client4->control->active);

    win::move(client3, QPoint(200, 1224));
    win::move(client4, QPoint(200, 1424));

    // Switch to window above.
    win::activate_window_direction(*Test::app()->base->space, win::direction::north);
    QVERIFY(client3->control->active);

    // Switch to window above.
    win::activate_window_direction(*Test::app()->base->space, win::direction::north);
    QVERIFY(client2->control->active);

    // Switch to window above.
    win::activate_window_direction(*Test::app()->base->space, win::direction::north);
    QVERIFY(client4->control->active);

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
    auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {1280, 0, 1280, 1024}};
    Test::app()->set_outputs(geometries);
}

void ActivationTest::stackScreensVertically()
{
    auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {0, 1024, 1280, 1024}};
    Test::app()->set_outputs(geometries);
}

}

WAYLANDTEST_MAIN(KWin::ActivationTest)
#include "activation_test.moc"
