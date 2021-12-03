/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
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
#include "workspace.h"

#include <kwinglplatform.h>

#include "atoms.h"
#include "base/x11/event_filter.h"
#include "base/x11/event_filter_container.h"
#include "base/x11/event_filter_manager.h"
#include "dbusinterface.h"
#include "input/cursor.h"
#include "killwindow.h"
#include "moving_client_x11_filter.h"
#include "platform.h"
#include "render/effects.h"
#include "render/x11/compositor.h"
#include "render/x11/outline.h"
#include "rules/rule_book.h"
#include "rules/rules.h"
#include "screenedge.h"
#include "screens.h"
#include "scripting/platform.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif
#include "decorations/decorationbridge.h"
#include "main.h"
#include "useractions.h"
#include "virtualdesktops.h"
#include "was_user_interaction_x11_filter.h"
#include "xcbutils.h"

#include "win/app_menu.h"
#include "win/controlling.h"
#include "win/focus_chain.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/layers.h"
#include "win/remnant.h"
#include "win/setup.h"
#include "win/space.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/util.h"

#include "win/x11/control.h"
#include "win/x11/event.h"
#include "win/x11/group.h"
#include "win/x11/netinfo.h"
#include "win/x11/space_areas.h"
#include "win/x11/stacking_tree.h"
#include "win/x11/sync_alarm_filter.h"
#include "win/x11/transient.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KStartupInfo>

#include <QtConcurrentRun>
#include <memory>

namespace KWin
{

ColorMapper::ColorMapper(QObject* parent)
    : QObject(parent)
    , m_default(defaultScreen()->default_colormap)
    , m_installed(defaultScreen()->default_colormap)
{
}

ColorMapper::~ColorMapper()
{
}

void ColorMapper::update()
{
    xcb_colormap_t cmap = m_default;
    if (auto c = dynamic_cast<win::x11::window*>(Workspace::self()->activeClient())) {
        if (c->colormap != XCB_COLORMAP_NONE) {
            cmap = c->colormap;
        }
    }
    if (cmap != m_installed) {
        xcb_install_colormap(connection(), cmap);
        m_installed = cmap;
    }
}

Workspace* Workspace::_self = nullptr;

Workspace::Workspace()
    : QObject(nullptr)
    , stacking_order(new win::stacking_order)
    , x_stacking_tree(std::make_unique<win::x11::stacking_tree>())
    , m_userActionsMenu(new UserActionsMenu(this))
    , m_sessionManager(new SessionManager(this))
{
    // For invoke methods of UserActionsMenu.
    qRegisterMetaType<Toplevel*>();

    win::app_menu::create(this);

    _self = this;

    m_quickTileCombineTimer = new QTimer(this);
    m_quickTileCombineTimer->setSingleShot(true);

    RuleBook::create(this)->load();

    Q_ASSERT(Screens::self());
    ScreenEdges::create(this);

    // VirtualDesktopManager needs to be created prior to init shortcuts
    // and prior to TabBox, due to TabBox connecting to signals
    // actual initialization happens in init()
    VirtualDesktopManager::create(this);

    // dbus interface
    new VirtualDesktopManagerDBusInterface(VirtualDesktopManager::self());

#ifdef KWIN_BUILD_TABBOX
    // need to create the tabbox before compositing scene is setup
    TabBox::TabBox::create(this);
#endif

    m_compositor = render::compositor::self();
    assert(m_compositor);

    connect(
        this, &Workspace::currentDesktopChanged, m_compositor, &render::compositor::addRepaintFull);
    connect(m_compositor, &QObject::destroyed, this, [this] { m_compositor = nullptr; });

    auto decorationBridge = Decoration::DecorationBridge::create(this);
    decorationBridge->init();
    connect(this,
            &Workspace::configChanged,
            decorationBridge,
            &Decoration::DecorationBridge::reconfigure);

    connect(
        m_sessionManager, &SessionManager::loadSessionRequested, this, &Workspace::loadSessionInfo);

    connect(m_sessionManager,
            &SessionManager::prepareSessionSaveRequested,
            this,
            [this](const QString& name) { storeSession(name, SMSavePhase0); });
    connect(m_sessionManager,
            &SessionManager::finishSessionSaveRequested,
            this,
            [this](const QString& name) { storeSession(name, SMSavePhase2); });

    new DBusInterface(this);

    render::x11::outline::create(this);

    initShortcuts();

    KSharedConfigPtr config = kwinApp()->config();
    Screens* screens = Screens::self();

    // get screen support
    connect(screens, &Screens::changed, this, &Workspace::desktopResized);
    screens->setConfig(config);
    screens->reconfigure();
    connect(options, &Options::configChanged, screens, &Screens::reconfigure);

    ScreenEdges* screenEdges = ScreenEdges::self();
    screenEdges->setConfig(config);
    screenEdges->init();
    connect(options, &Options::configChanged, screenEdges, &ScreenEdges::reconfigure);
    connect(VirtualDesktopManager::self(),
            &VirtualDesktopManager::layoutChanged,
            screenEdges,
            &ScreenEdges::updateLayout);
    connect(this, &Workspace::clientActivated, screenEdges, &ScreenEdges::checkBlocking);

    auto* focusChain = win::focus_chain::create(this);
    connect(this, &Workspace::clientRemoved, focusChain, &win::focus_chain::remove);
    connect(this, &Workspace::clientActivated, focusChain, &win::focus_chain::setActiveClient);
    connect(VirtualDesktopManager::self(),
            &VirtualDesktopManager::countChanged,
            focusChain,
            &win::focus_chain::resize);
    connect(VirtualDesktopManager::self(),
            &VirtualDesktopManager::currentChanged,
            focusChain,
            &win::focus_chain::setCurrentDesktop);
    connect(options,
            &Options::separateScreenFocusChanged,
            focusChain,
            &win::focus_chain::setSeparateScreenFocus);
    focusChain->setSeparateScreenFocus(options->isSeparateScreenFocus());

    // create VirtualDesktopManager and perform dependency injection
    VirtualDesktopManager* vds = VirtualDesktopManager::self();
    connect(vds, &VirtualDesktopManager::countChanged, this, &Workspace::slotDesktopCountChanged);
    connect(
        vds, &VirtualDesktopManager::currentChanged, this, &Workspace::slotCurrentDesktopChanged);
    vds->setNavigationWrappingAround(options->isRollOverDesktops());
    connect(options,
            &Options::rollOverDesktopsChanged,
            vds,
            &VirtualDesktopManager::setNavigationWrappingAround);
    vds->setConfig(config);

    // positioning object needs to be created before the virtual desktops are loaded.
    vds->load();
    vds->updateLayout();

    // makes sure any autogenerated id is saved, necessary as in case of xwayland, load will be
    // called 2 times
    // load is needed to be called again when starting xwayalnd to sync to RootInfo, see BUG 385260
    vds->save();

    if (!VirtualDesktopManager::self()->setCurrent(m_initialDesktop))
        VirtualDesktopManager::self()->setCurrent(1);

    reconfigureTimer.setSingleShot(true);
    updateToolWindowsTimer.setSingleShot(true);

    connect(&reconfigureTimer, &QTimer::timeout, this, &Workspace::slotReconfigure);
    connect(&updateToolWindowsTimer, &QTimer::timeout, this, &Workspace::slotUpdateToolWindows);

    // TODO: do we really need to reconfigure everything when fonts change?
    // maybe just reconfigure the decorations? Move this into libkdecoration?
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KDEPlatformTheme"),
                                          QStringLiteral("org.kde.KDEPlatformTheme"),
                                          QStringLiteral("refreshFonts"),
                                          this,
                                          SLOT(reconfigure()));

    active_client = nullptr;
    connect(stacking_order, &win::stacking_order::changed, this, [this] {
        if (active_client) {
            active_client->control->update_mouse_grab();
        }
    });

    initWithX11();
    scripting = std::make_unique<scripting::platform>();

    // SELI TODO: This won't work with unreasonable focus policies,
    // and maybe in rare cases also if the selected client doesn't
    // want focus
    workspaceInit = false;

    // Start the scripting platform, but first process all events.
    // TODO(romangg): Can we also do this through a simple call?
    QMetaObject::invokeMethod(scripting.get(), "start", Qt::QueuedConnection);

    // TODO: ungrabXServer()
}

void Workspace::initWithX11()
{
    if (!kwinApp()->x11Connection()) {
        connect(kwinApp(),
                &Application::x11ConnectionChanged,
                this,
                &Workspace::initWithX11,
                Qt::UniqueConnection);
        return;
    }
    disconnect(kwinApp(), &Application::x11ConnectionChanged, this, &Workspace::initWithX11);

    atoms->retrieveHelpers();

    // first initialize the extensions
    Xcb::Extensions::self();
    ColorMapper* colormaps = new ColorMapper(this);
    connect(this, &Workspace::clientActivated, colormaps, &ColorMapper::update);

    // Call this before XSelectInput() on the root window
    startup = new KStartupInfo(
        KStartupInfo::DisableKWinModule | KStartupInfo::AnnounceSilenceChanges, this);

    // Select windowmanager privileges
    selectWmInputEventMask();

    // Compatibility
    int32_t data = 1;

    xcb_change_property(connection(),
                        XCB_PROP_MODE_APPEND,
                        rootWindow(),
                        atoms->kwin_running,
                        atoms->kwin_running,
                        32,
                        1,
                        &data);

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        m_wasUserInteractionFilter.reset(new WasUserInteractionX11Filter);
        m_movingClientFilter.reset(new MovingClientX11Filter);
    }
    if (Xcb::Extensions::self()->isSyncAvailable()) {
        m_syncAlarmFilter.reset(new win::x11::sync_alarm_filter);
    }
    updateXTime(); // Needed for proper initialization of user_time in Client ctor

    const uint32_t nullFocusValues[] = {true};
    m_nullFocus.reset(new Xcb::Window(QRect(-1, -1, 1, 1),
                                      XCB_WINDOW_CLASS_INPUT_ONLY,
                                      XCB_CW_OVERRIDE_REDIRECT,
                                      nullFocusValues));
    m_nullFocus->map();

    auto* rootInfo = win::x11::root_info::create();
    const auto vds = VirtualDesktopManager::self();
    vds->setRootInfo(rootInfo);
    rootInfo->activate();

    // TODO: only in X11 mode
    // Extra NETRootInfo instance in Client mode is needed to get the values of the properties
    NETRootInfo client_info(connection(), NET::ActiveWindow | NET::CurrentDesktop);
    if (!qApp->isSessionRestored()) {
        m_initialDesktop = client_info.currentDesktop();
        vds->setCurrent(m_initialDesktop);
    }

    // TODO: better value
    rootInfo->setActiveWindow(XCB_WINDOW_NONE);
    focusToNull();

    if (!qApp->isSessionRestored())
        ++block_focus; // Because it will be set below

    {
        // Begin updates blocker block
        Blocker blocker(stacking_order);

        Xcb::Tree tree(rootWindow());
        xcb_window_t* wins = xcb_query_tree_children(tree.data());

        QVector<Xcb::WindowAttributes> windowAttributes(tree->children_len);
        QVector<Xcb::WindowGeometry> windowGeometries(tree->children_len);

        // Request the attributes and geometries of all toplevel windows
        for (int i = 0; i < tree->children_len; i++) {
            windowAttributes[i] = Xcb::WindowAttributes(wins[i]);
            windowGeometries[i] = Xcb::WindowGeometry(wins[i]);
        }

        // Get the replies
        for (int i = 0; i < tree->children_len; i++) {
            Xcb::WindowAttributes attr(windowAttributes.at(i));

            if (attr.isNull()) {
                continue;
            }

            if (attr->override_redirect) {
                if (attr->map_state == XCB_MAP_STATE_VIEWABLE
                    && attr->_class != XCB_WINDOW_CLASS_INPUT_ONLY)
                    // ### This will request the attributes again
                    createUnmanaged(wins[i]);
            } else if (attr->map_state != XCB_MAP_STATE_UNMAPPED) {
                if (Application::wasCrash()) {
                    fixPositionAfterCrash(wins[i], windowGeometries.at(i).data());
                }

                // ### This will request the attributes again
                createClient(wins[i], true);
            }
        }

        // Propagate clients, will really happen at the end of the updates blocker block
        stacking_order->update(true);

        saveOldScreenSizes();
        updateClientArea();

        // NETWM spec says we have to set it to (0,0) if we don't support it
        NETPoint* viewports = new NETPoint[VirtualDesktopManager::self()->count()];
        rootInfo->setDesktopViewport(VirtualDesktopManager::self()->count(), *viewports);
        delete[] viewports;
        QRect geom;
        for (int i = 0; i < screens()->count(); i++) {
            geom |= screens()->geometry(i);
        }
        NETSize desktop_geometry;
        desktop_geometry.width = geom.width();
        desktop_geometry.height = geom.height();
        rootInfo->setDesktopGeometry(desktop_geometry);
        setShowingDesktop(false);

    } // End updates blocker block

    // TODO: only on X11?
    Toplevel* new_active_client = nullptr;
    if (!qApp->isSessionRestored()) {
        --block_focus;
        new_active_client
            = findClient(win::x11::predicate_match::window, client_info.activeWindow());
    }
    if (new_active_client == nullptr && activeClient() == nullptr && should_get_focus.size() == 0) {
        // No client activated in manage()
        if (new_active_client == nullptr)
            new_active_client
                = win::top_client_on_desktop(this, VirtualDesktopManager::self()->current(), -1);
        if (new_active_client == nullptr) {
            new_active_client
                = win::find_desktop(this, true, VirtualDesktopManager::self()->current());
        }
    }
    if (new_active_client != nullptr)
        activateClient(new_active_client);
}

void Workspace::clear_x11()
{
    stacking_order->lock();

    // Use stacking_order, so that kwin --replace keeps stacking order
    auto const stack = stacking_order->sorted();

    // "mutex" the stackingorder, since anything trying to access it from now on will find
    // many dangeling pointers and crash
    stacking_order->win_stack.clear();

    // Only release windows on X11.
    auto is_x11 = kwinApp()->operationMode() == Application::OperationModeX11;

    for (auto it = stack.cbegin(), end = stack.cend(); it != end; ++it) {
        auto c = qobject_cast<win::x11::window*>(const_cast<Toplevel*>(*it));
        if (!c) {
            continue;
        }

        win::x11::release_window(c, is_x11);

        // No removeClient() is called, it does more than just removing.
        // However, remove from some lists to e.g. prevent performTransiencyCheck()
        // from crashing.
        remove_all(m_allClients, c);
        remove_all(m_windows, c);
    }

    for (auto const& unmanaged : unmanagedList()) {
        win::x11::release_window(static_cast<win::x11::window*>(unmanaged), is_x11);
        remove_all(m_windows, unmanaged);
        remove_all(stacking_order->pre_stack, unmanaged);
    }

    win::x11::window::cleanupX11();

    stacking_order->unlock();
}

