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
#include "input/keyboard_redirect.h"
#include "kwin_wayland_test.h"
#include "platform.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KConfigGroup>

#include <QDBusConnection>

#include <linux/input.h>

using namespace Wrapland::Client;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_modifier_only_shortcut-0");
static const QString s_serviceName = QStringLiteral("org.kde.KWin.Test.ModifierOnlyShortcut");
static const QString s_path = QStringLiteral("/Test");

namespace KWin
{

class ModifierOnlyShortcutTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testTrigger_data();
    void testTrigger();
    void testCapsLock();
    void testGlobalShortcutsDisabled_data();
    void testGlobalShortcutsDisabled();
};

class Target : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Test.ModifierOnlyShortcut")

public:
    Target();
    ~Target() override;

public Q_SLOTS:
    Q_SCRIPTABLE void shortcut();

Q_SIGNALS:
    void shortcutTriggered();
};

Target::Target()
    : QObject()
{
    QDBusConnection::sessionBus().registerService(s_serviceName);
    QDBusConnection::sessionBus().registerObject(
        s_path, s_serviceName, this, QDBusConnection::ExportScriptableSlots);
}

Target::~Target()
{
    QDBusConnection::sessionBus().unregisterObject(s_path);
    QDBusConnection::sessionBus().unregisterService(s_serviceName);
}

void Target::shortcut()
{
    emit shortcutTriggered();
}

void ModifierOnlyShortcutTest::initTestCase()
{
    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    waylandServer()->initWorkspace();
}

void ModifierOnlyShortcutTest::init()
{
    Test::setup_wayland_connection();
    screens()->setCurrent(0);
    input::cursor::setPos(QPoint(640, 512));
}

void ModifierOnlyShortcutTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void ModifierOnlyShortcutTest::testTrigger_data()
{
    QTest::addColumn<QStringList>("metaConfig");
    QTest::addColumn<QStringList>("altConfig");
    QTest::addColumn<QStringList>("controlConfig");
    QTest::addColumn<QStringList>("shiftConfig");
    QTest::addColumn<int>("modifier");
    QTest::addColumn<QList<int>>("nonTriggeringMods");

    const QStringList trigger
        = QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")};
    const QStringList e = QStringList();

    QTest::newRow("leftMeta") << trigger << e << e << e << KEY_LEFTMETA
                              << QList<int>{KEY_LEFTALT,
                                            KEY_RIGHTALT,
                                            KEY_LEFTCTRL,
                                            KEY_RIGHTCTRL,
                                            KEY_LEFTSHIFT,
                                            KEY_RIGHTSHIFT};
    QTest::newRow("rightMeta") << trigger << e << e << e << KEY_RIGHTMETA
                               << QList<int>{KEY_LEFTALT,
                                             KEY_RIGHTALT,
                                             KEY_LEFTCTRL,
                                             KEY_RIGHTCTRL,
                                             KEY_LEFTSHIFT,
                                             KEY_RIGHTSHIFT};
    QTest::newRow("leftAlt") << e << trigger << e << e << KEY_LEFTALT
                             << QList<int>{KEY_LEFTMETA,
                                           KEY_RIGHTMETA,
                                           KEY_LEFTCTRL,
                                           KEY_RIGHTCTRL,
                                           KEY_LEFTSHIFT,
                                           KEY_RIGHTSHIFT};
    QTest::newRow("rightAlt") << e << trigger << e << e << KEY_RIGHTALT
                              << QList<int>{KEY_LEFTMETA,
                                            KEY_RIGHTMETA,
                                            KEY_LEFTCTRL,
                                            KEY_RIGHTCTRL,
                                            KEY_LEFTSHIFT,
                                            KEY_RIGHTSHIFT};
    QTest::newRow("leftControl") << e << e << trigger << e << KEY_LEFTCTRL
                                 << QList<int>{KEY_LEFTALT,
                                               KEY_RIGHTALT,
                                               KEY_LEFTMETA,
                                               KEY_RIGHTMETA,
                                               KEY_LEFTSHIFT,
                                               KEY_RIGHTSHIFT};
    QTest::newRow("rightControl") << e << e << trigger << e << KEY_RIGHTCTRL
                                  << QList<int>{KEY_LEFTALT,
                                                KEY_RIGHTALT,
                                                KEY_LEFTMETA,
                                                KEY_RIGHTMETA,
                                                KEY_LEFTSHIFT,
                                                KEY_RIGHTSHIFT};
    QTest::newRow("leftShift")
        << e << e << e << trigger << KEY_LEFTSHIFT
        << QList<int>{
               KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTMETA, KEY_RIGHTMETA};
    QTest::newRow("rightShift")
        << e << e << e << trigger << KEY_RIGHTSHIFT
        << QList<int>{
               KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTMETA, KEY_RIGHTMETA};
}

