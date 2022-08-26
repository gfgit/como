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
#include "lib/app.h"

#include "base/wayland/server.h"
#include "base/x11/xcb/window.h"
#include "debug/console/wayland/wayland_console.h"
#include "win/control.h"
#include "win/deco.h"
#include "win/internal_window.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

#include <QPainter>
#include <QRasterWindow>

namespace KWin
{

class DebugConsoleTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void topLevelTest_data();
    void topLevelTest();
    void testX11Client();
    void testX11Unmanaged();
    void testWaylandClient();
    void testInternalWindow();
    void testClosingDebugConsole();
};

void DebugConsoleTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();
}

void DebugConsoleTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void DebugConsoleTest::topLevelTest_data()
{
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<bool>("expectedValid");

    // this tests various combinations of row/column on the top level whether they are valid
    // valid are rows 0-4 with column 0, everything else is invalid
    QTest::newRow("0/0") << 0 << 0 << true;
    QTest::newRow("0/1") << 0 << 1 << false;
    QTest::newRow("0/3") << 0 << 3 << false;
    QTest::newRow("1/0") << 1 << 0 << true;
    QTest::newRow("1/1") << 1 << 1 << false;
    QTest::newRow("1/3") << 1 << 3 << false;
    QTest::newRow("2/0") << 2 << 0 << true;
    QTest::newRow("3/0") << 3 << 0 << true;
    QTest::newRow("4/0") << 4 << 0 << false;
    QTest::newRow("100/0") << 4 << 0 << false;
}

void DebugConsoleTest::topLevelTest()
{
    debug::wayland_console_model model(*Test::app()->base.space);
    QCOMPARE(model.rowCount(QModelIndex()), 4);
    QCOMPARE(model.columnCount(QModelIndex()), 2);
    QFETCH(int, row);
    QFETCH(int, column);
    const QModelIndex index = model.index(row, column, QModelIndex());
    QTEST(index.isValid(), "expectedValid");
    if (index.isValid()) {
        QVERIFY(!model.parent(index).isValid());
        QVERIFY(model.data(index, Qt::DisplayRole).isValid());
        QCOMPARE(model.data(index, Qt::DisplayRole).userType(), int(QMetaType::QString));
        for (int i = Qt::DecorationRole; i <= Qt::UserRole; i++) {
            QVERIFY(!model.data(index, i).isValid());
        }
    }
}