Workspace::~Workspace()
{
    stacking_order->lock();

    // TODO: grabXServer();

    clear_x11();

    for (auto const& window : m_windows) {
        if (auto internal = qobject_cast<win::internal_window*>(window)) {
            internal->destroyClient();
            remove_all(m_windows, internal);
        }
    }

    // At this point only remnants are remaining.
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        assert((*it)->remnant());
        Q_EMIT deletedRemoved(*it);
        it = m_windows.erase(it);
    }

    assert(m_windows.empty());

    if (auto c = kwinApp()->x11Connection()) {
        xcb_delete_property(c, kwinApp()->x11RootWindow(), atoms->kwin_running);
    }

    delete stacking_order;

    delete RuleBook::self();
    kwinApp()->config()->sync();

    win::x11::root_info::destroy();
    delete startup;
    delete client_keys_dialog;
    for (auto const& s : session)
        delete s;

    // TODO: ungrabXServer();

    Xcb::Extensions::destroy();
    _self = nullptr;
}

win::x11::window* Workspace::createClient(xcb_window_t w, bool is_mapped)
{
    Blocker blocker(stacking_order);

    auto c = win::x11::create_controlled_window<win::x11::window>(w, is_mapped);
    if (c) {
        addClient(c);
    }
    return c;
}

win::x11::window* Workspace::createUnmanaged(xcb_window_t w)
{
    if (auto compositor = render::x11::compositor::self()) {
        if (compositor->checkForOverlayWindow(w)) {
            return nullptr;
        }
    }
    auto c = win::x11::create_unmanaged_window<win::x11::window>(w);
    if (!c) {
        return nullptr;
    }
    connect(c, &win::x11::window::needsRepaint, m_compositor, [c] {
        render::compositor::self()->schedule_repaint(c);
    });
    addUnmanaged(c);
    Q_EMIT unmanagedAdded(c);
    return c;
}

void Workspace::addClient(win::x11::window* c)
{
    auto grp = findGroup(c->xcb_window());

    Q_EMIT clientAdded(c);

    if (grp != nullptr)
        grp->gotLeader(c);

    if (win::is_desktop(c)) {
        if (active_client == nullptr && should_get_focus.empty() && c->isOnCurrentDesktop()) {
            // TODO: Make sure desktop is active after startup if there's no other window active
            request_focus(c);
        }
    } else {
        win::focus_chain::self()->update(c, win::focus_chain::Update);
    }

    m_windows.push_back(c);
    m_allClients.push_back(c);

    if (!contains(stacking_order->pre_stack, c)) {
        // Raise if it hasn't got any stacking position yet
        stacking_order->pre_stack.push_back(c);
    }
    if (!contains(stacking_order->sorted(), c)) {
        // It'll be updated later, and updateToolWindows() requires c to be in stacking_order.
        stacking_order->win_stack.push_back(c);
    }
    x_stacking_tree->mark_as_dirty();
    updateClientArea(); // This cannot be in manage(), because the client got added only now
    win::update_layer(c);
    if (win::is_desktop(c)) {
        win::raise_window(this, c);
        // If there's no active client, make this desktop the active one
        if (activeClient() == nullptr && should_get_focus.size() == 0)
            activateClient(win::find_desktop(this, true, VirtualDesktopManager::self()->current()));
    }
    win::x11::check_active_modal<win::x11::window>();
    checkTransients(c);
    stacking_order->update(true); // Propagate new client
    if (win::is_utility(c) || win::is_menu(c) || win::is_toolbar(c)) {
        win::update_tool_windows(this, true);
    }
    updateTabbox();
}

void Workspace::addUnmanaged(Toplevel* c)
{
    m_windows.push_back(c);
    x_stacking_tree->mark_as_dirty();
}

/**
 * Destroys the client \a c
 */
void Workspace::removeClient(win::x11::window* c)
{
    if (c == active_popup_client)
        closeActivePopup();
    if (m_userActionsMenu->isMenuClient(c)) {
        m_userActionsMenu->close();
    }

    if (client_keys_client == c)
        setupWindowShortcutDone(false);
    if (!c->control->shortcut().isEmpty()) {
        // Remove from client_keys.
        win::set_shortcut(c, QString());

        // Needed, since this is otherwise delayed by setShortcut() and wouldn't run
        clientShortcutUpdated(c);
    }

    assert(contains(m_allClients, c));
    // TODO: if marked client is removed, notify the marked list
    remove_all(m_allClients, c);
    remove_all(m_windows, c);
    x_stacking_tree->mark_as_dirty();
    remove_all(attention_chain, c);

    auto group = findGroup(c->xcb_window());
    if (group != nullptr)
        group->lostLeader();

    if (c == most_recently_raised) {
        most_recently_raised = nullptr;
    }
    remove_all(should_get_focus, c);
    Q_ASSERT(c != active_client);
    if (c == last_active_client)
        last_active_client = nullptr;
    if (c == delayfocus_client)
        cancelDelayFocus();

    Q_EMIT clientRemoved(c);

    stacking_order->update(true);
    updateClientArea();
    updateTabbox();
}

void Workspace::removeUnmanaged(Toplevel* window)
{
    Q_ASSERT(contains(m_windows, window));
    remove_all(m_windows, window);
    Q_EMIT unmanagedRemoved(window);
    x_stacking_tree->mark_as_dirty();
}

void Workspace::addDeleted(Toplevel* c, Toplevel* orig)
{
    assert(!contains(m_windows, c));

    m_remnant_count++;
    m_windows.push_back(c);

    auto const unconstraintedIndex = index_of(stacking_order->pre_stack, orig);
    if (unconstraintedIndex != -1) {
        stacking_order->pre_stack.at(unconstraintedIndex) = c;
    } else {
        stacking_order->pre_stack.push_back(c);
    }
    auto const index = index_of(stacking_order->sorted(), orig);
    if (index != -1) {
        stacking_order->win_stack.at(index) = c;
    } else {
        stacking_order->win_stack.push_back(c);
    }
    x_stacking_tree->mark_as_dirty();
    connect(c, &Toplevel::needsRepaint, m_compositor, [c] {
        render::compositor::self()->schedule_repaint(c);
    });
}

void Workspace::removeDeleted(Toplevel* window)
{
    assert(contains(m_windows, window));

    Q_EMIT deletedRemoved(window);
    m_remnant_count--;

    remove_all(m_windows, window);
    remove_all(stacking_order->pre_stack, window);
    remove_all(stacking_order->win_stack, window);

    x_stacking_tree->mark_as_dirty();

    if (auto compositor = render::x11::compositor::self();
        compositor && window->remnant()->control) {
        compositor->updateClientCompositeBlocking();
    }
}

void Workspace::stopUpdateToolWindowsTimer()
{
    updateToolWindowsTimer.stop();
}

void Workspace::resetUpdateToolWindowsTimer()
{
    updateToolWindowsTimer.start(200);
}

void Workspace::slotUpdateToolWindows()
{
    win::update_tool_windows(this, true);
}

void Workspace::slotReloadConfig()
{
    reconfigure();
}

void Workspace::reconfigure()
{
    reconfigureTimer.start(200);
}

/**
 * Reread settings
 */

void Workspace::slotReconfigure()
{
    qCDebug(KWIN_CORE) << "Workspace::slotReconfigure()";
    reconfigureTimer.stop();

    bool borderlessMaximizedWindows = options->borderlessMaximizedWindows();

    kwinApp()->config()->reparseConfiguration();
    options->updateSettings();
    scripting->start();

    Q_EMIT configChanged();

    m_userActionsMenu->discard();
    win::update_tool_windows(this, true);

    RuleBook::self()->load();
    for (auto window : m_allClients) {
        if (window->supportsWindowRules()) {
            win::evaluate_rules(window);
            RuleBook::self()->discardUsed(window, false);
        }
    }

    if (borderlessMaximizedWindows != options->borderlessMaximizedWindows()
        && !options->borderlessMaximizedWindows()) {
        // in case borderless maximized windows option changed and new option
        // is to have borders, we need to unset the borders for all maximized windows
        for (auto it = m_allClients.begin(); it != m_allClients.end(); ++it) {
            if ((*it)->maximizeMode() == win::maximize_mode::full)
                (*it)->checkNoBorder();
        }
    }
}

void Workspace::slotCurrentDesktopChanged(uint oldDesktop, uint newDesktop)
{
    closeActivePopup();
    ++block_focus;
    Blocker blocker(stacking_order);
    win::update_client_visibility_on_desktop_change(this, newDesktop);
    // Restore the focus on this desktop
    --block_focus;

    activateClientOnNewDesktop(newDesktop);
    Q_EMIT currentDesktopChanged(oldDesktop, movingClient);
}

void Workspace::activateClientOnNewDesktop(uint desktop)
{
    Toplevel* c = nullptr;
    if (options->focusPolicyIsReasonable()) {
        c = findClientToActivateOnDesktop(desktop);
    }
    // If "unreasonable focus policy" and active_client is on_all_desktops and
    // under mouse (Hence == old_active_client), conserve focus.
    // (Thanks to Volker Schatz <V.Schatz at thphys.uni-heidelberg.de>)
    else if (active_client && active_client->isShown() && active_client->isOnCurrentDesktop())
        c = active_client;

    if (!c) {
        c = win::find_desktop(this, true, desktop);
    }

    if (c != active_client) {
        setActiveClient(nullptr);
    }

    if (c) {
        request_focus(c);
    } else if (auto desktop_client = win::find_desktop(this, true, desktop)) {
        request_focus(desktop_client);
    } else {
        focusToNull();
    }
}

Toplevel* Workspace::findClientToActivateOnDesktop(uint desktop)
{
    if (movingClient != nullptr && active_client == movingClient
        && win::focus_chain::self()->contains(active_client, desktop) && active_client->isShown()
        && active_client->isOnCurrentDesktop()) {
        // A requestFocus call will fail, as the client is already active
        return active_client;
    }
    // from actiavtion.cpp
    if (options->isNextFocusPrefersMouse()) {
        auto it = stacking_order->sorted().cend();
        while (it != stacking_order->sorted().cbegin()) {
            auto client = qobject_cast<win::x11::window*>(*(--it));
            if (!client) {
                continue;
            }

            if (!(client->isShown() && client->isOnDesktop(desktop)
                  && win::on_active_screen(client)))
                continue;

            if (client->frameGeometry().contains(input::get_cursor()->pos())) {
                if (!win::is_desktop(client))
                    return client;
                break; // unconditional break  - we do not pass the focus to some client below an
                       // unusable one
            }
        }
    }
    return win::focus_chain::self()->getForActivation(desktop);
}

void Workspace::slotDesktopCountChanged(uint previousCount, uint newCount)
{
    Q_UNUSED(previousCount)
    resetClientAreas(newCount);
}

void Workspace::resetClientAreas(uint desktopCount)
{
    // Make it +1, so that it can be accessed as [1..numberofdesktops]
    areas.work.clear();
    areas.work.resize(desktopCount + 1);
    areas.restrictedmove.clear();
    areas.restrictedmove.resize(desktopCount + 1);
    areas.screen.clear();

    updateClientArea(true);
}

void Workspace::selectWmInputEventMask()
{
    uint32_t presentMask = 0;
    Xcb::WindowAttributes attr(rootWindow());
    if (!attr.isNull()) {
        presentMask = attr->your_event_mask;
    }

    Xcb::selectInput(rootWindow(),
                     presentMask | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_PROPERTY_CHANGE
                         | XCB_EVENT_MASK_COLOR_MAP_CHANGE | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
                         | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE
                         | // For NotifyDetailNone
                         XCB_EVENT_MASK_EXPOSURE);
}

/**
 * Sends client \a c to desktop \a desk.
 *
 * Takes care of transients as well.
 */
void Workspace::sendClientToDesktop(Toplevel* window, int desk, bool dont_activate)
{
    if ((desk < 1 && desk != NET::OnAllDesktops)
        || desk > static_cast<int>(VirtualDesktopManager::self()->count())) {
        return;
    }
    auto old_desktop = window->desktop();
    auto was_on_desktop = window->isOnDesktop(desk) || window->isOnAllDesktops();
    win::set_desktop(window, desk);
    if (window->desktop() != desk) {
        // No change or desktop forced
        return;
    }
    desk = window->desktop(); // Client did range checking

    if (window->isOnDesktop(VirtualDesktopManager::self()->current())) {
        if (win::wants_tab_focus(window) && options->focusPolicyIsReasonable() && !was_on_desktop
            && // for stickyness changes
            !dont_activate) {
            request_focus(window);
        } else {
            win::restack_client_under_active(this, window);
        }
    } else
        win::raise_window(this, window);

    win::check_workspace_position(window, QRect(), old_desktop);

    auto transients_stacking_order
        = win::restacked_by_space_stacking_order(this, window->transient()->children);
    for (auto const& transient : transients_stacking_order) {
        if (transient->control) {
            sendClientToDesktop(transient, desk, dont_activate);
        }
    }
    updateClientArea();
}

void Workspace::sendClientToScreen(Toplevel* window, int screen)
{
    win::send_to_screen(window, screen);
}

/**
 * Delayed focus functions
 */
void Workspace::delayFocus()
{
    request_focus(delayfocus_client);
    cancelDelayFocus();
}

void Workspace::requestDelayFocus(Toplevel* c)
{
    delayfocus_client = c;
    delete delayFocusTimer;
    delayFocusTimer = new QTimer(this);
    connect(delayFocusTimer, &QTimer::timeout, this, &Workspace::delayFocus);
    delayFocusTimer->setSingleShot(true);
    delayFocusTimer->start(options->delayFocusInterval());
}

void Workspace::cancelDelayFocus()
{
    delete delayFocusTimer;
    delayFocusTimer = nullptr;
}

bool Workspace::checkStartupNotification(xcb_window_t w, KStartupInfoId& id, KStartupInfoData& data)
{
    return startup->checkStartup(w, id, data) == KStartupInfo::Match;
}

/**
 * Puts the focus on a dummy window
 * Just using XSetInputFocus() with None would block keyboard input
 */
void Workspace::focusToNull()
{
    if (m_nullFocus) {
        m_nullFocus->focus();
    }
}