void ModifierOnlyShortcutTest::testTrigger()
{
    // this test verifies that modifier only shortcut triggers correctly
    Target target;
    QSignalSpy triggeredSpy(&target, &Target::shortcutTriggered);
    QVERIFY(triggeredSpy.isValid());

    KConfigGroup group = kwinApp()->config()->group("ModifierOnlyShortcuts");
    QFETCH(QStringList, metaConfig);
    QFETCH(QStringList, altConfig);
    QFETCH(QStringList, shiftConfig);
    QFETCH(QStringList, controlConfig);
    group.writeEntry("Meta", metaConfig);
    group.writeEntry("Alt", altConfig);
    group.writeEntry("Shift", shiftConfig);
    group.writeEntry("Control", controlConfig);
    group.sync();
    workspace()->slotReconfigure();

    // configured shortcut should trigger
    quint32 timestamp = 1;
    QFETCH(int, modifier);

    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QVERIFY(triggeredSpy.count() || triggeredSpy.wait());
    QCOMPARE(triggeredSpy.count(), 1);

    // the other shortcuts should not trigger
    QFETCH(QList<int>, nonTriggeringMods);
    for (auto it = nonTriggeringMods.constBegin(), end = nonTriggeringMods.constEnd(); it != end;
         it++) {
        Test::keyboard_key_pressed(*it, timestamp++);
        Test::keyboard_key_released(*it, timestamp++);
        QCOMPARE(triggeredSpy.count(), 1);
    }

    // try configured again
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QVERIFY(triggeredSpy.count() == 2 || triggeredSpy.wait());
    QCOMPARE(triggeredSpy.count(), 2);

    // click another key while modifier is held
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_pressed(KEY_A, timestamp++);
    Test::keyboard_key_released(KEY_A, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QCOMPARE(triggeredSpy.count(), 2);

    // release other key after modifier release
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_pressed(KEY_A, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    Test::keyboard_key_released(KEY_A, timestamp++);
    QCOMPARE(triggeredSpy.count(), 2);

    // press key before pressing modifier
    Test::keyboard_key_pressed(KEY_A, timestamp++);
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    Test::keyboard_key_released(KEY_A, timestamp++);
    QCOMPARE(triggeredSpy.count(), 2);

    // mouse button pressed before clicking modifier
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->qtButtonStates(), Qt::LeftButton);

    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->qtButtonStates(), Qt::NoButton);
    QCOMPARE(triggeredSpy.count(), 2);

    // mouse button press before mod press, release before mod release
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->qtButtonStates(), Qt::LeftButton);

    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->qtButtonStates(), Qt::NoButton);
    QCOMPARE(triggeredSpy.count(), 2);

    // mouse button click while mod is pressed
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->qtButtonStates(), Qt::LeftButton);

    Test::pointer_button_released(BTN_LEFT, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->qtButtonStates(), Qt::NoButton);
    QCOMPARE(triggeredSpy.count(), 2);

    // scroll while mod is pressed
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::pointer_axis_vertical(5.0, timestamp++, 0);
    Test::keyboard_key_released(modifier, timestamp++);
    QCOMPARE(triggeredSpy.count(), 2);

    // same for horizontal
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::pointer_axis_horizontal(5.0, timestamp++, 0);
    Test::keyboard_key_released(modifier, timestamp++);
    QCOMPARE(triggeredSpy.count(), 2);

    // now try to lock the screen while modifier key is pressed
    Test::keyboard_key_pressed(modifier, timestamp++);

    Test::lock_screen();

    Test::keyboard_key_released(modifier, timestamp++);
    QCOMPARE(triggeredSpy.count(), 2);

    // now trigger while screen is locked, should also not work
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QCOMPARE(triggeredSpy.count(), 2);

    Test::unlock_screen();
}

