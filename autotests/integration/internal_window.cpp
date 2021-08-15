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
#include "effects.h"
#include "input/cursor.h"
#include "kwin_wayland_test.h"
#include "platform.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/deco.h"
#include "win/internal_client.h"
#include "win/move.h"
#include "win/net.h"
#include "win/x11/stacking_tree.h"

#include <QPainter>
#include <QRasterWindow>

#include <KWindowSystem>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/surface.h>

#include <linux/input.h>

using namespace Wrapland::Client;

Q_DECLARE_METATYPE(NET::WindowType);

namespace KWin
{

class InternalWindowTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testEnterLeave();
    void testPointerPressRelease();
    void testPointerAxis();
    void testKeyboard_data();
    void testKeyboard();
    void testKeyboardShowWithoutActivating();
    void testKeyboardTriggersLeave();
    void testTouch();
    void testOpacity();
    void testMove();
    void testSkipCloseAnimation_data();
    void testSkipCloseAnimation();
    void testModifierClickUnrestrictedMove();
    void testModifierScroll();
    void testPopup();
    void testScale();
    void testWindowType_data();
    void testWindowType();
    void testChangeWindowType_data();
    void testChangeWindowType();
    void testEffectWindow();
};

class HelperWindow : public QRasterWindow
{
    Q_OBJECT
public:
    HelperWindow();
    ~HelperWindow() override;

    QPoint latestGlobalMousePos() const
    {
        return m_latestGlobalMousePos;
    }
    Qt::MouseButtons pressedButtons() const
    {
        return m_pressedButtons;
    }

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
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    QPoint m_latestGlobalMousePos;
    Qt::MouseButtons m_pressedButtons = Qt::MouseButtons();
};

HelperWindow::HelperWindow()
    : QRasterWindow(nullptr)
{
    setFlags(Qt::FramelessWindowHint);
}

HelperWindow::~HelperWindow() = default;

void HelperWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(0, 0, width(), height(), Qt::red);
}

bool HelperWindow::event(QEvent* event)
{
    if (event->type() == QEvent::Enter) {
        emit entered();
    }
    if (event->type() == QEvent::Leave) {
        emit left();
    }
    return QRasterWindow::event(event);
}

void HelperWindow::mouseMoveEvent(QMouseEvent* event)
{
    m_latestGlobalMousePos = event->globalPos();
    emit mouseMoved(event->globalPos());
}

void HelperWindow::mousePressEvent(QMouseEvent* event)
{
    m_latestGlobalMousePos = event->globalPos();
    m_pressedButtons = event->buttons();
    emit mousePressed();
}

void HelperWindow::mouseReleaseEvent(QMouseEvent* event)
{
    m_latestGlobalMousePos = event->globalPos();
    m_pressedButtons = event->buttons();
    emit mouseReleased();
}

void HelperWindow::wheelEvent(QWheelEvent* event)
{
    Q_UNUSED(event)
    emit wheel();
}

void HelperWindow::keyPressEvent(QKeyEvent* event)
{
    Q_UNUSED(event)
    emit keyPressed();
}

void HelperWindow::keyReleaseEvent(QKeyEvent* event)
{
    Q_UNUSED(event)
    emit keyReleased();
}