void Workspace::setShowingDesktop(bool showing)
{
    const bool changed = showing != showing_desktop;
    if (win::x11::rootInfo() && changed) {
        win::x11::rootInfo()->setShowingDesktop(showing);
    }
    showing_desktop = showing;

    Toplevel* topDesk = nullptr;

    { // for the blocker RAII
        Blocker blocker(
            stacking_order); // updateLayer & lowerClient would invalidate stacking_order
        for (int i = static_cast<int>(stacking_order->sorted().size()) - 1; i > -1; --i) {
            auto c = qobject_cast<Toplevel*>(stacking_order->sorted().at(i));
            if (c && c->isOnCurrentDesktop()) {
                if (win::is_dock(c)) {
                    win::update_layer(c);
                } else if (win::is_desktop(c) && c->isShown()) {
                    win::update_layer(c);
                    win::lower_window(this, c);
                    if (!topDesk)
                        topDesk = c;
                    if (auto group = c->group()) {
                        for (auto cm : group->members()) {
                            win::update_layer(cm);
                        }
                    }
                }
            }
        }
    } // ~Blocker

    if (showing_desktop && topDesk) {
        request_focus(topDesk);
    } else if (!showing_desktop && changed) {
        const auto client
            = win::focus_chain::self()->getForActivation(VirtualDesktopManager::self()->current());
        if (client) {
            activateClient(client);
        }
    }
    if (changed)
        Q_EMIT showingDesktopChanged(showing);
}

void Workspace::disableGlobalShortcutsForClient(bool disable)
{
    if (global_shortcuts_disabled_for_client == disable)
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                          QStringLiteral("/kglobalaccel"),
                                                          QStringLiteral("org.kde.KGlobalAccel"),
                                                          QStringLiteral("blockGlobalShortcuts"));
    message.setArguments(QList<QVariant>() << disable);
    QDBusConnection::sessionBus().asyncCall(message);

    global_shortcuts_disabled_for_client = disable;
    // Update also Meta+LMB actions etc.
    for (auto& client : allClientList()) {
        client->control->update_mouse_grab();
    }
}

QString Workspace::supportInformation() const
{
    QString support;
    const QString yes = QStringLiteral("yes\n");
    const QString no = QStringLiteral("no\n");

    support.append(ki18nc("Introductory text shown in the support information.",
                          "KWin Support Information:\n"
                          "The following information should be used when requesting support on "
                          "e.g. https://forum.kde.org.\n"
                          "It provides information about the currently running instance, which "
                          "options are used,\n"
                          "what OpenGL driver and which effects are running.\n"
                          "Please post the information provided underneath this introductory text "
                          "to a paste bin service\n"
                          "like https://paste.kde.org instead of pasting into support threads.\n")
                       .toString());
    support.append(QStringLiteral("\n==========================\n\n"));
    // all following strings are intended for support. They need to be pasted to e.g forums.kde.org
    // it is expected that the support will happen in English language or that the people providing
    // help understand English. Because of that all texts are not translated
    support.append(QStringLiteral("Version\n"));
    support.append(QStringLiteral("=======\n"));
    support.append(QStringLiteral("KWin version: "));
    support.append(QStringLiteral(KWIN_VERSION_STRING));
    support.append(QStringLiteral("\n"));
    support.append(QStringLiteral("Qt Version: "));
    support.append(QString::fromUtf8(qVersion()));
    support.append(QStringLiteral("\n"));
    support.append(QStringLiteral("Qt compile version: %1\n").arg(QStringLiteral(QT_VERSION_STR)));
    support.append(
        QStringLiteral("XCB compile version: %1\n\n").arg(QStringLiteral(XCB_VERSION_STRING)));
    support.append(QStringLiteral("Operation Mode: "));
    switch (kwinApp()->operationMode()) {
    case Application::OperationModeX11:
        support.append(QStringLiteral("X11 only"));
        break;
    case Application::OperationModeWaylandOnly:
        support.append(QStringLiteral("Wayland Only"));
        break;
    case Application::OperationModeXwayland:
        support.append(QStringLiteral("Xwayland"));
        break;
    }
    support.append(QStringLiteral("\n\n"));

    support.append(QStringLiteral("Build Options\n"));
    support.append(QStringLiteral("=============\n"));

    support.append(QStringLiteral("KWIN_BUILD_DECORATIONS: "));
#ifdef KWIN_BUILD_DECORATIONS
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("KWIN_BUILD_TABBOX: "));
#ifdef KWIN_BUILD_TABBOX
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("KWIN_BUILD_ACTIVITIES (deprecated): "));
    support.append(no);
    support.append(QStringLiteral("HAVE_PERF: "));
#if HAVE_PERF
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("HAVE_EPOXY_GLX: "));
#if HAVE_EPOXY_GLX
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("\n"));

    if (auto c = kwinApp()->x11Connection()) {
        support.append(QStringLiteral("X11\n"));
        support.append(QStringLiteral("===\n"));
        auto x11setup = xcb_get_setup(c);
        support.append(QStringLiteral("Vendor: %1\n")
                           .arg(QString::fromUtf8(QByteArray::fromRawData(
                               xcb_setup_vendor(x11setup), xcb_setup_vendor_length(x11setup)))));
        support.append(QStringLiteral("Vendor Release: %1\n").arg(x11setup->release_number));
        support.append(QStringLiteral("Protocol Version/Revision: %1/%2\n")
                           .arg(x11setup->protocol_major_version)
                           .arg(x11setup->protocol_minor_version));
        const auto extensions = Xcb::Extensions::self()->extensions();
        for (const auto& e : extensions) {
            support.append(QStringLiteral("%1: %2; Version: 0x%3\n")
                               .arg(QString::fromUtf8(e.name))
                               .arg(e.present ? yes.trimmed() : no.trimmed())
                               .arg(QString::number(e.version, 16)));
        }
        support.append(QStringLiteral("\n"));
    }

    if (auto bridge = Decoration::DecorationBridge::self()) {
        support.append(QStringLiteral("Decoration\n"));
        support.append(QStringLiteral("==========\n"));
        support.append(bridge->supportInformation());
        support.append(QStringLiteral("\n"));
    }
    support.append(QStringLiteral("Platform\n"));
    support.append(QStringLiteral("==========\n"));
    support.append(kwinApp()->platform->supportInformation());
    support.append(QStringLiteral("\n"));

    support.append(QStringLiteral("Options\n"));
    support.append(QStringLiteral("=======\n"));
    const QMetaObject* metaOptions = options->metaObject();
    auto printProperty = [](const QVariant& variant) {
        if (variant.type() == QVariant::Size) {
            const QSize& s = variant.toSize();
            return QStringLiteral("%1x%2")
                .arg(QString::number(s.width()))
                .arg(QString::number(s.height()));
        }
        if (QLatin1String(variant.typeName()) == QLatin1String("KWin::OpenGLPlatformInterface")
            || QLatin1String(variant.typeName())
                == QLatin1String("KWin::Options::WindowOperation")) {
            return QString::number(variant.toInt());
        }
        return variant.toString();
    };
    for (int i = 0; i < metaOptions->propertyCount(); ++i) {
        const QMetaProperty property = metaOptions->property(i);
        if (QLatin1String(property.name()) == QLatin1String("objectName")) {
            continue;
        }
        support.append(QStringLiteral("%1: %2\n")
                           .arg(property.name())
                           .arg(printProperty(options->property(property.name()))));
    }
    support.append(QStringLiteral("\nScreen Edges\n"));
    support.append(QStringLiteral("============\n"));
    const QMetaObject* metaScreenEdges = ScreenEdges::self()->metaObject();
    for (int i = 0; i < metaScreenEdges->propertyCount(); ++i) {
        const QMetaProperty property = metaScreenEdges->property(i);
        if (QLatin1String(property.name()) == QLatin1String("objectName")) {
            continue;
        }
        support.append(QStringLiteral("%1: %2\n")
                           .arg(property.name())
                           .arg(printProperty(ScreenEdges::self()->property(property.name()))));
    }
    support.append(QStringLiteral("\nScreens\n"));
    support.append(QStringLiteral("=======\n"));
    support.append(QStringLiteral("Multi-Head: "));
    support.append(QStringLiteral("not supported anymore\n"));
    support.append(QStringLiteral("Active screen follows mouse: "));
    if (screens()->isCurrentFollowsMouse())
        support.append(QStringLiteral(" yes\n"));
    else
        support.append(QStringLiteral(" no\n"));
    support.append(QStringLiteral("Number of Screens: %1\n\n").arg(screens()->count()));
    for (int i = 0; i < screens()->count(); ++i) {
        const QRect geo = screens()->geometry(i);
        support.append(QStringLiteral("Screen %1:\n").arg(i));
        support.append(QStringLiteral("---------\n"));
        support.append(QStringLiteral("Name: %1\n").arg(screens()->name(i)));
        support.append(QStringLiteral("Geometry: %1,%2,%3x%4\n")
                           .arg(geo.x())
                           .arg(geo.y())
                           .arg(geo.width())
                           .arg(geo.height()));
        support.append(QStringLiteral("Scale: %1\n").arg(screens()->scale(i)));
        support.append(QStringLiteral("Refresh Rate: %1\n\n").arg(screens()->refreshRate(i)));
    }
    support.append(QStringLiteral("\nCompositing\n"));
    support.append(QStringLiteral("===========\n"));
    if (effects) {
        support.append(QStringLiteral("Compositing is active\n"));
        switch (effects->compositingType()) {
        case OpenGLCompositing: {
            GLPlatform* platform = GLPlatform::instance();
            if (platform->isGLES()) {
                support.append(QStringLiteral("Compositing Type: OpenGL ES 2.0\n"));
            } else {
                support.append(QStringLiteral("Compositing Type: OpenGL\n"));
            }
            support.append(QStringLiteral("OpenGL vendor string: ")
                           + QString::fromUtf8(platform->glVendorString()) + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL renderer string: ")
                           + QString::fromUtf8(platform->glRendererString())
                           + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL version string: ")
                           + QString::fromUtf8(platform->glVersionString()) + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL platform interface: "));
            switch (platform->platformInterface()) {
            case GlxPlatformInterface:
                support.append(QStringLiteral("GLX"));
                break;
            case EglPlatformInterface:
                support.append(QStringLiteral("EGL"));
                break;
            default:
                support.append(QStringLiteral("UNKNOWN"));
            }
            support.append(QStringLiteral("\n"));

            if (platform->supports(LimitedGLSL) || platform->supports(GLSL))
                support.append(QStringLiteral("OpenGL shading language version string: ")
                               + QString::fromUtf8(platform->glShadingLanguageVersionString())
                               + QStringLiteral("\n"));

            support.append(QStringLiteral("Driver: ")
                           + GLPlatform::driverToString(platform->driver()) + QStringLiteral("\n"));
            if (!platform->isMesaDriver())
                support.append(QStringLiteral("Driver version: ")
                               + GLPlatform::versionToString(platform->driverVersion())
                               + QStringLiteral("\n"));

            support.append(QStringLiteral("GPU class: ")
                           + GLPlatform::chipClassToString(platform->chipClass())
                           + QStringLiteral("\n"));

            support.append(QStringLiteral("OpenGL version: ")
                           + GLPlatform::versionToString(platform->glVersion())
                           + QStringLiteral("\n"));

            if (platform->supports(LimitedGLSL) || platform->supports(GLSL))
                support.append(QStringLiteral("GLSL version: ")
                               + GLPlatform::versionToString(platform->glslVersion())
                               + QStringLiteral("\n"));

            if (platform->isMesaDriver())
                support.append(QStringLiteral("Mesa version: ")
                               + GLPlatform::versionToString(platform->mesaVersion())
                               + QStringLiteral("\n"));
            if (platform->serverVersion() > 0)
                support.append(QStringLiteral("X server version: ")
                               + GLPlatform::versionToString(platform->serverVersion())
                               + QStringLiteral("\n"));
            if (platform->kernelVersion() > 0)
                support.append(QStringLiteral("Linux kernel version: ")
                               + GLPlatform::versionToString(platform->kernelVersion())
                               + QStringLiteral("\n"));

            support.append(QStringLiteral("Direct rendering: "));
            support.append(QStringLiteral("Requires strict binding: "));
            if (!platform->isLooseBinding()) {
                support.append(QStringLiteral("yes\n"));
            } else {
                support.append(QStringLiteral("no\n"));
            }
            support.append(QStringLiteral("GLSL shaders: "));
            if (platform->supports(GLSL)) {
                if (platform->supports(LimitedGLSL)) {
                    support.append(QStringLiteral(" limited\n"));
                } else {
                    support.append(QStringLiteral(" yes\n"));
                }
            } else {
                support.append(QStringLiteral(" no\n"));
            }
            support.append(QStringLiteral("Texture NPOT support: "));
            if (platform->supports(TextureNPOT)) {
                if (platform->supports(LimitedNPOT)) {
                    support.append(QStringLiteral(" limited\n"));
                } else {
                    support.append(QStringLiteral(" yes\n"));
                }
            } else {
                support.append(QStringLiteral(" no\n"));
            }
            support.append(QStringLiteral("Virtual Machine: "));
            if (platform->isVirtualMachine()) {
                support.append(QStringLiteral(" yes\n"));
            } else {
                support.append(QStringLiteral(" no\n"));
            }
            support.append(QStringLiteral("Timer query support: "));
            if (platform->supports(GLFeature::TimerQuery)) {
                support.append(QStringLiteral("yes\n"));
            } else {
                support.append(QStringLiteral("no\n"));
            }

            support.append(QStringLiteral("OpenGL 2 Shaders are used\n"));
            break;
        }
        case XRenderCompositing:
            support.append(QStringLiteral("Compositing Type: XRender\n"));
            break;
        case QPainterCompositing:
            support.append("Compositing Type: QPainter\n");
            break;
        case NoCompositing:
        default:
            support.append(
                QStringLiteral("Something is really broken, neither OpenGL nor XRender is used"));
        }
        support.append(QStringLiteral("\nLoaded Effects:\n"));
        support.append(QStringLiteral("---------------\n"));
        auto const& loaded_effects
            = static_cast<render::effects_handler_impl*>(effects)->loadedEffects();
        for (auto const& effect : qAsConst(loaded_effects)) {
            support.append(effect + QStringLiteral("\n"));
        }
        support.append(QStringLiteral("\nCurrently Active Effects:\n"));
        support.append(QStringLiteral("-------------------------\n"));
        auto const& active_effects
            = static_cast<render::effects_handler_impl*>(effects)->activeEffects();
        for (auto const& effect : qAsConst(active_effects)) {
            support.append(effect + QStringLiteral("\n"));
        }
        support.append(QStringLiteral("\nEffect Settings:\n"));
        support.append(QStringLiteral("----------------\n"));
        for (auto const& effect : qAsConst(loaded_effects)) {
            support.append(
                static_cast<render::effects_handler_impl*>(effects)->supportInformation(effect));
            support.append(QStringLiteral("\n"));
        }
    } else {
        support.append(QStringLiteral("Compositing is not active\n"));
    }
    return support;
}