void DebugConsoleTest::testX11Client()
{
    debug::wayland_console_model model(*Test::app()->base.space);
    QModelIndex x11TopLevelIndex = model.index(0, 0, QModelIndex());
    QVERIFY(x11TopLevelIndex.isValid());
    // we don't have any windows yet
    QCOMPARE(model.rowCount(x11TopLevelIndex), 0);
    QVERIFY(!model.hasChildren(x11TopLevelIndex));
    // child index must be invalid
    QVERIFY(!model.index(0, 0, x11TopLevelIndex).isValid());
    QVERIFY(!model.index(0, 1, x11TopLevelIndex).isValid());
    QVERIFY(!model.index(0, 2, x11TopLevelIndex).isValid());
    QVERIFY(!model.index(1, 0, x11TopLevelIndex).isValid());

    // start glxgears, to get a window, which should be added to the model
    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QVERIFY(rowsInsertedSpy.isValid());

    QProcess glxgears;
    glxgears.setProgram(QStringLiteral("glxgears"));
    glxgears.start();
    QVERIFY(glxgears.waitForStarted());

    QVERIFY(rowsInsertedSpy.wait());
    QCOMPARE(rowsInsertedSpy.count(), 1);
    QVERIFY(model.hasChildren(x11TopLevelIndex));
    QCOMPARE(model.rowCount(x11TopLevelIndex), 1);
    QCOMPARE(rowsInsertedSpy.first().at(0).value<QModelIndex>(), x11TopLevelIndex);
    QCOMPARE(rowsInsertedSpy.first().at(1).value<int>(), 0);
    QCOMPARE(rowsInsertedSpy.first().at(2).value<int>(), 0);

    QModelIndex clientIndex = model.index(0, 0, x11TopLevelIndex);
    QVERIFY(clientIndex.isValid());
    QCOMPARE(model.parent(clientIndex), x11TopLevelIndex);
    QVERIFY(model.hasChildren(clientIndex));
    QVERIFY(model.rowCount(clientIndex) != 0);
    QCOMPARE(model.columnCount(clientIndex), 2);
    // other indexes are still invalid
    QVERIFY(!model.index(0, 1, x11TopLevelIndex).isValid());
    QVERIFY(!model.index(0, 2, x11TopLevelIndex).isValid());
    QVERIFY(!model.index(1, 0, x11TopLevelIndex).isValid());

    // the clientIndex has children and those are properties
    for (int i = 0; i < model.rowCount(clientIndex); i++) {
        const QModelIndex propNameIndex = model.index(i, 0, clientIndex);
        QVERIFY(propNameIndex.isValid());
        QCOMPARE(model.parent(propNameIndex), clientIndex);
        QVERIFY(!model.hasChildren(propNameIndex));
        QVERIFY(!model.index(0, 0, propNameIndex).isValid());
        QVERIFY(model.data(propNameIndex, Qt::DisplayRole).isValid());
        QCOMPARE(model.data(propNameIndex, Qt::DisplayRole).userType(), int(QMetaType::QString));

        // and the value
        const QModelIndex propValueIndex = model.index(i, 1, clientIndex);
        QVERIFY(propValueIndex.isValid());
        QCOMPARE(model.parent(propValueIndex), clientIndex);
        QVERIFY(!model.index(0, 0, propValueIndex).isValid());
        QVERIFY(!model.hasChildren(propValueIndex));
        // TODO: how to test whether the values actually work?

        // and on third column we should not get an index any more
        QVERIFY(!model.index(i, 2, clientIndex).isValid());
    }
    // row after count should be invalid
    QVERIFY(!model.index(model.rowCount(clientIndex), 0, clientIndex).isValid());

    // creating a second model should be initialized directly with the X11 child
    debug::wayland_console_model model2(*Test::app()->base.space);
    QVERIFY(model2.hasChildren(model2.index(0, 0, QModelIndex())));

    // now close the window again, it should be removed from the model
    QSignalSpy rowsRemovedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(rowsRemovedSpy.isValid());

    glxgears.terminate();
    QVERIFY(glxgears.waitForFinished());

    QVERIFY(rowsRemovedSpy.wait());
    QCOMPARE(rowsRemovedSpy.count(), 1);
    QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), x11TopLevelIndex);
    QCOMPARE(rowsRemovedSpy.first().at(1).value<int>(), 0);
    QCOMPARE(rowsRemovedSpy.first().at(2).value<int>(), 0);

    // the child should be gone again
    QVERIFY(!model.hasChildren(x11TopLevelIndex));
    QVERIFY(!model2.hasChildren(model2.index(0, 0, QModelIndex())));
}