void InternalWindowTest::initTestCase()
{
    qRegisterMetaType<KWin::win::InternalClient*>();
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform->setInitialWindowSize(QSize(1280, 1024));
    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    kwinApp()->start();
    QMetaObject::invokeMethod(
        kwinApp()->platform, "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));
    QVERIFY(workspaceCreatedSpy.size() || workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
}

void InternalWindowTest::init()
{
    input::get_cursor()->setPos(QPoint(1280, 512));
    Test::setup_wayland_connection(Test::AdditionalWaylandInterface::Seat);
    QVERIFY(Test::wait_for_wayland_keyboard());
}

void InternalWindowTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void InternalWindowTest::testEnterLeave()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    QVERIFY(!workspace()->findInternal(nullptr));
    QVERIFY(!workspace()->findInternal(&win));
    win.setGeometry(0, 0, 100, 100);
    win.show();

    QTRY_COMPARE(clientAddedSpy.count(), 1);
    QVERIFY(!workspace()->activeClient());
    win::InternalClient* c = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(c);
    QVERIFY(c->isInternal());
    QVERIFY(!win::decoration(c));
    QCOMPARE(workspace()->findInternal(&win), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 100));
    QVERIFY(c->isShown());
    QVERIFY(contains(workspace()->x_stacking_tree->as_list(), c));

    QSignalSpy enterSpy(&win, &HelperWindow::entered);
    QVERIFY(enterSpy.isValid());
    QSignalSpy leaveSpy(&win, &HelperWindow::left);
    QVERIFY(leaveSpy.isValid());
    QSignalSpy moveSpy(&win, &HelperWindow::mouseMoved);
    QVERIFY(moveSpy.isValid());

    quint32 timestamp = 1;
    Test::pointer_motion_absolute(QPoint(50, 50), timestamp++);
    QTRY_COMPARE(moveSpy.count(), 1);

    Test::pointer_motion_absolute(QPoint(60, 50), timestamp++);
    QTRY_COMPARE(moveSpy.count(), 2);
    QCOMPARE(moveSpy[1].first().toPoint(), QPoint(60, 50));

    Test::pointer_motion_absolute(QPoint(101, 50), timestamp++);
    QTRY_COMPARE(leaveSpy.count(), 1);

    // set a mask on the window
    win.setMask(QRegion(10, 20, 30, 40));
    // outside the mask we should not get an enter
    Test::pointer_motion_absolute(QPoint(5, 5), timestamp++);
    QVERIFY(!enterSpy.wait(100));
    QCOMPARE(enterSpy.count(), 1);
    // inside the mask we should still get an enter
    Test::pointer_motion_absolute(QPoint(25, 27), timestamp++);
    QTRY_COMPARE(enterSpy.count(), 2);
}

void InternalWindowTest::testPointerPressRelease()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QSignalSpy pressSpy(&win, &HelperWindow::mousePressed);
    QVERIFY(pressSpy.isValid());
    QSignalSpy releaseSpy(&win, &HelperWindow::mouseReleased);
    QVERIFY(releaseSpy.isValid());

    QTRY_COMPARE(clientAddedSpy.count(), 1);

    quint32 timestamp = 1;
    Test::pointer_motion_absolute(QPoint(50, 50), timestamp++);

    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QTRY_COMPARE(pressSpy.count(), 1);
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QTRY_COMPARE(releaseSpy.count(), 1);
}

void InternalWindowTest::testPointerAxis()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QSignalSpy wheelSpy(&win, &HelperWindow::wheel);
    QVERIFY(wheelSpy.isValid());
    QTRY_COMPARE(clientAddedSpy.count(), 1);

    quint32 timestamp = 1;
    Test::pointer_motion_absolute(QPoint(50, 50), timestamp++);

    Test::pointer_axis_vertical(5.0, timestamp++, 0);
    QTRY_COMPARE(wheelSpy.count(), 1);
    Test::pointer_axis_horizontal(5.0, timestamp++, 0);
    QTRY_COMPARE(wheelSpy.count(), 2);
}

void InternalWindowTest::testKeyboard_data()
{
    QTest::addColumn<QPoint>("cursorPos");

    QTest::newRow("on Window") << QPoint(50, 50);
    QTest::newRow("outside Window") << QPoint(250, 250);
}

void InternalWindowTest::testKeyboard()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QSignalSpy pressSpy(&win, &HelperWindow::keyPressed);
    QVERIFY(pressSpy.isValid());
    QSignalSpy releaseSpy(&win, &HelperWindow::keyReleased);
    QVERIFY(releaseSpy.isValid());
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QVERIFY(internalClient->isInternal());
    QVERIFY(internalClient->readyForPainting());

    quint32 timestamp = 1;
    QFETCH(QPoint, cursorPos);
    Test::pointer_motion_absolute(cursorPos, timestamp++);

    Test::keyboard_key_pressed(KEY_A, timestamp++);
    QTRY_COMPARE(pressSpy.count(), 1);
    QCOMPARE(releaseSpy.count(), 0);
    Test::keyboard_key_released(KEY_A, timestamp++);
    QTRY_COMPARE(releaseSpy.count(), 1);
    QCOMPARE(pressSpy.count(), 1);
}

void InternalWindowTest::testKeyboardShowWithoutActivating()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setProperty("_q_showWithoutActivating", true);
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QSignalSpy pressSpy(&win, &HelperWindow::keyPressed);
    QVERIFY(pressSpy.isValid());
    QSignalSpy releaseSpy(&win, &HelperWindow::keyReleased);
    QVERIFY(releaseSpy.isValid());
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QVERIFY(internalClient->isInternal());
    QVERIFY(internalClient->readyForPainting());

    quint32 timestamp = 1;
    const QPoint cursorPos = QPoint(50, 50);
    Test::pointer_motion_absolute(cursorPos, timestamp++);

    Test::keyboard_key_pressed(KEY_A, timestamp++);
    QCOMPARE(pressSpy.count(), 0);
    QVERIFY(!pressSpy.wait(100));
    QCOMPARE(releaseSpy.count(), 0);
    Test::keyboard_key_released(KEY_A, timestamp++);
    QCOMPARE(releaseSpy.count(), 0);
    QVERIFY(!releaseSpy.wait(100));
    QCOMPARE(pressSpy.count(), 0);
}

