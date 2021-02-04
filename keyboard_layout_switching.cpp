/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Gräßlin <mgraesslin@kde.org>

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
#include "keyboard_layout_switching.h"
#include "keyboard_layout.h"
#include "toplevel.h"
#include "virtualdesktops.h"
#include "workspace.h"
#include "xkb.h"

#include "win/control.h"
#include "win/net.h"
#include "win/util.h"

namespace KWin
{

namespace KeyboardLayoutSwitching
{

Policy::Policy(Xkb *xkb, KeyboardLayout *layout, const KConfigGroup &config)
    : QObject(layout)
    , m_config(config)
    , m_xkb(xkb)
    , m_layout(layout)
{
    connect(m_layout, &KeyboardLayout::layoutsReconfigured, this, &Policy::clearCache);
    connect(m_layout, &KeyboardLayout::layoutChanged, this, &Policy::layoutChanged);
}

Policy::~Policy() = default;

void Policy::setLayout(uint index)
{
    const uint previousLayout = m_xkb->currentLayout();
    m_xkb->switchToLayout(index);
    const uint currentLayout = m_xkb->currentLayout();
    if (previousLayout != currentLayout) {
        emit m_layout->layoutChanged(currentLayout);
    }
}

Policy *Policy::create(Xkb *xkb, KeyboardLayout *layout, const KConfigGroup &config, const QString &policy)
{
    if (policy.toLower() == QStringLiteral("desktop")) {
        return new VirtualDesktopPolicy(xkb, layout, config);
    }
    if (policy.toLower() == QStringLiteral("window")) {
        return new WindowPolicy(xkb, layout);
    }
    if (policy.toLower() == QStringLiteral("winclass")) {
        return new ApplicationPolicy(xkb, layout, config);
    }
    return new GlobalPolicy(xkb, layout, config);
}

const char Policy::defaultLayoutEntryKeyPrefix[] = "LayoutDefault";
const QString Policy::defaultLayoutEntryKey() const
{
    return QLatin1String(defaultLayoutEntryKeyPrefix) % name() % QLatin1Char('_');
}

void Policy::clearLayouts()
{
    const QStringList layoutEntryList = m_config.keyList().filter(defaultLayoutEntryKeyPrefix);
    for (const auto &layoutEntry : layoutEntryList) {
        m_config.deleteEntry(layoutEntry);
    }
}

const QString GlobalPolicy::defaultLayoutEntryKey() const
{
    return QLatin1String(defaultLayoutEntryKeyPrefix) % name();
}

GlobalPolicy::GlobalPolicy(Xkb *xkb, KeyboardLayout *_layout, const KConfigGroup &config)
    : Policy(xkb, _layout, config)
{
    connect(workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this,
        [this, xkb] (const QString &name) {
            Q_UNUSED(name)
            clearLayouts();
            if (const uint layout = xkb->currentLayout()) {
                m_config.writeEntry(defaultLayoutEntryKey(), layout);
            }
        }
    );

    connect(workspace()->sessionManager(), &SessionManager::loadSessionRequested, this,
        [this, xkb] (const QString &name) {
            Q_UNUSED(name)
            if (xkb->numberOfLayouts() > 1) {
                setLayout(m_config.readEntry(defaultLayoutEntryKey(), 0));
            }
        }
    );
}

GlobalPolicy::~GlobalPolicy() = default;

VirtualDesktopPolicy::VirtualDesktopPolicy(Xkb *xkb, KeyboardLayout *layout, const KConfigGroup &config)
    : Policy(xkb, layout, config)
{
    connect(VirtualDesktopManager::self(), &VirtualDesktopManager::currentChanged,
            this, 						   &VirtualDesktopPolicy::desktopChanged);

    connect(workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this,
        [this] (const QString &name) {
            Q_UNUSED(name)
            clearLayouts();

            for (auto i = m_layouts.constBegin(); i != m_layouts.constEnd(); ++i) {
                if (const uint layout = *i) {
                    m_config.writeEntry(
                                defaultLayoutEntryKey() %
                                    QLatin1String( QByteArray::number(i.key()->x11DesktopNumber()) ),
                                layout);
                }
            }
        }
    );

    connect(workspace()->sessionManager(), &SessionManager::loadSessionRequested, this,
        [this, xkb] (const QString &name) {
            Q_UNUSED(name)
            if (xkb->numberOfLayouts() > 1) {
                const auto &desktops = VirtualDesktopManager::self()->desktops();
                for (KWin::VirtualDesktop* const desktop : desktops) {
                    const uint layout = m_config.readEntry(
                                defaultLayoutEntryKey() %
                                    QLatin1String( QByteArray::number(desktop->x11DesktopNumber()) ),
                                0u);
                    if (layout) {
                        m_layouts.insert(desktop, layout);
                        connect(desktop, &VirtualDesktop::aboutToBeDestroyed, this,
                            [this, desktop] {
                                m_layouts.remove(desktop);
                            }
                        );
                    }
                }
                desktopChanged();
            }
        }
    );
}

VirtualDesktopPolicy::~VirtualDesktopPolicy() = default;

void VirtualDesktopPolicy::clearCache()
{
    m_layouts.clear();
}

namespace {
template <typename T, typename U>
quint32 getLayout(const T &layouts, const U &reference)
{
    auto it = layouts.constFind(reference);
    if (it == layouts.constEnd()) {
        return 0;
    } else {
        return it.value();
    }
}
}

void VirtualDesktopPolicy::desktopChanged()
{
    auto d = VirtualDesktopManager::self()->currentDesktop();
    if (!d) {
        return;
    }
    setLayout(getLayout(m_layouts, d));
}

void VirtualDesktopPolicy::layoutChanged(uint index)
{
    auto d = VirtualDesktopManager::self()->currentDesktop();
    if (!d) {
        return;
    }
    auto it = m_layouts.find(d);
    if (it == m_layouts.end()) {
        m_layouts.insert(d, index);
        connect(d, &VirtualDesktop::aboutToBeDestroyed, this,
            [this, d] {
                m_layouts.remove(d);
            }
        );
    } else {
        if (it.value() == index) {
            return;
        }
        it.value() = index;
    }
}

WindowPolicy::WindowPolicy(KWin::Xkb* xkb, KWin::KeyboardLayout* layout)
    : Policy(xkb, layout)
{
    connect(workspace(), &Workspace::clientActivated, this,
        [this] (Toplevel* window) {
            if (!window) {
                return;
            }
            // ignore some special types
            if (win::is_desktop(window) || win::is_dock(window)) {
                return;
            }
            setLayout(getLayout(m_layouts, window));
        }
    );
}

WindowPolicy::~WindowPolicy()
{
}

void WindowPolicy::clearCache()
{
    m_layouts.clear();
}

void WindowPolicy::layoutChanged(uint index)
{
    auto c = workspace()->activeClient();
    if (!c) {
        return;
    }
    // ignore some special types
    if (win::is_desktop(c) || win::is_dock(c)) {
        return;
    }

    auto it = m_layouts.find(c);
    if (it == m_layouts.end()) {
        m_layouts.insert(c, index);
        connect(c, &Toplevel::windowClosed, this,
            [this, c] {
                m_layouts.remove(c);
            }
        );
    } else {
        if (it.value() == index) {
            return;
        }
        it.value() = index;
    }
}

ApplicationPolicy::ApplicationPolicy(KWin::Xkb* xkb, KWin::KeyboardLayout* layout, const KConfigGroup &config)
    : Policy(xkb, layout, config)
{
    connect(workspace(), &Workspace::clientActivated, this, &ApplicationPolicy::clientActivated);

    connect(workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this,
        [this] (const QString &name) {
            Q_UNUSED(name)
            clearLayouts();

            for (auto i = m_layouts.constBegin(); i != m_layouts.constEnd(); ++i) {
                if (const uint layout = *i) {
                    auto const desktopFileName = i.key()->control->desktop_file_name();
                    if (!desktopFileName.isEmpty()) {
                        m_config.writeEntry(
                                    defaultLayoutEntryKey() % QLatin1String(desktopFileName),
                                    layout);
                    }
                }
            }
        }
    );

    connect(workspace()->sessionManager(), &SessionManager::loadSessionRequested, this,
        [this, xkb] (const QString &name) {
            Q_UNUSED(name)
            if (xkb->numberOfLayouts() > 1) {
                const QString keyPrefix = defaultLayoutEntryKey();
                const QStringList keyList = m_config.keyList().filter(keyPrefix);
                for (const QString& key : keyList) {
                    m_layoutsRestored.insert(
                                key.midRef(keyPrefix.size()).toLatin1(),
                                m_config.readEntry(key, 0));
                }
            }
            m_layoutsRestored.squeeze();
        }
    );
}

ApplicationPolicy::~ApplicationPolicy()
{
}

void ApplicationPolicy::clientActivated(Toplevel* window)
{
    if (!window) {
        return;
    }
    // ignore some special types
    if (win::is_desktop(window) || win::is_dock(window)) {
        return;
    }
    auto it = m_layouts.constFind(window);
    if(it != m_layouts.constEnd()) {
        setLayout(it.value());
        return;
    };
    for (it = m_layouts.constBegin(); it != m_layouts.constEnd(); it++) {
        if (win::belong_to_same_client(window, it.key())) {
            auto const layout = it.value();
            setLayout(layout);
            layoutChanged(layout);
            return;
        }
    }
    setLayout( m_layoutsRestored.take(window->control->desktop_file_name()) );
    if (auto const index = m_xkb->currentLayout()) {
        layoutChanged(index);
    }
}

void ApplicationPolicy::clearCache()
{
    m_layouts.clear();
}

void ApplicationPolicy::layoutChanged(uint index)
{
    auto c = workspace()->activeClient();
    if (!c) {
        return;
    }
    // ignore some special types
    if (win::is_desktop(c) || win::is_dock(c)) {
        return;
    }

    auto it = m_layouts.find(c);
    if (it == m_layouts.end()) {
        m_layouts.insert(c, index);
        connect(c, &Toplevel::windowClosed, this,
            [this, c] {
                m_layouts.remove(c);
            }
        );
    } else {
        if (it.value() == index) {
            return;
        }
        it.value() = index;
    }
    // update all layouts for the application
    for (it = m_layouts.begin(); it != m_layouts.end(); it++) {
        if (win::belong_to_same_client(it.key(), c)) {
            it.value() = index;
        }
    }
}

}
}