void DebugConsoleTest::testX11Unmanaged()
{
    debug::wayland_console_model model(*Test::app()->base.space);
    QModelIndex unmanagedTopLevelIndex = model.index(1, 0, QModelIndex());
    QVERIFY(unmanagedTopLevelIndex.isValid());
    // we don't have any windows yet
    QCOMPARE(model.rowCount(unmanagedTopLevelIndex), 0);
    QVERIFY(!model.hasChildren(unmanagedTopLevelIndex));
    // child index must be invalid
    QVERIFY(!model.index(0, 0, unmanagedTopLevelIndex).isValid());
    QVERIFY(!model.index(0, 1, unmanagedTopLevelIndex).isValid());
    QVERIFY(!model.index(0, 2, unmanagedTopLevelIndex).isValid());
    QVERIFY(!model.index(1, 0, unmanagedTopLevelIndex).isValid());

    // we need to create an unmanaged window
    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QVERIFY(rowsInsertedSpy.isValid());

    // let's create an override redirect window
    const uint32_t values[] = {true};
    base::x11::xcb::window window(QRect(0, 0, 10, 10), XCB_CW_OVERRIDE_REDIRECT, values);
    window.map();

    QVERIFY(rowsInsertedSpy.wait());
    QCOMPARE(rowsInsertedSpy.count(), 1);
    QVERIFY(model.hasChildren(unmanagedTopLevelIndex));
    QCOMPARE(model.rowCount(unmanagedTopLevelIndex), 1);
    QCOMPARE(rowsInsertedSpy.first().at(0).value<QModelIndex>(), unmanagedTopLevelIndex);
    QCOMPARE(rowsInsertedSpy.first().at(1).value<int>(), 0);
    QCOMPARE(rowsInsertedSpy.first().at(2).value<int>(), 0);

    QModelIndex clientIndex = model.index(0, 0, unmanagedTopLevelIndex);
    QVERIFY(clientIndex.isValid());
    QCOMPARE(model.parent(clientIndex), unmanagedTopLevelIndex);
    QVERIFY(model.hasChildren(clientIndex));
    QVERIFY(model.rowCount(clientIndex) != 0);
    QCOMPARE(model.columnCount(clientIndex), 2);
    // other indexes are still invalid
    QVERIFY(!model.index(0, 1, unmanagedTopLevelIndex).isValid());
    QVERIFY(!model.index(0, 2, unmanagedTopLevelIndex).isValid());
    QVERIFY(!model.index(1, 0, unmanagedTopLevelIndex).isValid());

    QCOMPARE(model.data(clientIndex, Qt::DisplayRole).toString(), QString::number(window));

    // the clientIndex has children and those are properties
    for (int i = 0; i < model.rowCount(clientIndex); i++) {
        const QModelIndex propNameIndex = model.index(i, 0, clientIndex);
        QVERIFY(propNameIndex.isValid());
        QCOMPARE(model.parent(propNameIndex), clientIndex);
        QVERIFY(!model.hasChildren(propNameIndex));
        QVERIFY(!model.index(0, 0, propNameIndex).isValid());
        QVERIFY(model.data(propNameIndex, Qt::DisplayRole).isValid());
        QCOMPARE(model.data(propNameIndex, Qt::DisplayRole).userType(), int(QMetaType::QString));

        // and the value
        const QModelIndex propValueIndex = model.index(i, 1, clientIndex);
        QVERIFY(propValueIndex.isValid());
        QCOMPARE(model.parent(propValueIndex), clientIndex);
        QVERIFY(!model.index(0, 0, propValueIndex).isValid());
        QVERIFY(!model.hasChildren(propValueIndex));
        // TODO: how to test whether the values actually work?

        // and on third column we should not get an index any more
        QVERIFY(!model.index(i, 2, clientIndex).isValid());
    }
    // row after count should be invalid
    QVERIFY(!model.index(model.rowCount(clientIndex), 0, clientIndex).isValid());

    // creating a second model should be initialized directly with the X11 child
    debug::wayland_console_model model2(*Test::app()->base.space);
    QVERIFY(model2.hasChildren(model2.index(1, 0, QModelIndex())));

    // now close the window again, it should be removed from the model
    QSignalSpy rowsRemovedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(rowsRemovedSpy.isValid());

    window.unmap();

    QVERIFY(rowsRemovedSpy.wait());
    QCOMPARE(rowsRemovedSpy.count(), 1);
    QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), unmanagedTopLevelIndex);
    QCOMPARE(rowsRemovedSpy.first().at(1).value<int>(), 0);
    QCOMPARE(rowsRemovedSpy.first().at(2).value<int>(), 0);

    // the child should be gone again
    QVERIFY(!model.hasChildren(unmanagedTopLevelIndex));
    QVERIFY(!model2.hasChildren(model2.index(1, 0, QModelIndex())));
}

