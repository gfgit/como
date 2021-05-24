/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
// own
#include "globalshortcuts.h"
// kwin
#include <config-kwin.h>
#include "kwinglobals.h"
#include "main.h"
#include "gestures.h"
#include "utils.h"
// KDE
#include <KGlobalAccel/private/kglobalacceld.h>
#include <KGlobalAccel/private/kglobalaccel_interface.h>
// Qt
#include <QAction>
#include <variant>

namespace KWin
{

GlobalShortcut::GlobalShortcut(Shortcut&& sc, QAction* action) : m_shortcut(sc), m_action(action)
{
    static const QMap<SwipeDirection,SwipeGesture::Direction> dirs = {
        {SwipeDirection::Up, SwipeGesture::Direction::Up},
        {SwipeDirection::Down, SwipeGesture::Direction::Down},
        {SwipeDirection::Left, SwipeGesture::Direction::Left},
        {SwipeDirection::Right, SwipeGesture::Direction::Right},
    };
    if (auto swipeGesture = std::get_if<FourFingerSwipeShortcut>(&sc)) {
        m_gesture.reset(new SwipeGesture);
        m_gesture->setDirection(dirs[swipeGesture->swipeDirection]);
        m_gesture->setMaximumFingerCount(4);
        m_gesture->setMinimumFingerCount(4);
        QObject::connect(m_gesture.get(), &SwipeGesture::triggered, m_action, &QAction::trigger, Qt::QueuedConnection);
    }
}

GlobalShortcut::~GlobalShortcut()
{
}

QAction* GlobalShortcut::action() const
{
    return m_action;
}

void GlobalShortcut::invoke() const
{
    QMetaObject::invokeMethod(m_action, "trigger", Qt::QueuedConnection);
}

const Shortcut& GlobalShortcut::shortcut() const
{
    return m_shortcut;
}

SwipeGesture* GlobalShortcut::swipeGesture() const
{
    return m_gesture.get();
}

GlobalShortcutsManager::GlobalShortcutsManager(QObject *parent)
    : QObject(parent)
    , m_gestureRecognizer(new GestureRecognizer(this))
{
}

GlobalShortcutsManager::~GlobalShortcutsManager()
{
}

void GlobalShortcutsManager::init()
{
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        qputenv("KGLOBALACCELD_PLATFORM", QByteArrayLiteral("org.kde.kwin"));
        m_kglobalAccel = new KGlobalAccelD(this);
        if (!m_kglobalAccel->init()) {
            qCDebug(KWIN_CORE) << "Init of kglobalaccel failed";
            delete m_kglobalAccel;
            m_kglobalAccel = nullptr;
        } else {
            qCDebug(KWIN_CORE) << "KGlobalAcceld inited";
        }
    }
}

void GlobalShortcutsManager::objectDeleted(QObject *object)
{
    auto it = m_shortcuts.begin();
    while (it != m_shortcuts.end()) {
        if (it->action() == object) {
            it = m_shortcuts.erase(it);
        } else {
            ++it;
        }
    }
}

bool GlobalShortcutsManager::addIfNotExists(GlobalShortcut sc)
{
    for (const auto& cs : m_shortcuts) {
        if (sc.shortcut() == cs.shortcut()) {
            return false;
        }
    }

    if (std::holds_alternative<FourFingerSwipeShortcut>(sc.shortcut())) {
        m_gestureRecognizer->registerGesture(sc.swipeGesture());
    }
    connect(sc.action(), &QAction::destroyed, this, &GlobalShortcutsManager::objectDeleted);
    m_shortcuts.push_back(std::move(sc));
    return true;
}

void GlobalShortcutsManager::registerPointerShortcut(QAction *action, Qt::KeyboardModifiers modifiers, Qt::MouseButtons pointerButtons)
{
    addIfNotExists(GlobalShortcut(PointerButtonShortcut{ modifiers, pointerButtons }, action));
}

void GlobalShortcutsManager::registerAxisShortcut(QAction *action, Qt::KeyboardModifiers modifiers, PointerAxisDirection axis)
{
    addIfNotExists(GlobalShortcut(PointerAxisShortcut{ modifiers, axis }, action));
}

void GlobalShortcutsManager::registerTouchpadSwipe(QAction *action, SwipeDirection direction)
{
    addIfNotExists(GlobalShortcut(FourFingerSwipeShortcut{ direction }, action));
}

bool GlobalShortcutsManager::processKey(Qt::KeyboardModifiers mods, int keyQt)
{
    if (m_kglobalAccelInterface) {
        if (!keyQt && !mods) {
            return false;
        }
        auto check = [this] (Qt::KeyboardModifiers mods, int keyQt) {
            bool retVal = false;
            QMetaObject::invokeMethod(m_kglobalAccelInterface,
                                        "checkKeyPressed",
                                        Qt::DirectConnection,
                                        Q_RETURN_ARG(bool, retVal),
                                        Q_ARG(int, int(mods) | keyQt));
            return retVal;
        };
        if (check(mods, keyQt)) {
            return true;
        } else if (keyQt == Qt::Key_Backtab) {
            // KGlobalAccel on X11 has some workaround for Backtab
            // see kglobalaccel/src/runtime/plugins/xcb/kglobalccel_x11.cpp method x11KeyPress
            // Apparently KKeySequenceWidget captures Shift+Tab instead of Backtab
            // thus if the key is backtab we should adjust to add shift again and use tab
            // in addition KWin registers the shortcut incorrectly as Alt+Shift+Backtab
            // this should be changed to either Alt+Backtab or Alt+Shift+Tab to match KKeySequenceWidget
            // trying the variants
            if (check(mods | Qt::ShiftModifier, keyQt)) {
                return true;
            }
            if (check(mods | Qt::ShiftModifier, Qt::Key_Tab)) {
                return true;
            }
        }
    }
    return false;
}

template<typename ShortcutKind, typename... Args>
bool match(QVector<GlobalShortcut> &shortcuts, Args... args)
{
    for (auto& sc : shortcuts) {
        if (std::holds_alternative<ShortcutKind>(sc.shortcut())) {
            if (std::get<ShortcutKind>(sc.shortcut()) == ShortcutKind{args...}) {
                sc.invoke();
                return true;
            }
        }
    }
    return false;
}

//TODO(C++20): use ranges for a nicer way of filtering by shortcut type
bool GlobalShortcutsManager::processPointerPressed(Qt::KeyboardModifiers mods, Qt::MouseButtons pointerButtons)
{
    return match<PointerButtonShortcut>(m_shortcuts, mods, pointerButtons);
}

bool GlobalShortcutsManager::processAxis(Qt::KeyboardModifiers mods, PointerAxisDirection axis)
{
    return match<PointerAxisShortcut>(m_shortcuts, mods, axis);
}

void GlobalShortcutsManager::processSwipeStart(uint fingerCount)
{
    m_gestureRecognizer->startSwipeGesture(fingerCount);
}

void GlobalShortcutsManager::processSwipeUpdate(const QSizeF &delta)
{
    m_gestureRecognizer->updateSwipeGesture(delta);
}

void GlobalShortcutsManager::processSwipeCancel()
{
    m_gestureRecognizer->cancelSwipeGesture();
}

void GlobalShortcutsManager::processSwipeEnd()
{
    m_gestureRecognizer->endSwipeGesture();
    // TODO: cancel on Wayland Seat if one triggered
}

} // namespace