Toplevel* Workspace::findAbstractClient(std::function<bool(const Toplevel*)> func) const
{
    if (auto ret = win::find_in_list(m_allClients, func)) {
        return ret;
    }
    return nullptr;
}

win::x11::window* Workspace::findUnmanaged(xcb_window_t w) const
{
    return static_cast<win::x11::window*>(findToplevel(
        [w](auto toplevel) { return !toplevel->control && toplevel->xcb_window() == w; }));
}

win::x11::window* Workspace::findClient(win::x11::predicate_match predicate, xcb_window_t w) const
{
    switch (predicate) {
    case win::x11::predicate_match::window:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_window() == w;
        }));
    case win::x11::predicate_match::wrapper_id:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_windows.wrapper == w;
        }));
    case win::x11::predicate_match::frame_id:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_windows.outer == w;
        }));
    case win::x11::predicate_match::input_id:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_windows.input == w;
        }));
    }
    return nullptr;
}

Toplevel* Workspace::findToplevel(std::function<bool(const Toplevel*)> func) const
{
    auto const it = std::find_if(m_windows.cbegin(), m_windows.cend(), [&func](auto const& win) {
        return !win->remnant() && func(win);
    });
    return it != m_windows.cend() ? *it : nullptr;
}

void Workspace::forEachToplevel(std::function<void(Toplevel*)> func)
{
    std::for_each(m_windows.cbegin(), m_windows.cend(), func);
}

bool Workspace::hasClient(Toplevel const* window)
{
    if (auto cc = dynamic_cast<win::x11::window const*>(window)) {
        return hasClient(cc);
    } else {
        return findAbstractClient([window](Toplevel const* test) { return test == window; })
            != nullptr;
    }
    return false;
}

void Workspace::forEachAbstractClient(std::function<void(Toplevel*)> func)
{
    std::for_each(m_allClients.cbegin(), m_allClients.cend(), func);
}

Toplevel* Workspace::findInternal(QWindow* w) const
{
    if (!w) {
        return nullptr;
    }
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        return findUnmanaged(w->winId());
    }
    for (auto client : m_allClients) {
        if (auto internal = qobject_cast<win::internal_window*>(client)) {
            if (internal->internalWindow() == w) {
                return internal;
            }
        }
    }
    return nullptr;
}

bool Workspace::compositing() const
{
    return m_compositor && m_compositor->scene();
}

void Workspace::setWasUserInteraction()
{
    if (was_user_interaction) {
        return;
    }
    was_user_interaction = true;
    // might be called from within the filter, so delay till we now the filter returned
    QTimer::singleShot(0, this, [this] { m_wasUserInteractionFilter.reset(); });
}

void Workspace::updateTabbox()
{
#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox* tabBox = TabBox::TabBox::self();
    if (tabBox->isDisplayed()) {
        tabBox->reset(true);
    }
#endif
}

void Workspace::addInternalClient(win::internal_window* client)
{
    m_windows.push_back(client);
    m_allClients.push_back(client);

    win::setup_space_window_connections(this, client);
    win::update_layer(client);

    if (client->placeable()) {
        auto const area = clientArea(PlacementArea, screens()->current(), client->desktop());
        win::place(client, area);
    }

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);
    updateClientArea();

    Q_EMIT internalClientAdded(client);
}

void Workspace::removeInternalClient(win::internal_window* client)
{
    remove_all(m_allClients, client);
    remove_all(m_windows, client);

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);
    updateClientArea();

    Q_EMIT internalClientRemoved(client);
}

void Workspace::remove_window(Toplevel* window)
{
    remove_all(m_windows, window);
    remove_all(stacking_order->pre_stack, window);
    remove_all(stacking_order->win_stack, window);

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);
}

QRect Workspace::get_icon_geometry(Toplevel const* /*win*/) const
{
    return QRect();
}

win::x11::group* Workspace::findGroup(xcb_window_t leader) const
{
    Q_ASSERT(leader != XCB_WINDOW_NONE);
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        if ((*it)->leader() == leader)
            return *it;
    return nullptr;
}

void Workspace::updateMinimizedOfTransients(Toplevel* c)
{
    // if mainwindow is minimized or shaded, minimize transients too
    auto const transients = c->transient()->children;

    if (c->control->minimized()) {
        for (auto it = transients.cbegin(); it != transients.cend(); ++it) {
            auto abstract_client = *it;
            if (abstract_client->transient()->modal())
                continue; // there's no reason to hide modal dialogs with the main client
            if (!(*it)->control) {
                continue;
            }
            // but to keep them to eg. watch progress or whatever
            if (!(*it)->control->minimized()) {
                win::set_minimized(abstract_client, true);
                updateMinimizedOfTransients(abstract_client);
            }
        }
        if (c->transient()
                ->modal()) { // if a modal dialog is minimized, minimize its mainwindow too
            for (auto c2 : c->transient()->leads()) {
                win::set_minimized(c2, true);
            }
        }
    } else {
        // else unmiminize the transients
        for (auto it = transients.cbegin(); it != transients.cend(); ++it) {
            auto abstract_client = *it;
            if (!(*it)->control) {
                continue;
            }
            if ((*it)->control->minimized()) {
                win::set_minimized(abstract_client, false);
                updateMinimizedOfTransients(abstract_client);
            }
        }
        if (c->transient()->modal()) {
            for (auto c2 : c->transient()->leads()) {
                win::set_minimized(c2, false);
            }
        }
    }
}

/**
 * Sets the client \a c's transient windows' on_all_desktops property to \a on_all_desktops.
 */
void Workspace::updateOnAllDesktopsOfTransients(Toplevel* window)
{
    auto const transients = window->transient()->children;
    for (auto const& transient : transients) {
        if (transient->isOnAllDesktops() != window->isOnAllDesktops()) {
            win::set_on_all_desktops(transient, window->isOnAllDesktops());
        }
    }
}

// A new window has been mapped. Check if it's not a mainwindow for some already existing transient
// window.
void Workspace::checkTransients(Toplevel* window)
{
    std::for_each(m_windows.cbegin(), m_windows.cend(), [&window](auto const& client) {
        client->checkTransient(window);
    });
}

/**
 * Resizes the workspace after an XRANDR screen size change
 */
void Workspace::desktopResized()
{
    QRect geom = screens()->geometry();
    if (win::x11::rootInfo()) {
        NETSize desktop_geometry;
        desktop_geometry.width = geom.width();
        desktop_geometry.height = geom.height();
        win::x11::rootInfo()->setDesktopGeometry(desktop_geometry);
    }

    updateClientArea();
    saveOldScreenSizes(); // after updateClientArea(), so that one still uses the previous one

    // TODO: emit a signal instead and remove the deep function calls into edges and effects
    ScreenEdges::self()->recreateEdges();

    if (effects) {
        static_cast<render::effects_handler_impl*>(effects)->desktopResized(geom.size());
    }
}

void Workspace::saveOldScreenSizes()
{
    olddisplaysize = screens()->displaySize();
    oldscreensizes.clear();
    for (int i = 0; i < screens()->count(); ++i)
        oldscreensizes.push_back(screens()->geometry(i));
}

/**
 * Updates the current client areas according to the current clients.
 *
 * If the area changes or force is @c true, the new areas are propagated to the world.
 *
 * The client area is the area that is available for clients (that
 * which is not taken by windows like panels, the top-of-screen menu
 * etc).
 *
 * @see clientArea()
 */
void Workspace::updateClientArea(bool force)
{
    auto const screens = Screens::self();
    auto const screens_count = screens->count();
    auto const desktops_count = static_cast<int>(VirtualDesktopManager::self()->count());

    // To be determined are new:
    // * work areas,
    // * restricted-move areas,
    // * screen areas.
    win::space_areas new_areas(desktops_count + 1);

    std::vector<QRect> screens_geos(screens_count);
    QRect desktop_area;

    for (auto screen = 0; screen < screens_count; screen++) {
        desktop_area |= screens->geometry(screen);
    }

    for (auto screen = 0; screen < screens_count; screen++) {
        screens_geos[screen] = screens->geometry(screen);
    }

    for (auto desktop = 1; desktop <= desktops_count; ++desktop) {
        new_areas.work[desktop] = desktop_area;
        new_areas.screen[desktop].resize(screens_count);
        for (int screen = 0; screen < screens_count; screen++) {
            new_areas.screen[desktop][screen] = screens_geos[screen];
        }
    }

    update_space_area_from_windows(desktop_area, screens_geos, new_areas);

    auto changed = force || areas.screen.empty();

    for (int desktop = 1; !changed && desktop <= desktops_count; ++desktop) {
        changed |= areas.work[desktop] != new_areas.work[desktop];
        changed |= areas.restrictedmove[desktop] != new_areas.restrictedmove[desktop];
        changed |= areas.screen[desktop].size() != new_areas.screen[desktop].size();

        for (int screen = 0; !changed && screen < screens_count; screen++) {
            changed |= new_areas.screen[desktop][screen] != areas.screen[desktop][screen];
        }
    }

    if (changed) {
        oldrestrictedmovearea = areas.restrictedmove;
        areas = new_areas;

        if (win::x11::rootInfo()) {
            NETRect rect;
            for (int desktop = 1; desktop <= desktops_count; desktop++) {
                rect.pos.x = areas.work[desktop].x();
                rect.pos.y = areas.work[desktop].y();
                rect.size.width = areas.work[desktop].width();
                rect.size.height = areas.work[desktop].height();
                win::x11::rootInfo()->setWorkArea(desktop, rect);
            }
        }

        for (auto win : m_allClients) {
            win::check_workspace_position(win);
        }

        // Reset, no longer valid or needed.
        oldrestrictedmovearea.clear();
    }
}

void Workspace::update_space_area_from_windows(QRect const& /*desktop_area*/,
                                               std::vector<QRect> const& /*screens_geos*/,
                                               win::space_areas& /*areas*/)
{
    // Can't be pure virtual because the function might be called from the ctor.
}

void Workspace::updateClientArea()
{
    updateClientArea(false);
}

/**
 * Returns the area available for clients. This is the desktop
 * geometry minus windows on the dock. Placement algorithms should
 * refer to this rather than Screens::geometry.
 */
QRect Workspace::clientArea(clientAreaOption opt, int screen, int desktop) const
{
    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0)
        desktop = VirtualDesktopManager::self()->current();
    if (screen == -1) {
        screen = screens()->current();
    }
    const QSize displaySize = screens()->displaySize();

    QRect sarea, warea;
    sarea = (!areas.screen.empty()
             // screens may be missing during KWin initialization or screen config changes
             && screen < static_cast<int>(areas.screen[desktop].size()))
        ? areas.screen[desktop][screen]
        : screens()->geometry(screen);
    warea = areas.work[desktop].isNull() ? QRect(0, 0, displaySize.width(), displaySize.height())
                                         : areas.work[desktop];

    switch (opt) {
    case MaximizeArea:
    case PlacementArea:
        return sarea;
    case MaximizeFullArea:
    case FullScreenArea:
    case MovementArea:
    case ScreenArea:
        return screens()->geometry(screen);
    case WorkArea:
        return warea;
    case FullArea:
        return QRect(0, 0, displaySize.width(), displaySize.height());
    }
    abort();
}

QRect Workspace::clientArea(clientAreaOption opt, const QPoint& p, int desktop) const
{
    return clientArea(opt, screens()->number(p), desktop);
}

QRect Workspace::clientArea(clientAreaOption opt, Toplevel const* window) const
{
    return clientArea(opt, win::pending_frame_geometry(window).center(), window->desktop());
}

static QRegion strutsToRegion(int desktop, StrutAreas areas, std::vector<StrutRects> const& struts)
{
    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0)
        desktop = VirtualDesktopManager::self()->current();
    QRegion region;
    const StrutRects& rects = struts[desktop];
    for (const StrutRect& rect : rects) {
        if (areas & rect.area()) {
            region += rect;
        }
    }
    return region;
}

QRegion Workspace::restrictedMoveArea(int desktop, StrutAreas areas) const
{
    return strutsToRegion(desktop, areas, this->areas.restrictedmove);
}

bool Workspace::inUpdateClientArea() const
{
    return !oldrestrictedmovearea.empty();
}

QRegion Workspace::previousRestrictedMoveArea(int desktop, StrutAreas areas) const
{
    return strutsToRegion(desktop, areas, oldrestrictedmovearea);
}

std::vector<QRect> Workspace::previousScreenSizes() const
{
    return oldscreensizes;
}

int Workspace::oldDisplayWidth() const
{
    return olddisplaysize.width();
}

int Workspace::oldDisplayHeight() const
{
    return olddisplaysize.height();
}

/**
 * Client \a c is moved around to position \a pos. This gives the
 * workspace the opportunity to interveniate and to implement
 * snap-to-windows functionality.
 *
 * The parameter \a snapAdjust is a multiplier used to calculate the
 * effective snap zones. When 1.0, it means that the snap zones will be
 * used without change.
 */