void DebugConsoleTest::testWaylandClient()
{
    debug::wayland_console_model model(*Test::app()->base.space);
    QModelIndex waylandTopLevelIndex = model.index(2, 0, QModelIndex());
    QVERIFY(waylandTopLevelIndex.isValid());

    // we don't have any windows yet
    QCOMPARE(model.rowCount(waylandTopLevelIndex), 0);
    QVERIFY(!model.hasChildren(waylandTopLevelIndex));
    // child index must be invalid
    QVERIFY(!model.index(0, 0, waylandTopLevelIndex).isValid());
    QVERIFY(!model.index(0, 1, waylandTopLevelIndex).isValid());
    QVERIFY(!model.index(0, 2, waylandTopLevelIndex).isValid());
    QVERIFY(!model.index(1, 0, waylandTopLevelIndex).isValid());

    // we need to create a wayland window
    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QVERIFY(rowsInsertedSpy.isValid());

    // create our connection
    Test::setup_wayland_connection();

    // create the Surface and ShellSurface
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface->isValid());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    Test::render(surface, QSize(10, 10), Qt::red);

    // now we have the window, it should be added to our model
    QVERIFY(rowsInsertedSpy.wait());
    QCOMPARE(rowsInsertedSpy.count(), 1);

    QVERIFY(model.hasChildren(waylandTopLevelIndex));
    QCOMPARE(model.rowCount(waylandTopLevelIndex), 1);
    QCOMPARE(rowsInsertedSpy.first().at(0).value<QModelIndex>(), waylandTopLevelIndex);
    QCOMPARE(rowsInsertedSpy.first().at(1).value<int>(), 0);
    QCOMPARE(rowsInsertedSpy.first().at(2).value<int>(), 0);

    QModelIndex clientIndex = model.index(0, 0, waylandTopLevelIndex);
    QVERIFY(clientIndex.isValid());
    QCOMPARE(model.parent(clientIndex), waylandTopLevelIndex);
    QVERIFY(model.hasChildren(clientIndex));
    QVERIFY(model.rowCount(clientIndex) != 0);
    QCOMPARE(model.columnCount(clientIndex), 2);
    // other indexes are still invalid
    QVERIFY(!model.index(0, 1, waylandTopLevelIndex).isValid());
    QVERIFY(!model.index(0, 2, waylandTopLevelIndex).isValid());
    QVERIFY(!model.index(1, 0, waylandTopLevelIndex).isValid());

    // the clientIndex has children and those are properties
    for (int i = 0; i < model.rowCount(clientIndex); i++) {
        const QModelIndex propNameIndex = model.index(i, 0, clientIndex);
        QVERIFY(propNameIndex.isValid());
        QCOMPARE(model.parent(propNameIndex), clientIndex);
        QVERIFY(!model.hasChildren(propNameIndex));
        QVERIFY(!model.index(0, 0, propNameIndex).isValid());
        QVERIFY(model.data(propNameIndex, Qt::DisplayRole).isValid());
        QCOMPARE(model.data(propNameIndex, Qt::DisplayRole).userType(), int(QMetaType::QString));

        // and the value
        const QModelIndex propValueIndex = model.index(i, 1, clientIndex);
        QVERIFY(propValueIndex.isValid());
        QCOMPARE(model.parent(propValueIndex), clientIndex);
        QVERIFY(!model.index(0, 0, propValueIndex).isValid());
        QVERIFY(!model.hasChildren(propValueIndex));
        // TODO: how to test whether the values actually work?

        // and on third column we should not get an index any more
        QVERIFY(!model.index(i, 2, clientIndex).isValid());
    }
    // row after count should be invalid
    QVERIFY(!model.index(model.rowCount(clientIndex), 0, clientIndex).isValid());

    // creating a second model should be initialized directly with the X11 child
    debug::wayland_console_model model2(*Test::app()->base.space);
    QVERIFY(model2.hasChildren(model2.index(2, 0, QModelIndex())));

    // now close the window again, it should be removed from the model
    QSignalSpy rowsRemovedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(rowsRemovedSpy.isValid());

    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    shellSurface.reset();
    Test::flush_wayland_connection();
    qDebug() << rowsRemovedSpy.count();
    QEXPECT_FAIL(
        "wlShell",
        "Deleting a ShellSurface does not result in the server removing the XdgShellClient",
        Continue);
    QVERIFY(rowsRemovedSpy.wait(500));
    surface.reset();

    if (rowsRemovedSpy.isEmpty()) {
        QVERIFY(rowsRemovedSpy.wait());
    }
    QCOMPARE(rowsRemovedSpy.count(), 1);
    QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), waylandTopLevelIndex);
    QCOMPARE(rowsRemovedSpy.first().at(1).value<int>(), 0);
    QCOMPARE(rowsRemovedSpy.first().at(2).value<int>(), 0);

    // the child should be gone again
    QVERIFY(!model.hasChildren(waylandTopLevelIndex));
    QVERIFY(!model2.hasChildren(model2.index(2, 0, QModelIndex())));
}

class HelperWindow : public QRasterWindow
{
    Q_OBJECT
public:
    HelperWindow()
        : QRasterWindow(nullptr)
    {
    }
    ~HelperWindow() override = default;

Q_SIGNALS:
    void entered();
    void left();
    void mouseMoved(const QPoint& global);
    void mousePressed();
    void mouseReleased();
    void wheel();
    void keyPressed();
    void keyReleased();

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)
        QPainter p(this);
        p.fillRect(0, 0, width(), height(), Qt::red);
    }
};