void InternalWindowTest::testKeyboardTriggersLeave()
{
    // this test verifies that a leave event is sent to a client when an internal window
    // gets a key event
    std::unique_ptr<Keyboard> keyboard(Test::get_client().interfaces.seat->createKeyboard());
    QVERIFY(keyboard);
    QVERIFY(keyboard->isValid());
    QSignalSpy enteredSpy(keyboard.get(), &Keyboard::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(keyboard.get(), &Keyboard::left);
    QVERIFY(leftSpy.isValid());
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));

    // now let's render
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());
    QVERIFY(!c->isInternal());

    if (enteredSpy.isEmpty()) {
        QVERIFY(enteredSpy.wait());
    }
    QCOMPARE(enteredSpy.count(), 1);

    // create internal window
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QSignalSpy pressSpy(&win, &HelperWindow::keyPressed);
    QVERIFY(pressSpy.isValid());
    QSignalSpy releaseSpy(&win, &HelperWindow::keyReleased);
    QVERIFY(releaseSpy.isValid());
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QVERIFY(internalClient->isInternal());
    QVERIFY(internalClient->readyForPainting());

    QVERIFY(leftSpy.isEmpty());
    QVERIFY(!leftSpy.wait(100));

    // now let's trigger a key, which should result in a leave
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_A, timestamp++);
    QVERIFY(leftSpy.wait());
    QCOMPARE(pressSpy.count(), 1);

    Test::keyboard_key_released(KEY_A, timestamp++);
    QTRY_COMPARE(releaseSpy.count(), 1);

    // after hiding the internal window, next key press should trigger an enter
    win.hide();
    Test::keyboard_key_pressed(KEY_A, timestamp++);
    QVERIFY(enteredSpy.wait());
    Test::keyboard_key_released(KEY_A, timestamp++);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
}

void InternalWindowTest::testTouch()
{
    // touch events for internal windows are emulated through mouse events
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);

    QSignalSpy pressSpy(&win, &HelperWindow::mousePressed);
    QVERIFY(pressSpy.isValid());
    QSignalSpy releaseSpy(&win, &HelperWindow::mouseReleased);
    QVERIFY(releaseSpy.isValid());
    QSignalSpy moveSpy(&win, &HelperWindow::mouseMoved);
    QVERIFY(moveSpy.isValid());

    quint32 timestamp = 1;
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons());
    Test::touch_down(0, QPointF(50, 50), timestamp++);
    QCOMPARE(pressSpy.count(), 1);
    QCOMPARE(win.latestGlobalMousePos(), QPoint(50, 50));
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

    // further touch down should not trigger
    Test::touch_down(1, QPointF(75, 75), timestamp++);
    QCOMPARE(pressSpy.count(), 1);
    Test::touch_up(1, timestamp++);
    QCOMPARE(releaseSpy.count(), 0);
    QCOMPARE(win.latestGlobalMousePos(), QPoint(50, 50));
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

    // another press
    Test::touch_down(1, QPointF(10, 10), timestamp++);
    QCOMPARE(pressSpy.count(), 1);
    QCOMPARE(win.latestGlobalMousePos(), QPoint(50, 50));
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

    // simulate the move
    QCOMPARE(moveSpy.count(), 0);
    Test::touch_motion(0, QPointF(80, 90), timestamp++);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

    // move on other ID should not do anything
    Test::touch_motion(1, QPointF(20, 30), timestamp++);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

    // now up our main point
    Test::touch_up(0, timestamp++);
    QCOMPARE(releaseSpy.count(), 1);
    QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons());

    // and up the additional point
    Test::touch_up(1, timestamp++);
    QCOMPARE(releaseSpy.count(), 1);
    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
    QCOMPARE(win.pressedButtons(), Qt::MouseButtons());
}