QPoint
Workspace::adjustClientPosition(Toplevel* window, QPoint pos, bool unrestricted, double snapAdjust)
{
    QSize borderSnapZone(options->borderSnapZone(), options->borderSnapZone());
    QRect maxRect;
    auto guideMaximized = win::maximize_mode::restore;
    if (window->maximizeMode() != win::maximize_mode::restore) {
        maxRect = clientArea(
            MaximizeArea, pos + QRect(QPoint(), window->size()).center(), window->desktop());
        QRect geo = window->frameGeometry();
        if (flags(window->maximizeMode() & win::maximize_mode::horizontal)
            && (geo.x() == maxRect.left() || geo.right() == maxRect.right())) {
            guideMaximized |= win::maximize_mode::horizontal;
            borderSnapZone.setWidth(qMax(borderSnapZone.width() + 2, maxRect.width() / 16));
        }
        if (flags(window->maximizeMode() & win::maximize_mode::vertical)
            && (geo.y() == maxRect.top() || geo.bottom() == maxRect.bottom())) {
            guideMaximized |= win::maximize_mode::vertical;
            borderSnapZone.setHeight(qMax(borderSnapZone.height() + 2, maxRect.height() / 16));
        }
    }

    if (options->windowSnapZone() || !borderSnapZone.isNull() || options->centerSnapZone()) {

        const bool sOWO = options->isSnapOnlyWhenOverlapping();
        const int screen = screens()->number(pos + QRect(QPoint(), window->size()).center());

        if (maxRect.isNull()) {
            maxRect = clientArea(MovementArea, screen, window->desktop());
        }

        const int xmin = maxRect.left();
        const int xmax = maxRect.right() + 1; // desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom() + 1;

        const int cx(pos.x());
        const int cy(pos.y());
        const int cw(window->size().width());
        const int ch(window->size().height());
        const int rx(cx + cw);
        const int ry(cy + ch); // these don't change

        int nx(cx), ny(cy); // buffers
        int deltaX(xmax);
        int deltaY(ymax); // minimum distance to other clients

        int lx, ly, lrx, lry; // coords and size for the comparison client, l

        // border snap
        const int snapX = borderSnapZone.width() * snapAdjust; // snap trigger
        const int snapY = borderSnapZone.height() * snapAdjust;
        if (snapX || snapY) {
            auto geo = window->frameGeometry();
            auto frameMargins = win::frame_margins(window);

            // snap to titlebar / snap to window borders on inner screen edges
            if (frameMargins.left()
                && (flags(window->maximizeMode() & win::maximize_mode::horizontal)
                    || screens()->intersecting(
                           geo.translated(maxRect.x() - (frameMargins.left() + geo.x()), 0))
                        > 1)) {
                frameMargins.setLeft(0);
            }
            if (frameMargins.right()
                && (flags(window->maximizeMode() & win::maximize_mode::horizontal)
                    || screens()->intersecting(
                           geo.translated(maxRect.right() + frameMargins.right() - geo.right(), 0))
                        > 1)) {
                frameMargins.setRight(0);
            }
            if (frameMargins.top()) {
                frameMargins.setTop(0);
            }
            if (frameMargins.bottom()
                && (flags(window->maximizeMode() & win::maximize_mode::vertical)
                    || screens()->intersecting(geo.translated(
                           0, maxRect.bottom() + frameMargins.bottom() - geo.bottom()))
                        > 1)) {
                frameMargins.setBottom(0);
            }
            if ((sOWO ? (cx < xmin) : true) && (qAbs(xmin - cx) < snapX)) {
                deltaX = xmin - cx;
                nx = xmin - frameMargins.left();
            }
            if ((sOWO ? (rx > xmax) : true) && (qAbs(rx - xmax) < snapX)
                && (qAbs(xmax - rx) < deltaX)) {
                deltaX = rx - xmax;
                nx = xmax - cw + frameMargins.right();
            }

            if ((sOWO ? (cy < ymin) : true) && (qAbs(ymin - cy) < snapY)) {
                deltaY = ymin - cy;
                ny = ymin - frameMargins.top();
            }
            if ((sOWO ? (ry > ymax) : true) && (qAbs(ry - ymax) < snapY)
                && (qAbs(ymax - ry) < deltaY)) {
                deltaY = ry - ymax;
                ny = ymax - ch + frameMargins.bottom();
            }
        }

        // windows snap
        int snap = options->windowSnapZone() * snapAdjust;
        if (snap) {
            for (auto l = m_allClients.cbegin(); l != m_allClients.cend(); ++l) {
                if ((*l) == window)
                    continue;
                if ((*l)->control->minimized())
                    continue; // is minimized
                if (!(*l)->isShown())
                    continue;
                if (!((*l)->isOnDesktop(window->desktop()) || window->isOnDesktop((*l)->desktop())))
                    continue; // wrong virtual desktop
                if (win::is_desktop(*l) || win::is_splash(*l))
                    continue;

                lx = (*l)->pos().x();
                ly = (*l)->pos().y();
                lrx = lx + (*l)->size().width();
                lry = ly + (*l)->size().height();

                if (!flags(guideMaximized & win::maximize_mode::horizontal)
                    && (((cy <= lry) && (cy >= ly)) || ((ry >= ly) && (ry <= lry))
                        || ((cy <= ly) && (ry >= lry)))) {
                    if ((sOWO ? (cx < lrx) : true) && (qAbs(lrx - cx) < snap)
                        && (qAbs(lrx - cx) < deltaX)) {
                        deltaX = qAbs(lrx - cx);
                        nx = lrx;
                    }
                    if ((sOWO ? (rx > lx) : true) && (qAbs(rx - lx) < snap)
                        && (qAbs(rx - lx) < deltaX)) {
                        deltaX = qAbs(rx - lx);
                        nx = lx - cw;
                    }
                }

                if (!flags(guideMaximized & win::maximize_mode::vertical)
                    && (((cx <= lrx) && (cx >= lx)) || ((rx >= lx) && (rx <= lrx))
                        || ((cx <= lx) && (rx >= lrx)))) {
                    if ((sOWO ? (cy < lry) : true) && (qAbs(lry - cy) < snap)
                        && (qAbs(lry - cy) < deltaY)) {
                        deltaY = qAbs(lry - cy);
                        ny = lry;
                    }
                    // if ( (qAbs( ry-ly ) < snap) && (qAbs( ry - ly ) < deltaY ))
                    if ((sOWO ? (ry > ly) : true) && (qAbs(ry - ly) < snap)
                        && (qAbs(ry - ly) < deltaY)) {
                        deltaY = qAbs(ry - ly);
                        ny = ly - ch;
                    }
                }

                // Corner snapping
                if (!flags(guideMaximized & win::maximize_mode::vertical)
                    && (nx == lrx || nx + cw == lx)) {
                    if ((sOWO ? (ry > lry) : true) && (qAbs(lry - ry) < snap)
                        && (qAbs(lry - ry) < deltaY)) {
                        deltaY = qAbs(lry - ry);
                        ny = lry - ch;
                    }
                    if ((sOWO ? (cy < ly) : true) && (qAbs(cy - ly) < snap)
                        && (qAbs(cy - ly) < deltaY)) {
                        deltaY = qAbs(cy - ly);
                        ny = ly;
                    }
                }
                if (!flags(guideMaximized & win::maximize_mode::horizontal)
                    && (ny == lry || ny + ch == ly)) {
                    if ((sOWO ? (rx > lrx) : true) && (qAbs(lrx - rx) < snap)
                        && (qAbs(lrx - rx) < deltaX)) {
                        deltaX = qAbs(lrx - rx);
                        nx = lrx - cw;
                    }
                    if ((sOWO ? (cx < lx) : true) && (qAbs(cx - lx) < snap)
                        && (qAbs(cx - lx) < deltaX)) {
                        deltaX = qAbs(cx - lx);
                        nx = lx;
                    }
                }
            }
        }

        // center snap
        snap = options->centerSnapZone() * snapAdjust; // snap trigger
        if (snap) {
            int diffX = qAbs((xmin + xmax) / 2 - (cx + cw / 2));
            int diffY = qAbs((ymin + ymax) / 2 - (cy + ch / 2));
            if (diffX < snap && diffY < snap && diffX < deltaX && diffY < deltaY) {
                // Snap to center of screen
                nx = (xmin + xmax) / 2 - cw / 2;
                ny = (ymin + ymax) / 2 - ch / 2;
            } else if (options->borderSnapZone()) {
                // Enhance border snap
                if ((nx == xmin || nx == xmax - cw) && diffY < snap && diffY < deltaY) {
                    // Snap to vertical center on screen edge
                    ny = (ymin + ymax) / 2 - ch / 2;
                } else if (((unrestricted ? ny == ymin : ny <= ymin) || ny == ymax - ch)
                           && diffX < snap && diffX < deltaX) {
                    // Snap to horizontal center on screen edge
                    nx = (xmin + xmax) / 2 - cw / 2;
                }
            }
        }

        pos = QPoint(nx, ny);
    }
    return pos;
}

QRect Workspace::adjustClientSize(Toplevel* window, QRect moveResizeGeom, win::position mode)
{
    // adapted from adjustClientPosition on 29May2004
    // this function is called when resizing a window and will modify
    // the new dimensions to snap to other windows/borders if appropriate
    if (options->windowSnapZone() || options->borderSnapZone()) { // || options->centerSnapZone )
        const bool sOWO = options->isSnapOnlyWhenOverlapping();

        auto const maxRect = clientArea(
            MovementArea, QRect(QPoint(0, 0), window->size()).center(), window->desktop());
        const int xmin = maxRect.left();
        const int xmax = maxRect.right(); // desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom();

        const int cx(moveResizeGeom.left());
        const int cy(moveResizeGeom.top());
        const int rx(moveResizeGeom.right());
        const int ry(moveResizeGeom.bottom());

        int newcx(cx), newcy(cy); // buffers
        int newrx(rx), newry(ry);
        int deltaX(xmax);
        int deltaY(ymax); // minimum distance to other clients

        int lx, ly, lrx, lry; // coords and size for the comparison client, l

        // border snap
        int snap = options->borderSnapZone(); // snap trigger
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);

#define SNAP_BORDER_TOP                                                                            \
    if ((sOWO ? (newcy < ymin) : true) && (qAbs(ymin - newcy) < deltaY)) {                         \
        deltaY = qAbs(ymin - newcy);                                                               \
        newcy = ymin;                                                                              \
    }

#define SNAP_BORDER_BOTTOM                                                                         \
    if ((sOWO ? (newry > ymax) : true) && (qAbs(ymax - newry) < deltaY)) {                         \
        deltaY = qAbs(ymax - newcy);                                                               \
        newry = ymax;                                                                              \
    }

#define SNAP_BORDER_LEFT                                                                           \
    if ((sOWO ? (newcx < xmin) : true) && (qAbs(xmin - newcx) < deltaX)) {                         \
        deltaX = qAbs(xmin - newcx);                                                               \
        newcx = xmin;                                                                              \
    }

#define SNAP_BORDER_RIGHT                                                                          \
    if ((sOWO ? (newrx > xmax) : true) && (qAbs(xmax - newrx) < deltaX)) {                         \
        deltaX = qAbs(xmax - newrx);                                                               \
        newrx = xmax;                                                                              \
    }
            switch (mode) {
            case win::position::bottom_right:
                SNAP_BORDER_BOTTOM
                SNAP_BORDER_RIGHT
                break;
            case win::position::right:
                SNAP_BORDER_RIGHT
                break;
            case win::position::bottom:
                SNAP_BORDER_BOTTOM
                break;
            case win::position::top_left:
                SNAP_BORDER_TOP
                SNAP_BORDER_LEFT
                break;
            case win::position::left:
                SNAP_BORDER_LEFT
                break;
            case win::position::top:
                SNAP_BORDER_TOP
                break;
            case win::position::top_right:
                SNAP_BORDER_TOP
                SNAP_BORDER_RIGHT
                break;
            case win::position::bottom_left:
                SNAP_BORDER_BOTTOM
                SNAP_BORDER_LEFT
                break;
            default:
                abort();
                break;
            }
        }

        // windows snap
        snap = options->windowSnapZone();
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);
            for (auto l = m_allClients.cbegin(); l != m_allClients.cend(); ++l) {
                if ((*l)->isOnDesktop(VirtualDesktopManager::self()->current())
                    && !(*l)->control->minimized() && (*l) != window) {
                    lx = (*l)->pos().x() - 1;
                    ly = (*l)->pos().y() - 1;
                    lrx = (*l)->pos().x() + (*l)->size().width();
                    lry = (*l)->pos().y() + (*l)->size().height();

#define WITHIN_HEIGHT                                                                              \
    (((newcy <= lry) && (newcy >= ly)) || ((newry >= ly) && (newry <= lry))                        \
     || ((newcy <= ly) && (newry >= lry)))

#define WITHIN_WIDTH                                                                               \
    (((cx <= lrx) && (cx >= lx)) || ((rx >= lx) && (rx <= lrx)) || ((cx <= lx) && (rx >= lrx)))

#define SNAP_WINDOW_TOP                                                                            \
    if ((sOWO ? (newcy < lry) : true) && WITHIN_WIDTH && (qAbs(lry - newcy) < deltaY)) {           \
        deltaY = qAbs(lry - newcy);                                                                \
        newcy = lry;                                                                               \
    }

#define SNAP_WINDOW_BOTTOM                                                                         \
    if ((sOWO ? (newry > ly) : true) && WITHIN_WIDTH && (qAbs(ly - newry) < deltaY)) {             \
        deltaY = qAbs(ly - newry);                                                                 \
        newry = ly;                                                                                \
    }

#define SNAP_WINDOW_LEFT                                                                           \
    if ((sOWO ? (newcx < lrx) : true) && WITHIN_HEIGHT && (qAbs(lrx - newcx) < deltaX)) {          \
        deltaX = qAbs(lrx - newcx);                                                                \
        newcx = lrx;                                                                               \
    }

#define SNAP_WINDOW_RIGHT                                                                          \
    if ((sOWO ? (newrx > lx) : true) && WITHIN_HEIGHT && (qAbs(lx - newrx) < deltaX)) {            \
        deltaX = qAbs(lx - newrx);                                                                 \
        newrx = lx;                                                                                \
    }

#define SNAP_WINDOW_C_TOP                                                                          \
    if ((sOWO ? (newcy < ly) : true) && (newcx == lrx || newrx == lx)                              \
        && qAbs(ly - newcy) < deltaY) {                                                            \
        deltaY = qAbs(ly - newcy + 1);                                                             \
        newcy = ly + 1;                                                                            \
    }

#define SNAP_WINDOW_C_BOTTOM                                                                       \
    if ((sOWO ? (newry > lry) : true) && (newcx == lrx || newrx == lx)                             \
        && qAbs(lry - newry) < deltaY) {                                                           \
        deltaY = qAbs(lry - newry - 1);                                                            \
        newry = lry - 1;                                                                           \
    }

#define SNAP_WINDOW_C_LEFT                                                                         \
    if ((sOWO ? (newcx < lx) : true) && (newcy == lry || newry == ly)                              \
        && qAbs(lx - newcx) < deltaX) {                                                            \
        deltaX = qAbs(lx - newcx + 1);                                                             \
        newcx = lx + 1;                                                                            \
    }

#define SNAP_WINDOW_C_RIGHT                                                                        \
    if ((sOWO ? (newrx > lrx) : true) && (newcy == lry || newry == ly)                             \
        && qAbs(lrx - newrx) < deltaX) {                                                           \
        deltaX = qAbs(lrx - newrx - 1);                                                            \
        newrx = lrx - 1;                                                                           \
    }

                    switch (mode) {
                    case win::position::bottom_right:
                        SNAP_WINDOW_BOTTOM
                        SNAP_WINDOW_RIGHT
                        SNAP_WINDOW_C_BOTTOM
                        SNAP_WINDOW_C_RIGHT
                        break;
                    case win::position::right:
                        SNAP_WINDOW_RIGHT
                        SNAP_WINDOW_C_RIGHT
                        break;
                    case win::position::bottom:
                        SNAP_WINDOW_BOTTOM
                        SNAP_WINDOW_C_BOTTOM
                        break;
                    case win::position::top_left:
                        SNAP_WINDOW_TOP
                        SNAP_WINDOW_LEFT
                        SNAP_WINDOW_C_TOP
                        SNAP_WINDOW_C_LEFT
                        break;
                    case win::position::left:
                        SNAP_WINDOW_LEFT
                        SNAP_WINDOW_C_LEFT
                        break;
                    case win::position::top:
                        SNAP_WINDOW_TOP
                        SNAP_WINDOW_C_TOP
                        break;
                    case win::position::top_right:
                        SNAP_WINDOW_TOP
                        SNAP_WINDOW_RIGHT
                        SNAP_WINDOW_C_TOP
                        SNAP_WINDOW_C_RIGHT
                        break;
                    case win::position::bottom_left:
                        SNAP_WINDOW_BOTTOM
                        SNAP_WINDOW_LEFT
                        SNAP_WINDOW_C_BOTTOM
                        SNAP_WINDOW_C_LEFT
                        break;
                    default:
                        abort();
                        break;
                    }
                }
            }
        }

        // center snap
        // snap = options->centerSnapZone;
        // if (snap)
        //    {
        //    // Don't resize snap to center as it interferes too much
        //    // There are two ways of implementing this if wanted:
        //    // 1) Snap only to the same points that the move snap does, and
        //    // 2) Snap to the horizontal and vertical center lines of the screen
        //    }

        moveResizeGeom = QRect(QPoint(newcx, newcy), QPoint(newrx, newry));
    }
    return moveResizeGeom;
}