void ModifierOnlyShortcutTest::testCapsLock()
{
    // this test verifies that Capslock does not trigger the shift shortcut
    // but other shortcuts still trigger even when Capslock is on
    Target target;
    QSignalSpy triggeredSpy(&target, &Target::shortcutTriggered);
    QVERIFY(triggeredSpy.isValid());

    KConfigGroup group = kwinApp()->config()->group("ModifierOnlyShortcuts");
    group.writeEntry("Meta", QStringList());
    group.writeEntry("Alt", QStringList());
    group.writeEntry("Shift",
                     QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")});
    group.writeEntry("Control", QStringList());
    group.sync();
    workspace()->slotReconfigure();

    // first test that the normal shortcut triggers
    quint32 timestamp = 1;
    const int modifier = KEY_LEFTSHIFT;
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 1);

    // now capslock
    Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
    Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->keyboardModifiers(), Qt::ShiftModifier);
    QTRY_COMPARE(triggeredSpy.count(), 1);

    // currently caps lock is on
    // shift still triggers
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->keyboardModifiers(), Qt::ShiftModifier);
    QTRY_COMPARE(triggeredSpy.count(), 2);

    // meta should also trigger
    group.writeEntry("Meta",
                     QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")});
    group.writeEntry("Alt", QStringList());
    group.writeEntry("Shift", QStringList{});
    group.writeEntry("Control", QStringList());
    group.sync();
    workspace()->slotReconfigure();
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->keyboardModifiers(),
                 Qt::ShiftModifier | Qt::MetaModifier);
    QTRY_COMPARE(
        kwinApp()->input_redirect->keyboard()->xkb()->modifiersRelevantForGlobalShortcuts(),
        Qt::MetaModifier);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 3);

    // set back to shift to ensure we don't trigger with capslock
    group.writeEntry("Meta", QStringList());
    group.writeEntry("Alt", QStringList());
    group.writeEntry("Shift",
                     QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")});
    group.writeEntry("Control", QStringList());
    group.sync();
    workspace()->slotReconfigure();

    // release caps lock
    Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
    Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
    QTRY_COMPARE(kwinApp()->input_redirect->keyboardModifiers(), Qt::NoModifier);
    QTRY_COMPARE(triggeredSpy.count(), 3);
}

void ModifierOnlyShortcutTest::testGlobalShortcutsDisabled_data()
{
    QTest::addColumn<QStringList>("metaConfig");
    QTest::addColumn<QStringList>("altConfig");
    QTest::addColumn<QStringList>("controlConfig");
    QTest::addColumn<QStringList>("shiftConfig");
    QTest::addColumn<int>("modifier");

    const QStringList trigger
        = QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")};
    const QStringList e = QStringList();

    QTest::newRow("leftMeta") << trigger << e << e << e << KEY_LEFTMETA;
    QTest::newRow("rightMeta") << trigger << e << e << e << KEY_RIGHTMETA;
    QTest::newRow("leftAlt") << e << trigger << e << e << KEY_LEFTALT;
    QTest::newRow("rightAlt") << e << trigger << e << e << KEY_RIGHTALT;
    QTest::newRow("leftControl") << e << e << trigger << e << KEY_LEFTCTRL;
    QTest::newRow("rightControl") << e << e << trigger << e << KEY_RIGHTCTRL;
    QTest::newRow("leftShift") << e << e << e << trigger << KEY_LEFTSHIFT;
    QTest::newRow("rightShift") << e << e << e << trigger << KEY_RIGHTSHIFT;
}

void ModifierOnlyShortcutTest::testGlobalShortcutsDisabled()
{
    // this test verifies that when global shortcuts are disabled inside KWin (e.g. through a window
    // rule) the modifier only shortcuts do not trigger. see BUG: 370146
    Target target;
    QSignalSpy triggeredSpy(&target, &Target::shortcutTriggered);
    QVERIFY(triggeredSpy.isValid());

    KConfigGroup group = kwinApp()->config()->group("ModifierOnlyShortcuts");
    QFETCH(QStringList, metaConfig);
    QFETCH(QStringList, altConfig);
    QFETCH(QStringList, shiftConfig);
    QFETCH(QStringList, controlConfig);
    group.writeEntry("Meta", metaConfig);
    group.writeEntry("Alt", altConfig);
    group.writeEntry("Shift", shiftConfig);
    group.writeEntry("Control", controlConfig);
    group.sync();
    workspace()->slotReconfigure();

    // trigger once to verify the shortcut works
    quint32 timestamp = 1;
    QFETCH(int, modifier);
    QVERIFY(!workspace()->globalShortcutsDisabled());
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 1);
    triggeredSpy.clear();

    // now disable global shortcuts
    workspace()->disableGlobalShortcutsForClient(true);
    QVERIFY(workspace()->globalShortcutsDisabled());
    // Should not get triggered
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 0);
    triggeredSpy.clear();

    // enable again
    workspace()->disableGlobalShortcutsForClient(false);
    QVERIFY(!workspace()->globalShortcutsDisabled());
    // should get triggered again
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 1);
}

}

WAYLANDTEST_MAIN(KWin::ModifierOnlyShortcutTest)
#include "modifier_only_shortcut_test.moc"