void DebugConsoleTest::testInternalWindow()
{
    debug::wayland_console_model model(*Test::app()->base.space);
    QModelIndex internalTopLevelIndex = model.index(3, 0, QModelIndex());
    QVERIFY(internalTopLevelIndex.isValid());

    // there might already be some internal windows, so we cannot reliable test whether there are
    // children given that we just test whether adding a window works.

    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QVERIFY(rowsInsertedSpy.isValid());

    std::unique_ptr<HelperWindow> w(new HelperWindow);
    w->setGeometry(0, 0, 100, 100);
    w->show();

    QTRY_COMPARE(rowsInsertedSpy.count(), 1);
    QCOMPARE(rowsInsertedSpy.first().first().value<QModelIndex>(), internalTopLevelIndex);

    QModelIndex clientIndex
        = model.index(rowsInsertedSpy.first().last().toInt(), 0, internalTopLevelIndex);
    QVERIFY(clientIndex.isValid());
    QCOMPARE(model.parent(clientIndex), internalTopLevelIndex);
    QVERIFY(model.hasChildren(clientIndex));
    QVERIFY(model.rowCount(clientIndex) != 0);
    QCOMPARE(model.columnCount(clientIndex), 2);
    // other indexes are still invalid
    QVERIFY(
        !model.index(rowsInsertedSpy.first().last().toInt(), 1, internalTopLevelIndex).isValid());
    QVERIFY(
        !model.index(rowsInsertedSpy.first().last().toInt(), 2, internalTopLevelIndex).isValid());
    QVERIFY(!model.index(rowsInsertedSpy.first().last().toInt() + 1, 0, internalTopLevelIndex)
                 .isValid());

    // the wayland shell client top level should not have gained this window
    QVERIFY(!model.hasChildren(model.index(2, 0, QModelIndex())));

    // the clientIndex has children and those are properties
    for (int i = 0; i < model.rowCount(clientIndex); i++) {
        const QModelIndex propNameIndex = model.index(i, 0, clientIndex);
        QVERIFY(propNameIndex.isValid());
        QCOMPARE(model.parent(propNameIndex), clientIndex);
        QVERIFY(!model.hasChildren(propNameIndex));
        QVERIFY(!model.index(0, 0, propNameIndex).isValid());
        QVERIFY(model.data(propNameIndex, Qt::DisplayRole).isValid());
        QCOMPARE(model.data(propNameIndex, Qt::DisplayRole).userType(), int(QMetaType::QString));

        // and the value
        const QModelIndex propValueIndex = model.index(i, 1, clientIndex);
        QVERIFY(propValueIndex.isValid());
        QCOMPARE(model.parent(propValueIndex), clientIndex);
        QVERIFY(!model.index(0, 0, propValueIndex).isValid());
        QVERIFY(!model.hasChildren(propValueIndex));
        // TODO: how to test whether the values actually work?

        // and on third column we should not get an index any more
        QVERIFY(!model.index(i, 2, clientIndex).isValid());
    }
    // row after count should be invalid
    QVERIFY(!model.index(model.rowCount(clientIndex), 0, clientIndex).isValid());

    // now close the window again, it should be removed from the model
    QSignalSpy rowsRemovedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(rowsRemovedSpy.isValid());

    w->hide();
    w.reset();

    QTRY_COMPARE(rowsRemovedSpy.count(), 1);
    QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), internalTopLevelIndex);
}

void DebugConsoleTest::testClosingDebugConsole()
{
    // this test verifies that the DebugConsole gets destroyed when closing the window
    // BUG: 369858

    auto console = new debug::console(*Test::app()->base.space);
    QSignalSpy destroyedSpy(console, &QObject::destroyed);
    QVERIFY(destroyedSpy.isValid());

    QSignalSpy clientAddedSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    console->show();
    QCOMPARE(console->windowHandle()->isVisible(), true);
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto c = dynamic_cast<win::internal_window*>(clientAddedSpy.first().first().value<Toplevel*>());
    QVERIFY(c->isInternal());
    QCOMPARE(c->internalWindow(), console->windowHandle());
    QVERIFY(win::decoration(c));
    QCOMPARE(c->isMinimizable(), false);
    c->closeWindow();
    QVERIFY(destroyedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::DebugConsoleTest)
#include "debug_console_test.moc"