/**
 * Marks the client as being moved or resized by the user.
 */
void Workspace::setMoveResizeClient(Toplevel* window)
{
    Q_ASSERT(!window || !movingClient); // Catch attempts to move a second
    // window while still moving the first one.
    movingClient = window;
    if (movingClient) {
        ++block_focus;
    } else {
        --block_focus;
    }
}

// When kwin crashes, windows will not be gravitated back to their original position
// and will remain offset by the size of the decoration. So when restarting, fix this
// (the property with the size of the frame remains on the window after the crash).
void Workspace::fixPositionAfterCrash(xcb_window_t w, const xcb_get_geometry_reply_t* geometry)
{
    NETWinInfo i(connection(), w, rootWindow(), NET::WMFrameExtents, NET::Properties2());
    NETStrut frame = i.frameExtents();

    if (frame.left != 0 || frame.top != 0) {
        // left and top needed due to narrowing conversations restrictions in C++11
        const uint32_t left = frame.left;
        const uint32_t top = frame.top;
        const uint32_t values[] = {geometry->x - left, geometry->y - top};
        xcb_configure_window(connection(), w, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    }
}

bool Workspace::hasClient(win::x11::window const* c)
{
    auto abstract_c = static_cast<Toplevel const*>(c);
    return findAbstractClient([abstract_c](Toplevel const* test) { return test == abstract_c; });
}

std::vector<Toplevel*> const& Workspace::windows() const
{
    return m_windows;
}

std::vector<Toplevel*> Workspace::unmanagedList() const
{
    std::vector<Toplevel*> ret;
    for (auto const& window : m_windows) {
        if (window->xcb_window() && !window->control && !window->remnant()) {
            ret.push_back(window);
        }
    }
    return ret;
}

std::vector<Toplevel*> Workspace::remnants() const
{
    std::vector<Toplevel*> ret;
    for (auto const& window : m_windows) {
        if (window->remnant()) {
            ret.push_back(window);
        }
    }
    return ret;
}

#ifndef KCMRULES

// ********************
// placement code
// ********************

/**
 * Moves active window left until in bumps into another window or workarea edge.
 */
void Workspace::slotWindowPackLeft()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    win::pack_to(active_client, packPositionLeft(active_client, pos.x(), true), pos.y());
}

void Workspace::slotWindowPackRight()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    auto const width = active_client->geometry_update.frame.size().width();
    win::pack_to(active_client,
                 packPositionRight(active_client, pos.x() + width, true) - width + 1,
                 pos.y());
}

void Workspace::slotWindowPackUp()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    win::pack_to(active_client, pos.x(), packPositionUp(active_client, pos.y(), true));
}

void Workspace::slotWindowPackDown()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    auto const height = active_client->geometry_update.frame.size().height();
    win::pack_to(active_client,
                 pos.x(),
                 packPositionDown(active_client, pos.y() + height, true) - height + 1);
}

void Workspace::slotWindowGrowHorizontal()
{
    if (active_client) {
        win::grow_horizontal(active_client);
    }
}

void Workspace::slotWindowShrinkHorizontal()
{
    if (active_client) {
        win::shrink_horizontal(active_client);
    }
}
void Workspace::slotWindowGrowVertical()
{
    if (active_client) {
        win::grow_vertical(active_client);
    }
}

void Workspace::slotWindowShrinkVertical()
{
    if (active_client) {
        win::shrink_vertical(active_client);
    }
}

void Workspace::quickTileWindow(win::quicktiles mode)
{
    if (!active_client) {
        return;
    }

    // If the user invokes two of these commands in a one second period, try to
    // combine them together to enable easy and intuitive corner tiling
    if (!m_quickTileCombineTimer->isActive()) {
        m_quickTileCombineTimer->start(1000);
        m_lastTilingMode = mode;
    } else {
        auto const was_left_or_right = m_lastTilingMode == win::quicktiles::left
            || m_lastTilingMode == win::quicktiles::right;
        auto const was_top_or_bottom = m_lastTilingMode == win::quicktiles::top
            || m_lastTilingMode == win::quicktiles::bottom;

        auto const is_left_or_right
            = mode == win::quicktiles::left || mode == win::quicktiles::right;
        auto const is_top_or_bottom
            = mode == win::quicktiles::top || mode == win::quicktiles::bottom;

        if ((was_left_or_right && is_top_or_bottom) || (was_top_or_bottom && is_left_or_right)) {
            mode |= m_lastTilingMode;
        }
        m_quickTileCombineTimer->stop();
    }

    win::set_quicktile_mode(active_client, mode, true);
}

