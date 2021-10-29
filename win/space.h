/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "meta.h"
#include "net.h"
#include "screen.h"
#include "stacking_order.h"
#include "transient.h"
#include "types.h"

#include "x11/group.h"
#include "x11/hide.h"
#include "x11/netinfo.h"

#include "options.h"

namespace KWin::win
{

template<typename Space>
void update_client_visibility_on_desktop_change(Space* space, uint newDesktop)
{
    for (auto const& toplevel : space->stacking_order->sorted()) {
        auto client = qobject_cast<x11::window*>(toplevel);
        if (!client) {
            continue;
        }

        if (!client->isOnDesktop(newDesktop) && client != space->moveResizeClient()
            && client->isOnCurrentActivity()) {
            update_visibility(client);
        }
    }

    // Now propagate the change, after hiding, before showing.
    if (x11::rootInfo()) {
        x11::rootInfo()->setCurrentDesktop(VirtualDesktopManager::self()->current());
    }

    if (auto move_resize_client = space->moveResizeClient()) {
        if (!move_resize_client->isOnDesktop(newDesktop)) {
            win::set_desktop(move_resize_client, newDesktop);
        }
    }

    auto const& list = space->stacking_order->sorted();
    for (int i = list.size() - 1; i >= 0; --i) {
        auto client = qobject_cast<x11::window*>(list.at(i));
        if (!client) {
            continue;
        }
        if (client->isOnDesktop(newDesktop) && client->isOnCurrentActivity()) {
            update_visibility(client);
        }
    }

    if (space->showingDesktop()) {
        // Do this only after desktop change to avoid flicker.
        space->setShowingDesktop(false);
    }
}

/**
 * Relevant for windows of type NET::Utility, NET::Menu or NET::Toolbar.
 */
template<typename Space>
void update_tool_windows(Space* space, bool also_hide)
{
    if (!options->isHideUtilityWindowsForInactive()) {
        for (auto const& window : space->allClientList()) {
            window->hideClient(false);
        }
        return;
    }

    x11::group const* active_group = nullptr;
    auto active_window = space->activeClient();

    // Go up in transiency hiearchy, if the top is found, only tool transients for the top
    // window will be shown; if a group transient is group, all tools in the group will be shown.
    while (active_window) {
        if (!active_window->isTransient()) {
            break;
        }
        if (active_window->groupTransient()) {
            active_group = active_window->group();
            break;
        }
        active_window = active_window->transient()->lead();
    }

    // Use stacking order only to reduce flicker, it doesn't matter if block_stacking_updates == 0,
    // i.e. if it's not up to date.

    // TODO(SELI): But maybe it should - what if a new window has been added that's not in stacking
    // order yet?
    std::vector<Toplevel*> to_show;
    std::vector<Toplevel*> to_hide;

    for (auto const& window : space->stacking_order->sorted()) {
        if (!window->control) {
            continue;
        }

        if (!is_utility(window) && !is_menu(window) && !is_toolbar(window)) {
            continue;
        }

        auto show{true};

        if (window->isTransient()) {
            auto const in_active_group = active_group && window->group() == active_group;
            auto const has_active_lead
                = active_window && window->transient()->is_follower_of(active_window);
            show = in_active_group || has_active_lead;
        } else {
            auto const is_individual = !window->group() || window->group()->members().size() == 1;
            auto const in_active_group = active_window && active_window->group() == window->group();
            show = is_individual || in_active_group;
        }

        if (!show && also_hide) {
            auto const& leads = window->transient()->leads();
            // Don't hide utility windows which are standalone(?) or have e.g. kicker as lead.
            show = leads.empty()
                || std::any_of(leads.cbegin(), leads.cend(), is_special_window<Toplevel>);
            if (!show) {
                to_hide.push_back(window);
            }
        }

        if (show) {
            to_show.push_back(window);
        }
    }

    // First show new ones, then hide.
    // Show from topmost.
    for (int i = to_show.size() - 1; i >= 0; --i) {
        // TODO(unknown author): Since this is in stacking order, the order of taskbar entries
        //                       changes :(
        to_show.at(i)->hideClient(false);
    }

    if (also_hide) {
        // Hide from bottom-most.
        for (auto const& window : to_hide) {
            window->hideClient(true);
        }
        space->stopUpdateToolWindowsTimer();
    } else {
        // Workspace::setActiveClient(..) is afterwards called with NULL client, quickly followed
        // by setting a new client, which would result in flickering.
        space->resetUpdateToolWindowsTimer();
    }
}

}
