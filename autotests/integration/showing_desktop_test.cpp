/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "win/activation.h"
#include "win/net.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/surface.h>

using namespace Wrapland::Client;

namespace KWin
{

class ShowingDesktopTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testRestoreFocus();
    void testRestoreFocusWithDesktopWindow();
};

void ShowingDesktopTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void ShowingDesktopTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::plasma_shell);
}

void ShowingDesktopTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void ShowingDesktopTest::testRestoreFocus()
{
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    QVERIFY(surface1);
    QVERIFY(shellSurface1);

    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    QVERIFY(surface2);
    QVERIFY(shellSurface2);

    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client1 != client2);

    QCOMPARE(Test::get_wayland_window(Test::app()->base->space->stacking.active), client2);
    win::toggle_show_desktop(*Test::app()->base->space);
    QVERIFY(Test::app()->base->space->showing_desktop);
    win::toggle_show_desktop(*Test::app()->base->space);
    QVERIFY(!Test::app()->base->space->showing_desktop);

    QVERIFY(Test::get_wayland_window(Test::app()->base->space->stacking.active));
    QCOMPARE(Test::get_wayland_window(Test::app()->base->space->stacking.active), client2);
}

void ShowingDesktopTest::testRestoreFocusWithDesktopWindow()
{
    // first create a desktop window

    std::unique_ptr<Surface> desktopSurface(Test::create_surface());
    QVERIFY(desktopSurface);
    std::unique_ptr<XdgShellToplevel> desktopShellSurface(
        Test::create_xdg_shell_toplevel(desktopSurface));
    QVERIFY(desktopShellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(
        Test::get_client().interfaces.plasma_shell->createSurface(desktopSurface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::Desktop);

    auto desktop = Test::render_and_wait_for_shown(desktopSurface, QSize(100, 50), Qt::blue);
    QVERIFY(desktop);
    QVERIFY(win::is_desktop(desktop));

    // now create some windows
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    QVERIFY(surface1);
    QVERIFY(shellSurface1);

    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    QVERIFY(surface2);
    QVERIFY(shellSurface2);

    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client1 != client2);

    QCOMPARE(Test::get_wayland_window(Test::app()->base->space->stacking.active), client2);
    win::toggle_show_desktop(*Test::app()->base->space);
    QVERIFY(Test::app()->base->space->showing_desktop);
    QCOMPARE(Test::get_wayland_window(Test::app()->base->space->stacking.active), desktop);
    win::toggle_show_desktop(*Test::app()->base->space);
    QVERIFY(!Test::app()->base->space->showing_desktop);

    QVERIFY(Test::get_wayland_window(Test::app()->base->space->stacking.active));
    QCOMPARE(Test::get_wayland_window(Test::app()->base->space->stacking.active), client2);
}

}

WAYLANDTEST_MAIN(KWin::ShowingDesktopTest)
#include "showing_desktop_test.moc"