int Workspace::packPositionLeft(Toplevel const* window, int oldX, bool leftEdge) const
{
    int newX = clientArea(MaximizeArea, window).left();
    if (oldX <= newX) { // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.left() - 1,
                                 window->geometry_update.frame.center().y()),
                          window->desktop())
                   .left();
    }

    auto const right = newX - win::frame_margins(window).left();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (screens()->intersecting(frameGeometry) < 2) {
        newX = right;
    }

    if (oldX <= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? VirtualDesktopManager::self()->current()
        : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int x = leftEdge ? (*it)->geometry_update.frame.right() + 1
                               : (*it)->geometry_update.frame.left() - 1;
        if (x > newX && x < oldX
            && !(window->geometry_update.frame.top() > (*it)->geometry_update.frame.bottom()
                 || window->geometry_update.frame.bottom() < (*it)->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

int Workspace::packPositionRight(Toplevel const* window, int oldX, bool rightEdge) const
{
    int newX = clientArea(MaximizeArea, window).right();

    if (oldX >= newX) {
        // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.right() + 1,
                                 window->geometry_update.frame.center().y()),
                          window->desktop())
                   .right();
    }

    auto const right = newX + win::frame_margins(window).right();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (screens()->intersecting(frameGeometry) < 2) {
        newX = right;
    }

    if (oldX >= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? VirtualDesktopManager::self()->current()
        : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int x = rightEdge ? (*it)->geometry_update.frame.left() - 1
                                : (*it)->geometry_update.frame.right() + 1;
        if (x < newX && x > oldX
            && !(window->geometry_update.frame.top() > (*it)->geometry_update.frame.bottom()
                 || window->geometry_update.frame.bottom() < (*it)->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

int Workspace::packPositionUp(Toplevel const* window, int oldY, bool topEdge) const
{
    int newY = clientArea(MaximizeArea, window).top();
    if (oldY <= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.center().x(),
                                 window->geometry_update.frame.top() - 1),
                          window->desktop())
                   .top();
    }

    if (oldY <= newY) {
        return oldY;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? VirtualDesktopManager::self()->current()
        : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int y = topEdge ? (*it)->geometry_update.frame.bottom() + 1
                              : (*it)->geometry_update.frame.top() - 1;
        if (y > newY && y < oldY
            && !(window->geometry_update.frame.left()
                     > (*it)->geometry_update.frame.right() // they overlap in X direction
                 || window->geometry_update.frame.right() < (*it)->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

int Workspace::packPositionDown(Toplevel const* window, int oldY, bool bottomEdge) const
{
    int newY = clientArea(MaximizeArea, window).bottom();
    if (oldY >= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.center().x(),
                                 window->geometry_update.frame.bottom() + 1),
                          window->desktop())
                   .bottom();
    }

    auto const bottom = newY + win::frame_margins(window).bottom();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveBottom(bottom);
    if (screens()->intersecting(frameGeometry) < 2) {
        newY = bottom;
    }

    if (oldY >= newY) {
        return oldY;
    }
    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? VirtualDesktopManager::self()->current()
        : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int y = bottomEdge ? (*it)->geometry_update.frame.top() - 1
                                 : (*it)->geometry_update.frame.bottom() + 1;
        if (y < newY && y > oldY
            && !(window->geometry_update.frame.left() > (*it)->geometry_update.frame.right()
                 || window->geometry_update.frame.right() < (*it)->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

#endif

/*
 Prevention of focus stealing:

 KWin tries to prevent unwanted changes of focus, that would result
 from mapping a new window. Also, some nasty applications may try
 to force focus change even in cases when ICCCM 4.2.7 doesn't allow it
 (e.g. they may try to activate their main window because the user
 definitely "needs" to see something happened - misusing
 of QWidget::setActiveWindow() may be such case).

 There are 4 ways how a window may become active:
 - the user changes the active window (e.g. focus follows mouse, clicking
   on some window's titlebar) - the change of focus will
   be done by KWin, so there's nothing to solve in this case
 - the change of active window will be requested using the _NET_ACTIVE_WINDOW
   message (handled in RootInfo::changeActiveWindow()) - such requests
   will be obeyed, because this request is meant mainly for e.g. taskbar
   asking the WM to change the active window as a result of some user action.
   Normal applications should use this request only rarely in special cases.
   See also below the discussion of _NET_ACTIVE_WINDOW_TRANSFER.
 - the change of active window will be done by performing XSetInputFocus()
   on a window that's not currently active. ICCCM 4.2.7 describes when
   the application may perform change of input focus. In order to handle
   misbehaving applications, KWin will try to detect focus changes to
   windows that don't belong to currently active application, and restore
   focus back to the currently active window, instead of activating the window
   that got focus (unfortunately there's no way to FocusChangeRedirect similar
   to e.g. SubstructureRedirect, so there will be short time when the focus
   will be changed). The check itself that's done is
   Workspace::allowClientActivation() (see below).
 - a new window will be mapped - this is the most complicated case. If
   the new window belongs to the currently active application, it may be safely
   mapped on top and activated. The same if there's no active window,
   or the active window is the desktop. These checks are done by
   Workspace::allowClientActivation().
    Following checks need to compare times. One time is the timestamp
   of last user action in the currently active window, the other time is
   the timestamp of the action that originally caused mapping of the new window
   (e.g. when the application was started). If the first time is newer than
   the second one, the window will not be activated, as that indicates
   futher user actions took place after the action leading to this new
   mapped window. This check is done by Workspace::allowClientActivation().
    There are several ways how to get the timestamp of action that caused
   the new mapped window (done in win::x11::window::readUserTimeMapTimestamp()) :
     - the window may have the _NET_WM_USER_TIME property. This way
       the application may either explicitly request that the window is not
       activated (by using 0 timestamp), or the property contains the time
       of last user action in the application.
     - KWin itself tries to detect time of last user action in every window,
       by watching KeyPress and ButtonPress events on windows. This way some
       events may be missed (if they don't propagate to the toplevel window),
       but it's good as a fallback for applications that don't provide
       _NET_WM_USER_TIME, and missing some events may at most lead
       to unwanted focus stealing.
     - the timestamp may come from application startup notification.
       Application startup notification, if it exists for the new mapped window,
       should include time of the user action that caused it.
     - if there's no timestamp available, it's checked whether the new window
       belongs to some already running application - if yes, the timestamp
       will be 0 (i.e. refuse activation)
     - if the window is from session restored window, the timestamp will
       be 0 too, unless this application was the active one at the time
       when the session was saved, in which case the window will be
       activated if there wasn't any user interaction since the time
       KWin was started.
     - as the last resort, the _KDE_NET_USER_CREATION_TIME timestamp
       is used. For every toplevel window that is created (see CreateNotify
       handling), this property is set to the at that time current time.
       Since at this time it's known that the new window doesn't belong
       to any existing application (better said, the application doesn't
       have any other window mapped), it is either the very first window
       of the application, or it is the only window of the application
       that was hidden before. The latter case is handled by removing
       the property from windows before withdrawing them, making
       the timestamp empty for next mapping of the window. In the sooner
       case, the timestamp will be used. This helps in case when
       an application is launched without application startup notification,
       it creates its mainwindow, and starts its initialization (that
       may possibly take long time). The timestamp used will be older
       than any user action done after launching this application.
     - if no timestamp is found at all, the window is activated.
    The check whether two windows belong to the same application (same
   process) is done in win::x11::window::belongToSameApplication(). Not 100% reliable,
   but hopefully 99,99% reliable.

 As a somewhat special case, window activation is always enabled when
 session saving is in progress. When session saving, the session
 manager allows only one application to interact with the user.
 Not allowing window activation in such case would result in e.g. dialogs
 not becoming active, so focus stealing prevention would cause here
 more harm than good.

 Windows that attempted to become active but KWin prevented this will
 be marked as demanding user attention. They'll get
 the _NET_WM_STATE_DEMANDS_ATTENTION state, and the taskbar should mark
 them specially (blink, etc.). The state will be reset when the window
 eventually really becomes active.

 There are two more ways how a window can become obtrusive, window stealing
 focus: By showing above the active window, by either raising itself,
 or by moving itself on the active desktop.
     - KWin will refuse raising non-active window above the active one,
         unless they belong to the same application. Applications shouldn't
         raise their windows anyway (unless the app wants to raise one
         of its windows above another of its windows).
     - KWin activates windows moved to the current desktop (as that seems
         logical from the user's point of view, after sending the window
         there directly from KWin, or e.g. using pager). This means
         applications shouldn't send their windows to another desktop
         (SELI TODO - but what if they do?)

 Special cases I can think of:
    - konqueror reusing, i.e. kfmclient tells running Konqueror instance
        to open new window
        - without focus stealing prevention - no problem
        - with ASN (application startup notification) - ASN is forwarded,
            and because it's newer than the instance's user timestamp,
            it takes precedence
        - without ASN - user timestamp needs to be reset, otherwise it would
            be used, and it's old; moreover this new window mustn't be detected
            as window belonging to already running application, or it wouldn't
            be activated - see win::x11::window::sameAppWindowRoleMatch() for the (rather ugly)
            hack
    - konqueror preloading, i.e. window is created in advance, and kfmclient
        tells this Konqueror instance to show it later
        - without focus stealing prevention - no problem
        - with ASN - ASN is forwarded, and because it's newer than the instance's
            user timestamp, it takes precedence
        - without ASN - user timestamp needs to be reset, otherwise it would
            be used, and it's old; also, creation timestamp is changed to
            the time the instance starts (re-)initializing the window,
            this ensures creation timestamp will still work somewhat even in this case
    - KUniqueApplication - when the window is already visible, and the new instance
        wants it to activate
        - without focus stealing prevention - _NET_ACTIVE_WINDOW - no problem
        - with ASN - ASN is forwarded, and set on the already visible window, KWin
            treats the window as new with that ASN
        - without ASN - _NET_ACTIVE_WINDOW as application request is used,
                and there's no really usable timestamp, only timestamp
                from the time the (new) application instance was started,
                so KWin will activate the window *sigh*
                - the bad thing here is that there's absolutely no chance to recognize
                    the case of starting this KUniqueApp from Konsole (and thus wanting
                    the already visible window to become active) from the case
                    when something started this KUniqueApp without ASN (in which case
                    the already visible window shouldn't become active)
                - the only solution is using ASN for starting applications, at least silent
                    (i.e. without feedback)
    - when one application wants to activate another application's window (e.g. KMail
        activating already running KAddressBook window ?)
        - without focus stealing prevention - _NET_ACTIVE_WINDOW - no problem
        - with ASN - can't be here, it's the KUniqueApp case then
        - without ASN - _NET_ACTIVE_WINDOW as application request should be used,
            KWin will activate the new window depending on the timestamp and
            whether it belongs to the currently active application

 _NET_ACTIVE_WINDOW usage:
 data.l[0]= 1 ->app request
          = 2 ->pager request
          = 0 - backwards compatibility
 data.l[1]= timestamp
*/

/**
 * Informs the workspace about the active client, i.e. the client that
 * has the focus (or None if no client has the focus). This functions
 * is called by the client itself that gets focus. It has no other
 * effect than fixing the focus chain and the return value of
 * activeClient(). And of course, to propagate the active client to the
 * world.
 */
void Workspace::setActiveClient(Toplevel* window)
{
    if (active_client == window)
        return;

    if (active_popup && active_popup_client != window && set_active_client_recursion == 0)
        closeActivePopup();
    if (m_userActionsMenu->hasClient() && !m_userActionsMenu->isMenuClient(window)
        && set_active_client_recursion == 0) {
        m_userActionsMenu->close();
    }

    Blocker blocker(stacking_order);
    ++set_active_client_recursion;
    updateFocusMousePosition(input::get_cursor()->pos());
    if (active_client != nullptr) {
        // note that this may call setActiveClient( NULL ), therefore the recursion counter
        win::set_active(active_client, false);
    }
    active_client = window;

    Q_ASSERT(window == nullptr || window->control->active());

    if (active_client) {
        last_active_client = active_client;
        win::focus_chain::self()->update(active_client, win::focus_chain::MakeFirst);
        win::set_demands_attention(active_client, false);

        // activating a client can cause a non active fullscreen window to loose the ActiveLayer
        // status on > 1 screens
        if (screens()->count() > 1) {
            for (auto it = m_allClients.begin(); it != m_allClients.end(); ++it) {
                if (*it != active_client && (*it)->layer() == win::layer::active
                    && (*it)->screen() == active_client->screen()) {
                    win::update_layer(*it);
                }
            }
        }
    }

    win::update_tool_windows(this, false);
    if (window)
        disableGlobalShortcutsForClient(
            window->control->rules().checkDisableGlobalShortcuts(false));
    else
        disableGlobalShortcutsForClient(false);

    stacking_order->update(); // e.g. fullscreens have different layer when active/not-active

    if (win::x11::rootInfo()) {
        win::x11::rootInfo()->setActiveClient(active_client);
    }

    Q_EMIT clientActivated(active_client);
    --set_active_client_recursion;
}

/**
 * Tries to activate the client \a c. This function performs what you
 * expect when clicking the respective entry in a taskbar: showing and
 * raising the client (this may imply switching to the another virtual
 * desktop) and putting the focus onto it. Once X really gave focus to
 * the client window as requested, the client itself will call
 * setActiveClient() and the operation is complete. This may not happen
 * with certain focus policies, though.
 *
 * @see setActiveClient
 * @see requestFocus
 */
void Workspace::activateClient(Toplevel* window, bool force)
{
    if (window == nullptr) {
        focusToNull();
        setActiveClient(nullptr);
        return;
    }
    win::raise_window(this, window);
    if (!window->isOnCurrentDesktop()) {
        ++block_focus;
        VirtualDesktopManager::self()->setCurrent(window->desktop());
        --block_focus;
    }
    if (window->control->minimized()) {
        win::set_minimized(window, false);
    }

    // ensure the window is really visible - could eg. be a hidden utility window, see bug #348083
    window->hideClient(false);

    // TODO force should perhaps allow this only if the window already contains the mouse
    if (options->focusPolicyIsReasonable() || force) {
        request_focus(window, false, force);
    }

    // Don't update user time for clients that have focus stealing workaround.
    // As they usually belong to the current active window but fail to provide
    // this information, updating their user time would make the user time
    // of the currently active window old, and reject further activation for it.
    // E.g. typing URL in minicli which will show kio_uiserver dialog (with workaround),
    // and then kdesktop shows dialog about SSL certificate.
    // This needs also avoiding user creation time in win::x11::window::readUserTimeMapTimestamp().
    if (auto client = dynamic_cast<win::x11::window*>(window)) {
        // updateUserTime is X11 specific
        win::x11::update_user_time(client);
    }
}

/**
 * Tries to activate the client by asking X for the input focus. This
 * function does not perform any show, raise or desktop switching. See
 * Workspace::activateClient() instead.
 *
 * @see activateClient
 */
void Workspace::request_focus(Toplevel* window, bool raise, bool force_focus)
{
    auto take_focus = focusChangeEnabled() || window == active_client;

    if (!window) {
        focusToNull();
        return;
    }

    if (take_focus) {
        auto modal = window->findModal();
        if (modal && modal->control && modal != window) {
            if (!modal->isOnDesktop(window->desktop())) {
                win::set_desktop(modal, window->desktop());
            }
            if (!modal->isShown() && !modal->control->minimized()) {
                // forced desktop or utility window
                // activating a minimized blocked window will unminimize its modal implicitly
                activateClient(modal);
            }
            // if the click was inside the window (i.e. handled is set),
            // but it has a modal, there's no need to use handled mode, because
            // the modal doesn't get the click anyway
            // raising of the original window needs to be still done
            if (raise) {
                win::raise_window(this, window);
            }
            window = modal;
        }
        cancelDelayFocus();
    }

    if (!force_focus && (win::is_dock(window) || win::is_splash(window))) {
        // toplevel menus and dock windows don't take focus if not forced
        // and don't have a flag that they take focus
        if (!window->dockWantsInput()) {
            take_focus = false;
        }
    }

    if (!window->isShown()) {
        // Shouldn't happen, call activateClient() if needed.
        qCWarning(KWIN_CORE) << "request_focus: not shown";
        return;
    }

    if (take_focus) {
        window->takeFocus();
    }
    if (raise) {
        win::raise_window(this, window);
    }

    if (!win::on_active_screen(window)) {
        screens()->setCurrent(window->screen());
    }
}

/**
 * Informs the workspace that the client \a c has been hidden. If it
 * was the active client (or to-become the active client),
 * the workspace activates another one.
 *
 * @note @p c may already be destroyed.
 */
void Workspace::clientHidden(Toplevel* window)
{
    Q_ASSERT(!window->isShown() || !window->isOnCurrentDesktop());
    activateNextClient(window);
}

Toplevel* Workspace::clientUnderMouse(int screen) const
{
    auto it = stacking_order->sorted().cend();
    while (it != stacking_order->sorted().cbegin()) {
        auto client = *(--it);
        if (!client->control) {
            continue;
        }

        // rule out clients which are not really visible.
        // the screen test is rather superfluous for xrandr & twinview since the geometry would
        // differ -> TODO: might be dropped
        if (!(client->isShown() && client->isOnCurrentDesktop() && win::on_screen(client, screen)))
            continue;

        if (client->frameGeometry().contains(input::get_cursor()->pos())) {
            return client;
        }
    }
    return nullptr;
}

// deactivates 'c' and activates next client
bool Workspace::activateNextClient(Toplevel* window)
{
    // if 'c' is not the active or the to-become active one, do nothing
    if (!(window == active_client
          || (should_get_focus.size() > 0 && window == should_get_focus.back()))) {
        return false;
    }

    closeActivePopup();

    if (window != nullptr) {
        if (window == active_client) {
            setActiveClient(nullptr);
        }
        should_get_focus.erase(
            std::remove(should_get_focus.begin(), should_get_focus.end(), window),
            should_get_focus.end());
    }

    // if blocking focus, move focus to the desktop later if needed
    // in order to avoid flickering
    if (!focusChangeEnabled()) {
        focusToNull();
        return true;
    }

    if (!options->focusPolicyIsReasonable())
        return false;

    Toplevel* get_focus = nullptr;

    const int desktop = VirtualDesktopManager::self()->current();

    if (!get_focus && showingDesktop())
        get_focus = win::find_desktop(this, true, desktop); // to not break the state

    if (!get_focus && options->isNextFocusPrefersMouse()) {
        get_focus = clientUnderMouse(window ? window->screen() : screens()->current());
        if (get_focus && (get_focus == window || win::is_desktop(get_focus))) {
            // should rather not happen, but it cannot get the focus. rest of usability is tested
            // above
            get_focus = nullptr;
        }
    }

    if (!get_focus) { // no suitable window under the mouse -> find sth. else
        // first try to pass the focus to the (former) active clients leader
        if (window && window->transient()->lead()) {
            auto leaders = window->transient()->leads();
            if (leaders.size() == 1
                && win::focus_chain::self()->isUsableFocusCandidate(leaders.at(0), window)) {
                get_focus = leaders.at(0);

                // also raise - we don't know where it came from
                win::raise_window(this, get_focus);
            }
        }
        if (!get_focus) {
            // nope, ask the focus chain for the next candidate
            get_focus = win::focus_chain::self()->nextForDesktop(window, desktop);
        }
    }

    if (get_focus == nullptr) // last chance: focus the desktop
        get_focus = win::find_desktop(this, true, desktop);

    if (get_focus != nullptr) {
        request_focus(get_focus);
    } else {
        focusToNull();
    }

    return true;
}

void Workspace::setCurrentScreen(int new_screen)
{
    if (new_screen < 0 || new_screen >= screens()->count())
        return;
    if (!options->focusPolicyIsReasonable())
        return;
    closeActivePopup();
    const int desktop = VirtualDesktopManager::self()->current();
    auto get_focus = win::focus_chain::self()->getForActivation(desktop, new_screen);
    if (get_focus == nullptr) {
        get_focus = win::find_desktop(this, true, desktop);
    }
    if (get_focus != nullptr && get_focus != mostRecentlyActivatedClient()) {
        request_focus(get_focus);
    }
    screens()->setCurrent(new_screen);
}

void Workspace::gotFocusIn(Toplevel const* window)
{
    if (std::find(should_get_focus.cbegin(), should_get_focus.cend(), const_cast<Toplevel*>(window))
        != should_get_focus.cend()) {
        // remove also all sooner elements that should have got FocusIn,
        // but didn't for some reason (and also won't anymore, because they were sooner)
        while (should_get_focus.front() != window) {
            should_get_focus.pop_front();
        }
        should_get_focus.pop_front(); // remove 'c'
    }
}

void Workspace::setShouldGetFocus(Toplevel* window)
{
    should_get_focus.push_back(window);
    // e.g. fullscreens have different layer when active/not-active
    stacking_order->update();
}

namespace FSP
{
enum Level { None = 0, Low, Medium, High, Extreme };
}

// focus_in -> the window got FocusIn event
// ignore_desktop - call comes from _NET_ACTIVE_WINDOW message, don't refuse just because of window
//     is on a different desktop
bool Workspace::allowClientActivation(Toplevel const* window,
                                      xcb_timestamp_t time,
                                      bool focus_in,
                                      bool ignore_desktop)
{
    // options->focusStealingPreventionLevel :
    // 0 - none    - old KWin behaviour, new windows always get focus
    // 1 - low     - focus stealing prevention is applied normally, when unsure, activation is
    // allowed 2 - normal  - focus stealing prevention is applied normally, when unsure, activation
    // is not allowed,
    //              this is the default
    // 3 - high    - new window gets focus only if it belongs to the active application,
    //              or when no window is currently active
    // 4 - extreme - no window gets focus without user intervention
    if (time == -1U) {
        time = window->userTime();
    }
    auto level = window->control->rules().checkFSP(options->focusStealingPreventionLevel());
    if (sessionManager()->state() == SessionState::Saving && level <= FSP::Medium) { // <= normal
        return true;
    }
    auto ac = mostRecentlyActivatedClient();
    if (focus_in) {
        if (std::find(
                should_get_focus.cbegin(), should_get_focus.cend(), const_cast<Toplevel*>(window))
            != should_get_focus.cend()) {
            // FocusIn was result of KWin's action
            return true;
        }
        // Before getting FocusIn, the active Client already
        // got FocusOut, and therefore got deactivated.
        ac = last_active_client;
    }
    if (time == 0) { // explicitly asked not to get focus
        if (!window->control->rules().checkAcceptFocus(false))
            return false;
    }
    const int protection = ac ? ac->control->rules().checkFPP(2) : 0;

    // stealing is unconditionally allowed (NETWM behavior)
    if (level == FSP::None || protection == FSP::None)
        return true;

    // The active client "grabs" the focus or stealing is generally forbidden
    if (level == FSP::Extreme || protection == FSP::Extreme)
        return false;

    // Desktop switching is only allowed in the "no protection" case
    if (!ignore_desktop && !window->isOnCurrentDesktop())
        return false; // allow only with level == 0

    // No active client, it's ok to pass focus
    // NOTICE that extreme protection needs to be handled before to allow protection on unmanged
    // windows
    if (ac == nullptr || win::is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Activation: No client active, allowing";
        return true; // no active client -> always allow
    }

    // TODO window urgency  -> return true?

    // Unconditionally allow intra-client passing around for lower stealing protections
    // unless the active client has High interest
    if (win::belong_to_same_client(window, ac, win::same_client_check::relaxed_for_active)
        && protection < FSP::High) {
        qCDebug(KWIN_CORE) << "Activation: Belongs to active application";
        return true;
    }

    if (!window->isOnCurrentDesktop()) {
        // we allowed explicit self-activation across virtual desktops
        // inside a client or if no client was active, but not otherwise
        return false;
    }

    // High FPS, not intr-client change. Only allow if the active client has only minor interest
    if (level > FSP::Medium && protection > FSP::Low)
        return false;

    if (time == -1U) { // no time known
        qCDebug(KWIN_CORE) << "Activation: No timestamp at all";
        // Only allow for Low protection unless active client has High interest in focus
        if (level < FSP::Medium && protection < FSP::High)
            return true;
        // no timestamp at all, don't activate - because there's also creation timestamp
        // done on CreateNotify, this case should happen only in case application
        // maps again already used window, i.e. this won't happen after app startup
        return false;
    }

    // Low or medium FSP, usertime comparism is possible
    const xcb_timestamp_t user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Activation, compared:" << window << ":" << time << ":" << user_time
                       << ":" << (NET::timestampCompare(time, user_time) >= 0);
    return NET::timestampCompare(time, user_time) >= 0; // time >= user_time
}

// basically the same like allowClientActivation(), this time allowing
// a window to be fully raised upon its own request (XRaiseWindow),
// if refused, it will be raised only on top of windows belonging
// to the same application
bool Workspace::allowFullClientRaising(Toplevel const* window, xcb_timestamp_t time)
{
    auto level = window->control->rules().checkFSP(options->focusStealingPreventionLevel());
    if (sessionManager()->state() == SessionState::Saving && level <= 2) { // <= normal
        return true;
    }
    auto ac = mostRecentlyActivatedClient();
    if (level == 0) // none
        return true;
    if (level == 4) // extreme
        return false;
    if (ac == nullptr || win::is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Raising: No client active, allowing";
        return true; // no active client -> always allow
    }
    // TODO window urgency  -> return true?
    if (win::belong_to_same_client(window, ac, win::same_client_check::relaxed_for_active)) {
        qCDebug(KWIN_CORE) << "Raising: Belongs to active application";
        return true;
    }
    if (level == 3) // high
        return false;
    xcb_timestamp_t user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Raising, compared:" << time << ":" << user_time << ":"
                       << (NET::timestampCompare(time, user_time) >= 0);
    return NET::timestampCompare(time, user_time) >= 0; // time >= user_time
}

// called from Client after FocusIn that wasn't initiated by KWin and the client
// wasn't allowed to activate
void Workspace::restoreFocus()
{
    // this updateXTime() is necessary - as FocusIn events don't have
    // a timestamp *sigh*, kwin's timestamp would be older than the timestamp
    // that was used by whoever caused the focus change, and therefore
    // the attempt to restore the focus would fail due to old timestamp
    updateXTime();
    if (should_get_focus.size() > 0) {
        request_focus(should_get_focus.back());
    } else if (last_active_client) {
        request_focus(last_active_client);
    }
}

void Workspace::clientAttentionChanged(Toplevel* window, bool set)
{
    remove_all(attention_chain, window);
    if (set) {
        attention_chain.push_front(window);
    }
    Q_EMIT clientDemandsAttentionChanged(window, set);
}

/// X11 event handling

#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC 35
typedef struct xcb_ge_generic_event_t {
    uint8_t response_type;  /**<  */
    uint8_t extension;      /**<  */
    uint16_t sequence;      /**<  */
    uint32_t length;        /**<  */
    uint16_t event_type;    /**<  */
    uint8_t pad0[22];       /**<  */
    uint32_t full_sequence; /**<  */
} xcb_ge_generic_event_t;
#endif

QVector<QByteArray> s_xcbEerrors({QByteArrayLiteral("Success"),
                                  QByteArrayLiteral("BadRequest"),
                                  QByteArrayLiteral("BadValue"),
                                  QByteArrayLiteral("BadWindow"),
                                  QByteArrayLiteral("BadPixmap"),
                                  QByteArrayLiteral("BadAtom"),
                                  QByteArrayLiteral("BadCursor"),
                                  QByteArrayLiteral("BadFont"),
                                  QByteArrayLiteral("BadMatch"),
                                  QByteArrayLiteral("BadDrawable"),
                                  QByteArrayLiteral("BadAccess"),
                                  QByteArrayLiteral("BadAlloc"),
                                  QByteArrayLiteral("BadColor"),
                                  QByteArrayLiteral("BadGC"),
                                  QByteArrayLiteral("BadIDChoice"),
                                  QByteArrayLiteral("BadName"),
                                  QByteArrayLiteral("BadLength"),
                                  QByteArrayLiteral("BadImplementation"),
                                  QByteArrayLiteral("Unknown")});

/**
 * Handles workspace specific XCB event
 */
bool Workspace::workspaceEvent(xcb_generic_event_t* e)
{
    const uint8_t eventType = e->response_type & ~0x80;
    if (!eventType) {
        // let's check whether it's an error from one of the extensions KWin uses
        xcb_generic_error_t* error = reinterpret_cast<xcb_generic_error_t*>(e);
        const QVector<Xcb::ExtensionData> extensions = Xcb::Extensions::self()->extensions();
        for (const auto& extension : extensions) {
            if (error->major_code == extension.majorOpcode) {
                QByteArray errorName;
                if (error->error_code < s_xcbEerrors.size()) {
                    errorName = s_xcbEerrors.at(error->error_code);
                } else if (error->error_code >= extension.errorBase) {
                    const int index = error->error_code - extension.errorBase;
                    if (index >= 0 && index < extension.errorCodes.size()) {
                        errorName = extension.errorCodes.at(index);
                    }
                }
                if (errorName.isEmpty()) {
                    errorName = QByteArrayLiteral("Unknown");
                }
                qCWarning(KWIN_CORE,
                          "XCB error: %d (%s), sequence: %d, resource id: %d, major code: %d (%s), "
                          "minor code: %d (%s)",
                          int(error->error_code),
                          errorName.constData(),
                          int(error->sequence),
                          int(error->resource_id),
                          int(error->major_code),
                          extension.name.constData(),
                          int(error->minor_code),
                          extension.opCodes.size() > error->minor_code
                              ? extension.opCodes.at(error->minor_code).constData()
                              : "Unknown");
                return true;
            }
        }
        return false;
    }

    if (eventType == XCB_GE_GENERIC) {
        xcb_ge_generic_event_t* ge = reinterpret_cast<xcb_ge_generic_event_t*>(e);

        // We need to make a shadow copy of the event filter list because an activated event
        // filter may mutate it by removing or installing another event filter.
        auto const eventFilters = kwinApp()->x11_event_filters->generic_filters;

        for (auto container : eventFilters) {
            if (!container) {
                continue;
            }
            auto filter = container->filter();
            if (filter->extension() == ge->extension
                && filter->genericEventTypes().contains(ge->event_type) && filter->event(e)) {
                return true;
            }
        }
    } else {
        // We need to make a shadow copy of the event filter list because an activated event
        // filter may mutate it by removing or installing another event filter.
        auto const eventFilters = kwinApp()->x11_event_filters->filters;

        for (auto container : eventFilters) {
            if (!container) {
                continue;
            }
            auto filter = container->filter();
            if (filter->eventTypes().contains(eventType) && filter->event(e)) {
                return true;
            }
        }
    }

    if (effects && static_cast<render::effects_handler_impl*>(effects)->hasKeyboardGrab()
        && (eventType == XCB_KEY_PRESS || eventType == XCB_KEY_RELEASE))
        return false; // let Qt process it, it'll be intercepted again in eventFilter()

    // events that should be handled before Clients can get them
    switch (eventType) {
    case XCB_CONFIGURE_NOTIFY:
        if (reinterpret_cast<xcb_configure_notify_event_t*>(e)->event == rootWindow())
            x_stacking_tree->mark_as_dirty();
        break;
    };

    auto const eventWindow = win::x11::find_event_window(e);
    if (eventWindow != XCB_WINDOW_NONE) {
        if (auto c = findClient(win::x11::predicate_match::window, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto c = findClient(win::x11::predicate_match::wrapper_id, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto c = findClient(win::x11::predicate_match::frame_id, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto c = findClient(win::x11::predicate_match::input_id, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto unmanaged = findUnmanaged(eventWindow)) {
            if (win::x11::unmanaged_event(unmanaged, e)) {
                return true;
            }
        }
    }

    switch (eventType) {
    case XCB_CREATE_NOTIFY: {
        const auto* event = reinterpret_cast<xcb_create_notify_event_t*>(e);
        if (event->parent == rootWindow() && !QWidget::find(event->window)
            && !event->override_redirect) {
            // see comments for allowClientActivation()
            updateXTime();
            const xcb_timestamp_t t = xTime();
            xcb_change_property(connection(),
                                XCB_PROP_MODE_REPLACE,
                                event->window,
                                atoms->kde_net_wm_user_creation_time,
                                XCB_ATOM_CARDINAL,
                                32,
                                1,
                                &t);
        }
        break;
    }
    case XCB_UNMAP_NOTIFY: {
        const auto* event = reinterpret_cast<xcb_unmap_notify_event_t*>(e);
        return (event->event != event->window); // hide wm typical event from Qt
    }
    case XCB_REPARENT_NOTIFY: {
        // do not confuse Qt with these events. After all, _we_ are the
        // window manager who does the reparenting.
        return true;
    }
    case XCB_MAP_REQUEST: {
        updateXTime();

        const auto* event = reinterpret_cast<xcb_map_request_event_t*>(e);
        if (auto c = findClient(win::x11::predicate_match::window, event->window)) {
            // e->xmaprequest.window is different from e->xany.window
            // TODO this shouldn't be necessary now
            win::x11::window_event(c, e);
            win::focus_chain::self()->update(c, win::focus_chain::Update);
        } else if (true /*|| e->xmaprequest.parent != root */) {
            // NOTICE don't check for the parent being the root window, this breaks when some app
            // unmaps a window, changes something and immediately maps it back, without giving KWin
            // a chance to reparent it back to root
            // since KWin can get MapRequest only for root window children and
            // children of WindowWrapper (=clients), the check is AFAIK useless anyway
            // NOTICE: The save-set support in X11Client::mapRequestEvent() actually requires that
            // this code doesn't check the parent to be root.
            if (!createClient(event->window, false)) {
                xcb_map_window(connection(), event->window);
                const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
                xcb_configure_window(
                    connection(), event->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
            }
        }
        return true;
    }
    case XCB_MAP_NOTIFY: {
        const auto* event = reinterpret_cast<xcb_map_notify_event_t*>(e);
        if (event->override_redirect) {
            auto c = findUnmanaged(event->window);
            if (c == nullptr)
                c = createUnmanaged(event->window);
            if (c) {
                // if hasScheduledRelease is true, it means a unamp and map sequence has occurred.
                // since release is scheduled after map notify, this old Unmanaged will get released
                // before KWIN has chance to remanage it again. so release it right now.
                if (c->has_scheduled_release) {
                    win::x11::release_window(c, false);
                    c = createUnmanaged(event->window);
                }
                if (c) {
                    return win::x11::unmanaged_event(c, e);
                }
            }
        }
        return (event->event != event->window); // hide wm typical event from Qt
    }

    case XCB_CONFIGURE_REQUEST: {
        const auto* event = reinterpret_cast<xcb_configure_request_event_t*>(e);
        if (event->parent == rootWindow()) {
            uint32_t values[5] = {0, 0, 0, 0, 0};
            const uint32_t value_mask = event->value_mask
                & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH
                   | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH);
            int i = 0;
            if (value_mask & XCB_CONFIG_WINDOW_X) {
                values[i++] = event->x;
            }
            if (value_mask & XCB_CONFIG_WINDOW_Y) {
                values[i++] = event->y;
            }
            if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
                values[i++] = event->width;
            }
            if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
                values[i++] = event->height;
            }
            if (value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
                values[i++] = event->border_width;
            }
            xcb_configure_window(connection(), event->window, value_mask, values);
            return true;
        }
        break;
    }
    case XCB_FOCUS_IN: {
        const auto* event = reinterpret_cast<xcb_focus_in_event_t*>(e);
        if (event->event == rootWindow()
            && (event->detail == XCB_NOTIFY_DETAIL_NONE
                || event->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
                || event->detail == XCB_NOTIFY_DETAIL_INFERIOR)) {
            Xcb::CurrentInput currentInput;
            updateXTime(); // focusToNull() uses xTime(), which is old now (FocusIn has no
                           // timestamp)
            // it seems we can "loose" focus reversions when the closing client hold a grab
            // => catch the typical pattern (though we don't want the focus on the root anyway)
            // #348935
            const bool lostFocusPointerToRoot = currentInput->focus == rootWindow()
                && event->detail == XCB_NOTIFY_DETAIL_INFERIOR;
            if (!currentInput.isNull()
                && (currentInput->focus == XCB_WINDOW_NONE
                    || currentInput->focus == XCB_INPUT_FOCUS_POINTER_ROOT
                    || lostFocusPointerToRoot)) {
                // kWarning( 1212 ) << "X focus set to None/PointerRoot, reseting focus" ;
                auto window = mostRecentlyActivatedClient();
                if (window != nullptr) {
                    request_focus(window, false, true);
                } else if (activateNextClient(nullptr)) {
                    ; // ok, activated
                } else {
                    focusToNull();
                }
            }
        }
    }
        // fall through
    case XCB_FOCUS_OUT:
        return true; // always eat these, they would tell Qt that KWin is the active app
    default:
        break;
    }
    return false;
}

// Used only to filter events that need to be processed by Qt first
// (e.g. keyboard input to be composed), otherwise events are
// handle by the XEvent filter above
bool Workspace::workspaceEvent(QEvent* e)
{
    if ((e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease
         || e->type() == QEvent::ShortcutOverride)
        && effects && static_cast<render::effects_handler_impl*>(effects)->hasKeyboardGrab()) {
        static_cast<render::effects_handler_impl*>(effects)->grabbedKeyboardEvent(
            static_cast<QKeyEvent*>(e));
        return true;
    }
    return false;
}

}
