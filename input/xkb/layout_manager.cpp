/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "layout_manager.h"

#include "helpers.h"
#include "layout_policies.h"

#include "../../platform.h"
#include "input/dbus/keyboard_layout.h"
#include "input/event.h"
#include "main.h"

#include <KGlobalAccel>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <cassert>

namespace KWin::input::xkb
{

layout_manager::layout_manager(xkb::manager& xkb, KSharedConfigPtr const& config)
    : xkb{xkb}
    , m_configGroup(config->group("Layout"))
{
}

void layout_manager::init()
{
    QAction* switchKeyboardAction = new QAction(this);
    switchKeyboardAction->setObjectName(QStringLiteral("Switch to Next Keyboard Layout"));
    switchKeyboardAction->setProperty("componentName",
                                      QStringLiteral("KDE Keyboard Layout Switcher"));
    const QKeySequence sequence = QKeySequence(Qt::ALT + Qt::CTRL + Qt::Key_K);
    KGlobalAccel::self()->setDefaultShortcut(switchKeyboardAction, QList<QKeySequence>({sequence}));
    KGlobalAccel::self()->setShortcut(switchKeyboardAction, QList<QKeySequence>({sequence}));
    kwinApp()->platform->setupActionForGlobalAccel(switchKeyboardAction);
    QObject::connect(
        switchKeyboardAction, &QAction::triggered, this, &layout_manager::switchToNextLayout);

    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/Layouts"),
                                          QStringLiteral("org.kde.keyboard"),
                                          QStringLiteral("reloadConfig"),
                                          this,
                                          SLOT(reconfigure()));

    reconfigure();
}

void layout_manager::initDBusInterface()
{
    auto xkb = xkb::get_primary_xkb_keyboard();

    if (xkb->layouts_count() <= 1) {
        if (m_dbusInterface) {
            m_dbusInterface->deleteLater();
            m_dbusInterface = nullptr;
        }
        return;
    }

    if (m_dbusInterface) {
        return;
    }

    m_dbusInterface = new dbus::keyboard_layout(m_configGroup, this);

    QObject::connect(this,
                     &layout_manager::layoutChanged,
                     m_dbusInterface,
                     &dbus::keyboard_layout::layoutChanged);
    // TODO: the signal might be emitted even if the list didn't change
    QObject::connect(this,
                     &layout_manager::layoutsReconfigured,
                     m_dbusInterface,
                     &dbus::keyboard_layout::layoutListChanged);
}

void layout_manager::switchToNextLayout()
{
    auto xkb = xkb::get_primary_xkb_keyboard();
    auto const previousLayout = xkb->layout;
    xkb->switch_to_next_layout();
    check_layout_change(xkb, previousLayout);
}

void layout_manager::switchToPreviousLayout()
{
    auto xkb = xkb::get_primary_xkb_keyboard();
    auto const previousLayout = xkb->layout;
    xkb->switch_to_previous_layout();
    check_layout_change(xkb, previousLayout);
}

void layout_manager::switchToLayout(xkb_layout_index_t index)
{
    auto xkb = xkb::get_primary_xkb_keyboard();
    auto const previousLayout = xkb->layout;
    xkb->switch_to_layout(index);
    check_layout_change(xkb, previousLayout);
}

void layout_manager::reconfigure()
{
    if (m_configGroup.isValid()) {
        m_configGroup.config()->reparseConfiguration();
        const QString policyKey = m_configGroup.readEntry("SwitchMode", QStringLiteral("Global"));
        xkb.reconfigure();
        if (!m_policy || m_policy->name() != policyKey) {
            delete m_policy;
            m_policy = xkb::layout_policy::create(this, m_configGroup, policyKey);
        }
    } else {
        xkb.reconfigure();
    }

    auto xkb = xkb::get_primary_xkb_keyboard();

    load_shortcuts(xkb);

    initDBusInterface();
    Q_EMIT layoutsReconfigured();
}

void layout_manager::load_shortcuts(xkb::keyboard* xkb)
{
    qDeleteAll(m_layoutShortcuts);
    m_layoutShortcuts.clear();

    const QString componentName = QStringLiteral("KDE Keyboard Layout Switcher");
    auto const count = xkb->layouts_count();

    for (uint i = 0; i < count; ++i) {
        // layout name is translated in the action name in keyboard kcm!
        const QString action = QStringLiteral("Switch keyboard layout to %1")
                                   .arg(translated_keyboard_layout(xkb->layout_name_from_index(i)));
        const auto shortcuts = KGlobalAccel::self()->globalShortcut(componentName, action);
        if (shortcuts.isEmpty()) {
            continue;
        }
        QAction* a = new QAction(this);
        a->setObjectName(action);
        a->setProperty("componentName", componentName);
        QObject::connect(
            a, &QAction::triggered, this, std::bind(&layout_manager::switchToLayout, this, i));
        KGlobalAccel::self()->setShortcut(a, shortcuts, KGlobalAccel::Autoloading);
        m_layoutShortcuts << a;
    }
}

void layout_manager::check_layout_change(xkb::keyboard* xkb, uint32_t old_layout)
{
    if (old_layout == xkb->layout) {
        // No change.
        return;
    }
    if (xkb != xkb::get_primary_xkb_keyboard()) {
        // We currently only inform about changes on the primary device.
        return;
    }
    send_layout_to_osd(xkb);
    Q_EMIT layoutChanged(xkb->layout);
}

void layout_manager::send_layout_to_osd(xkb::keyboard* xkb)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                      QStringLiteral("/org/kde/osdService"),
                                                      QStringLiteral("org.kde.osdService"),
                                                      QStringLiteral("kbdLayoutChanged"));
    msg << translated_keyboard_layout(xkb->layout_name());
    QDBusConnection::sessionBus().asyncCall(msg);
}

}
