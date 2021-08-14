/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>

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
#include "platform.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/net.h"
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
    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform->setInitialWindowSize(QSize(1280, 1024));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.size() || workspaceCreatedSpy.wait());
}

void ShowingDesktopTest::init()
{
    Test::setup_wayland_connection(Test::AdditionalWaylandInterface::PlasmaShell);
}

void ShowingDesktopTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void ShowingDesktopTest::testRestoreFocus()
{
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client1 != client2);

    QCOMPARE(workspace()->activeClient(), client2);
    workspace()->slotToggleShowDesktop();
    QVERIFY(workspace()->showingDesktop());
    workspace()->slotToggleShowDesktop();
    QVERIFY(!workspace()->showingDesktop());

    QVERIFY(workspace()->activeClient());
    QCOMPARE(workspace()->activeClient(), client2);
}

void ShowingDesktopTest::testRestoreFocusWithDesktopWindow()
{
    // first create a desktop window

    std::unique_ptr<Surface> desktopSurface(Test::create_surface());
    QVERIFY(desktopSurface);
    std::unique_ptr<XdgShellToplevel> desktopShellSurface(
        Test::create_xdg_shell_toplevel(desktopSurface));
    QVERIFY(desktopSurface);
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
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client1 != client2);

    QCOMPARE(workspace()->activeClient(), client2);
    workspace()->slotToggleShowDesktop();
    QVERIFY(workspace()->showingDesktop());
    QCOMPARE(workspace()->activeClient(), desktop);
    workspace()->slotToggleShowDesktop();
    QVERIFY(!workspace()->showingDesktop());

    QVERIFY(workspace()->activeClient());
    QCOMPARE(workspace()->activeClient(), client2);
}

}

WAYLANDTEST_MAIN(KWin::ShowingDesktopTest)
#include "showing_desktop_test.moc"