void InternalWindowTest::testOpacity()
{
    // this test verifies that opacity is properly synced from QWindow to InternalClient
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setOpacity(0.5);
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QVERIFY(internalClient->isInternal());
    QCOMPARE(internalClient->opacity(), 0.5);

    QSignalSpy opacityChangedSpy(internalClient, &win::InternalClient::opacityChanged);
    QVERIFY(opacityChangedSpy.isValid());
    win.setOpacity(0.75);
    QCOMPARE(opacityChangedSpy.count(), 1);
    QCOMPARE(internalClient->opacity(), 0.75);
}

void InternalWindowTest::testMove()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setOpacity(0.5);
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QCOMPARE(internalClient->frameGeometry(), QRect(0, 0, 100, 100));

    // normal move should be synced
    win::move(internalClient, QPoint(5, 10));
    QCOMPARE(internalClient->frameGeometry(), QRect(5, 10, 100, 100));
    QTRY_COMPARE(win.geometry(), QRect(5, 10, 100, 100));
    // another move should also be synced
    win::move(internalClient, QPoint(10, 20));
    QCOMPARE(internalClient->frameGeometry(), QRect(10, 20, 100, 100));
    QTRY_COMPARE(win.geometry(), QRect(10, 20, 100, 100));

    // now move with a Geometry update blocker
    {
        win::geometry_updates_blocker blocker(internalClient);
        win::move(internalClient, QPoint(5, 10));
        // not synced!
        QCOMPARE(win.geometry(), QRect(10, 20, 100, 100));
    }
    // after destroying the blocker it should be synced
    QTRY_COMPARE(win.geometry(), QRect(5, 10, 100, 100));
}

void InternalWindowTest::testSkipCloseAnimation_data()
{
    QTest::addColumn<bool>("initial");

    QTest::newRow("set") << true;
    QTest::newRow("not set") << false;
}

void InternalWindowTest::testSkipCloseAnimation()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setOpacity(0.5);
    win.setGeometry(0, 0, 100, 100);
    QFETCH(bool, initial);
    win.setProperty("KWIN_SKIP_CLOSE_ANIMATION", initial);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QCOMPARE(internalClient->skipsCloseAnimation(), initial);
    QSignalSpy skipCloseChangedSpy(internalClient, &Toplevel::skipCloseAnimationChanged);
    QVERIFY(skipCloseChangedSpy.isValid());
    win.setProperty("KWIN_SKIP_CLOSE_ANIMATION", !initial);
    QCOMPARE(skipCloseChangedSpy.count(), 1);
    QCOMPARE(internalClient->skipsCloseAnimation(), !initial);
    win.setProperty("KWIN_SKIP_CLOSE_ANIMATION", initial);
    QCOMPARE(skipCloseChangedSpy.count(), 2);
    QCOMPARE(internalClient->skipsCloseAnimation(), initial);
}

void InternalWindowTest::testModifierClickUnrestrictedMove()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.setFlags(win.flags() & ~Qt::FramelessWindowHint);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QVERIFY(win::decoration(internalClient));

    KConfigGroup group = kwinApp()->config()->group("MouseBindings");
    group.writeEntry("CommandAllKey", "Meta");
    group.writeEntry("CommandAll1", "Move");
    group.writeEntry("CommandAll2", "Move");
    group.writeEntry("CommandAll3", "Move");
    group.sync();
    workspace()->slotReconfigure();
    QCOMPARE(options->commandAllModifier(), Qt::MetaModifier);
    QCOMPARE(options->commandAll1(), Options::MouseUnrestrictedMove);
    QCOMPARE(options->commandAll2(), Options::MouseUnrestrictedMove);
    QCOMPARE(options->commandAll3(), Options::MouseUnrestrictedMove);

    // move cursor on window
    input::get_cursor()->setPos(internalClient->frameGeometry().center());

    // simulate modifier+click
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    QVERIFY(!win::is_move(internalClient));
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QVERIFY(win::is_move(internalClient));
    // release modifier should not change it
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);
    QVERIFY(win::is_move(internalClient));
    // but releasing the key should end move/resize
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QVERIFY(!win::is_move(internalClient));
}

void InternalWindowTest::testModifierScroll()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.setFlags(win.flags() & ~Qt::FramelessWindowHint);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QVERIFY(win::decoration(internalClient));

    KConfigGroup group = kwinApp()->config()->group("MouseBindings");
    group.writeEntry("CommandAllKey", "Meta");
    group.writeEntry("CommandAllWheel", "change opacity");
    group.sync();
    workspace()->slotReconfigure();

    // move cursor on window
    input::get_cursor()->setPos(internalClient->frameGeometry().center());

    // set the opacity to 0.5
    internalClient->setOpacity(0.5);
    QCOMPARE(internalClient->opacity(), 0.5);
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    Test::pointer_axis_vertical(-5, timestamp++, 0);
    QCOMPARE(internalClient->opacity(), 0.6);
    Test::pointer_axis_vertical(5, timestamp++, 0);
    QCOMPARE(internalClient->opacity(), 0.5);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);
}

