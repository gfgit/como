/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "session_manager.h"
#include "x11/geo.h"

#include "utils/algorithm.h"

#include <KConfig>
#include <KConfigGroup>

namespace KWin::win
{

static inline KConfig* get_session_config(QString const& id, QString const& key)
{
    static KConfig* config = nullptr;
    static QString lastId;
    static QString lastKey;
    static auto pattern = QString(QLatin1String("session/%1_%2_%3")).arg(qApp->applicationName());

    if (id != lastId || key != lastKey) {
        delete config;
        config = nullptr;
    }

    lastId = id;
    lastKey = key;

    if (!config) {
        config = new KConfig(pattern.arg(id).arg(key), KConfig::SimpleConfig);
    }

    return config;
}

static const char* const window_type_names[] = {"Unknown",
                                                "Normal",
                                                "Desktop",
                                                "Dock",
                                                "Toolbar",
                                                "Menu",
                                                "Dialog",
                                                "Override",
                                                "TopMenu",
                                                "Utility",
                                                "Splash"};
// change also the two functions below when adding new entries

static inline char const* window_type_to_txt(NET::WindowType type)
{
    if (type >= NET::Unknown && type <= NET::Splash) {
        // +1 (unknown==-1)
        return window_type_names[type + 1];
    }

    if (type == -2) {
        // undefined (not really part of NET::WindowType)
        return "Undefined";
    }

    qFatal("Unknown Window Type");
    return nullptr;
}

static inline NET::WindowType txt_to_window_type(const char* txt)
{
    for (int i = NET::Unknown; i <= NET::Splash; ++i) {
        // Compare with window_type_names at i+1.
        if (qstrcmp(txt, window_type_names[i + 1]) == 0) {
            return static_cast<NET::WindowType>(i);
        }
    }

    // undefined
    return static_cast<NET::WindowType>(-2);
}

/**
 * Stores the current session in the config file
 *
 * @see loadSessionInfo
 */
template<typename Space>
void store_session(Space& space, QString const& sessionName, sm_save_phase phase)
{
    qCDebug(KWIN_CORE) << "storing session" << sessionName << "in phase" << phase;
    KConfig* config = get_session_config(sessionName, QString());

    KConfigGroup cg(config, "Session");
    int count = 0;
    int active_client = -1;

    for (auto const& window : space.windows) {
        if (!window->control) {
            continue;
        }
        auto x11_client = dynamic_cast<x11::window*>(window);
        if (!x11_client) {
            continue;
        }

        if (x11_client->windowType() > NET::Splash) {
            // window types outside this are not tooltips/menus/OSDs
            // typically these will be unmanaged and not in this list anyway, but that is not
            // enforced
            continue;
        }

        QByteArray sessionId = x11_client->sessionId();
        QByteArray wmCommand = x11_client->wmCommand();

        if (sessionId.isEmpty()) {
            // remember also applications that are not XSMP capable
            // and use the obsolete WM_COMMAND / WM_SAVE_YOURSELF
            if (wmCommand.isEmpty()) {
                continue;
            }
        }

        count++;
        if (x11_client->control->active()) {
            active_client = count;
        }

        if (phase == sm_save_phase2 || phase == sm_save_phase2_full) {
            store_window(space, cg, count, x11_client);
        }
    }

    if (phase == sm_save_phase0) {
        // it would be much simpler to save these values to the config file,
        // but both Qt and KDE treat phase1 and phase2 separately,
        // which results in different sessionkey and different config file :(
        space.session_active_client = active_client;
        space.session_desktop = space.virtual_desktop_manager->current();
    } else if (phase == sm_save_phase2) {
        cg.writeEntry("count", count);
        cg.writeEntry("active", space.session_active_client);
        cg.writeEntry("desktop", space.session_desktop);
    } else {
        // SMSavePhase2Full
        cg.writeEntry("count", count);
        cg.writeEntry("active", space.session_active_client);
        cg.writeEntry("desktop", space.virtual_desktop_manager->current());
    }

    // it previously did some "revert to defaults" stuff for phase1 I think
    config->sync();
}

template<typename Space, typename Win>
void store_window(Space const& space, KConfigGroup& cg, int num, Win* c)
{
    QString n = QString::number(num);
    cg.writeEntry(QLatin1String("sessionId") + n, c->sessionId().constData());
    cg.writeEntry(QLatin1String("windowRole") + n, c->windowRole().constData());
    cg.writeEntry(QLatin1String("wmCommand") + n, c->wmCommand().constData());
    cg.writeEntry(QLatin1String("resourceName") + n, c->resource_name.constData());
    cg.writeEntry(QLatin1String("resourceClass") + n, c->resource_class.constData());
    cg.writeEntry(QLatin1String("geometry") + n,
                  QRect(x11::calculate_gravitation(c, true), frame_to_client_size(c, c->size())));
    cg.writeEntry(QLatin1String("restore") + n, c->restore_geometries.maximize);
    cg.writeEntry(QLatin1String("fsrestore") + n, c->restore_geometries.maximize);
    cg.writeEntry(QLatin1String("maximize") + n, static_cast<int>(c->maximizeMode()));
    cg.writeEntry(QLatin1String("fullscreen") + n, static_cast<int>(c->control->fullscreen()));
    cg.writeEntry(QLatin1String("desktop") + n, c->desktop());

    // the config entry is called "iconified" for back. comp. reasons
    // (kconf_update script for updating session files would be too complicated)
    cg.writeEntry(QLatin1String("iconified") + n, c->control->minimized());
    cg.writeEntry(QLatin1String("opacity") + n, c->opacity());

    // the config entry is called "sticky" for back. comp. reasons
    cg.writeEntry(QLatin1String("sticky") + n, c->isOnAllDesktops());

    // the config entry is called "staysOnTop" for back. comp. reasons
    cg.writeEntry(QLatin1String("staysOnTop") + n, c->control->keep_above());
    cg.writeEntry(QLatin1String("keepBelow") + n, c->control->keep_below());
    cg.writeEntry(QLatin1String("skipTaskbar") + n, c->control->original_skip_taskbar());
    cg.writeEntry(QLatin1String("skipPager") + n, c->control->skip_pager());
    cg.writeEntry(QLatin1String("skipSwitcher") + n, c->control->skip_switcher());

    // not really just set by user, but name kept for back. comp. reasons
    cg.writeEntry(QLatin1String("userNoBorder") + n, c->user_no_border);
    cg.writeEntry(QLatin1String("windowType") + n, window_type_to_txt(c->windowType()));
    cg.writeEntry(QLatin1String("shortcut") + n, c->control->shortcut().toString());
    cg.writeEntry(QLatin1String("stackingOrder") + n,
                  static_cast<int>(index_of(space.stacking_order->pre_stack, c)));
}

template<typename Space>
void store_subsession(Space const& space, QString const& name, QSet<QByteArray> sessionIds)
{
    // TODO clear it first
    KConfigGroup cg(KSharedConfig::openConfig(), QLatin1String("SubSession: ") + name);
    int count = 0;
    int active_client = -1;

    for (auto const& window : space.windows) {
        if (!window->control) {
            continue;
        }

        auto x11_client = dynamic_cast<win::x11::window*>(window);
        if (!x11_client) {
            continue;
        }
        if (x11_client->windowType() > NET::Splash) {
            continue;
        }

        QByteArray sessionId = x11_client->sessionId();
        QByteArray wmCommand = x11_client->wmCommand();
        if (sessionId.isEmpty()) {
            // remember also applications that are not XSMP capable
            // and use the obsolete WM_COMMAND / WM_SAVE_YOURSELF
            if (wmCommand.isEmpty()) {
                continue;
            }
        }
        if (!sessionIds.contains(sessionId)) {
            continue;
        }

        qCDebug(KWIN_CORE) << "storing" << sessionId;
        count++;

        if (x11_client->control->active()) {
            active_client = count;
        }
        store_window(space, cg, count, x11_client);
    }

    cg.writeEntry("count", count);
    cg.writeEntry("active", active_client);
    // cg.writeEntry( "desktop", currentDesktop());
}

/**
 * Loads the session information from the config file.
 *
 * @see storeSession
 */
template<typename Space>
void load_session_info(Space& space, QString const& sessionName)
{
    space.session.clear();
    KConfigGroup cg(get_session_config(sessionName, QString()), "Session");
    add_session_info(space, cg);
}

template<typename Space>
void add_session_info(Space& space, KConfigGroup& cg)
{
    space.m_initialDesktop = cg.readEntry("desktop", 1);
    int count = cg.readEntry("count", 0);
    int active_client = cg.readEntry("active", 0);

    for (int i = 1; i <= count; i++) {
        QString n = QString::number(i);
        auto info = new win::session_info;
        space.session.push_back(info);
        info->sessionId = cg.readEntry(QLatin1String("sessionId") + n, QString()).toLatin1();
        info->windowRole = cg.readEntry(QLatin1String("windowRole") + n, QString()).toLatin1();
        info->wmCommand = cg.readEntry(QLatin1String("wmCommand") + n, QString()).toLatin1();
        info->resourceName = cg.readEntry(QLatin1String("resourceName") + n, QString()).toLatin1();
        info->resourceClass
            = cg.readEntry(QLatin1String("resourceClass") + n, QString()).toLower().toLatin1();
        info->geometry = cg.readEntry(QLatin1String("geometry") + n, QRect());
        info->restore = cg.readEntry(QLatin1String("restore") + n, QRect());
        info->fsrestore = cg.readEntry(QLatin1String("fsrestore") + n, QRect());
        info->maximized = cg.readEntry(QLatin1String("maximize") + n, 0);
        info->fullscreen = cg.readEntry(QLatin1String("fullscreen") + n, 0);
        info->desktop = cg.readEntry(QLatin1String("desktop") + n, 0);
        info->minimized = cg.readEntry(QLatin1String("iconified") + n, false);
        info->opacity = cg.readEntry(QLatin1String("opacity") + n, 1.0);
        info->onAllDesktops = cg.readEntry(QLatin1String("sticky") + n, false);
        info->keepAbove = cg.readEntry(QLatin1String("staysOnTop") + n, false);
        info->keepBelow = cg.readEntry(QLatin1String("keepBelow") + n, false);
        info->skipTaskbar = cg.readEntry(QLatin1String("skipTaskbar") + n, false);
        info->skipPager = cg.readEntry(QLatin1String("skipPager") + n, false);
        info->skipSwitcher = cg.readEntry(QLatin1String("skipSwitcher") + n, false);
        info->noBorder = cg.readEntry(QLatin1String("userNoBorder") + n, false);
        info->windowType = txt_to_window_type(
            cg.readEntry(QLatin1String("windowType") + n, QString()).toLatin1().constData());
        info->shortcut = cg.readEntry(QLatin1String("shortcut") + n, QString());
        info->active = (active_client == i);
        info->stackingOrder = cg.readEntry(QLatin1String("stackingOrder") + n, -1);
    }
}

template<typename Space>
void load_subsession_info(Space& space, QString const& name)
{
    KConfigGroup cg(KSharedConfig::openConfig(), QLatin1String("SubSession: ") + name);
    add_session_info(space, cg);
}

template<typename Win>
static bool session_info_window_type_match(Win const& c, win::session_info* info)
{
    if (info->windowType == -2) {
        // undefined (not really part of NET::WindowType)
        return !is_special_window(c);
    }
    return info->windowType == c->windowType();
}

/**
 * Returns a SessionInfo for client \a c. The returned session
 * info is removed from the storage. It's up to the caller to delete it.
 *
 * This function is called when a new window is mapped and must be managed.
 * We try to find a matching entry in the session.
 *
 * May return 0 if there's no session info for the client.
 */
template<typename Space, typename Win>
session_info* take_session_info(Space& space, Win* c)
{
    win::session_info* realInfo = nullptr;
    QByteArray sessionId = c->sessionId();
    QByteArray windowRole = c->windowRole();
    QByteArray wmCommand = c->wmCommand();
    auto const& resourceName = c->resource_name;
    auto const& resourceClass = c->resource_class;

    // First search ``session''
    if (!sessionId.isEmpty()) {
        // look for a real session managed client (algorithm suggested by ICCCM)
        for (auto const& info : space.session) {
            if (realInfo) {
                break;
            }
            if (info->sessionId == sessionId && session_info_window_type_match(c, info)) {
                if (!windowRole.isEmpty()) {
                    if (info->windowRole == windowRole) {
                        realInfo = info;
                        remove_all(space.session, info);
                    }
                } else {
                    if (info->windowRole.isEmpty() && info->resourceName == resourceName
                        && info->resourceClass == resourceClass) {
                        realInfo = info;
                        remove_all(space.session, info);
                    }
                }
            }
        }
    } else {
        // look for a sessioninfo with matching features.
        for (auto const& info : space.session) {
            if (realInfo) {
                break;
            }
            if (info->resourceName == resourceName && info->resourceClass == resourceClass
                && session_info_window_type_match(c, info)) {
                if (wmCommand.isEmpty() || info->wmCommand == wmCommand) {
                    realInfo = info;
                    remove_all(space.session, info);
                }
            }
        }
    }
    return realInfo;
}

}