void InternalWindowTest::testPopup()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.setFlags(win.flags() | Qt::Popup);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QCOMPARE(win::is_popup(internalClient), true);
}

void InternalWindowTest::testScale()
{
    QMetaObject::invokeMethod(
        kwinApp()->platform,
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, 2),
        Q_ARG(QVector<QRect>,
              QVector<QRect>({QRect(0, 0, 1280, 1024), QRect(1280 / 2, 0, 1280, 1024)})),
        Q_ARG(QVector<int>, QVector<int>({2, 2})));

    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.setFlags(win.flags() | Qt::Popup);
    win.show();
    QCOMPARE(win.devicePixelRatio(), 2.0);
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QCOMPARE(internalClient->bufferScale(), 2);
}

void InternalWindowTest::testWindowType_data()
{
    QTest::addColumn<NET::WindowType>("windowType");

    QTest::newRow("normal") << NET::Normal;
    QTest::newRow("desktop") << NET::Desktop;
    QTest::newRow("Dock") << NET::Dock;
    QTest::newRow("Toolbar") << NET::Toolbar;
    QTest::newRow("Menu") << NET::Menu;
    QTest::newRow("Dialog") << NET::Dialog;
    QTest::newRow("Utility") << NET::Utility;
    QTest::newRow("Splash") << NET::Splash;
    QTest::newRow("DropdownMenu") << NET::DropdownMenu;
    QTest::newRow("PopupMenu") << NET::PopupMenu;
    QTest::newRow("Tooltip") << NET::Tooltip;
    QTest::newRow("Notification") << NET::Notification;
    QTest::newRow("ComboBox") << NET::ComboBox;
    QTest::newRow("OnScreenDisplay") << NET::OnScreenDisplay;
    QTest::newRow("CriticalNotification") << NET::CriticalNotification;
}

void InternalWindowTest::testWindowType()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    QFETCH(NET::WindowType, windowType);
    KWindowSystem::setType(win.winId(), windowType);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QCOMPARE(internalClient->windowType(), windowType);
}

void InternalWindowTest::testChangeWindowType_data()
{
    QTest::addColumn<NET::WindowType>("windowType");

    QTest::newRow("desktop") << NET::Desktop;
    QTest::newRow("Dock") << NET::Dock;
    QTest::newRow("Toolbar") << NET::Toolbar;
    QTest::newRow("Menu") << NET::Menu;
    QTest::newRow("Dialog") << NET::Dialog;
    QTest::newRow("Utility") << NET::Utility;
    QTest::newRow("Splash") << NET::Splash;
    QTest::newRow("DropdownMenu") << NET::DropdownMenu;
    QTest::newRow("PopupMenu") << NET::PopupMenu;
    QTest::newRow("Tooltip") << NET::Tooltip;
    QTest::newRow("Notification") << NET::Notification;
    QTest::newRow("ComboBox") << NET::ComboBox;
    QTest::newRow("OnScreenDisplay") << NET::OnScreenDisplay;
    QTest::newRow("CriticalNotification") << NET::CriticalNotification;
}

void InternalWindowTest::testChangeWindowType()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QCOMPARE(internalClient->windowType(), NET::Normal);

    QFETCH(NET::WindowType, windowType);
    KWindowSystem::setType(win.winId(), windowType);
    QTRY_COMPARE(internalClient->windowType(), windowType);

    KWindowSystem::setType(win.winId(), NET::Normal);
    QTRY_COMPARE(internalClient->windowType(), NET::Normal);
}

void InternalWindowTest::testEffectWindow()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    HelperWindow win;
    win.setGeometry(0, 0, 100, 100);
    win.show();
    QTRY_COMPARE(clientAddedSpy.count(), 1);
    auto internalClient = clientAddedSpy.first().first().value<win::InternalClient*>();
    QVERIFY(internalClient);
    QVERIFY(internalClient->effectWindow());
    QCOMPARE(internalClient->effectWindow()->internalWindow(), &win);

    QCOMPARE(effects->findWindow(&win), internalClient->effectWindow());
    QCOMPARE(effects->findWindow(&win)->internalWindow(), &win);
}

}

WAYLANDTEST_MAIN(KWin::InternalWindowTest)
#include "internal_window.moc"
