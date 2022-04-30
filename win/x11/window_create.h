/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "stacking_tree.h"
#include "transient.h"

#include "win/space_helpers.h"

namespace KWin::win::x11
{

template<typename Space, typename Win>
void add_controlled_window_to_space(Space& space, Win* win)
{
    auto grp = space.findGroup(win->xcb_window());

    space.m_windows.push_back(win);
    Q_EMIT space.clientAdded(win);

    if (grp) {
        grp->gotLeader(win);
    }

    if (is_desktop(win)) {
        if (!space.active_client && space.should_get_focus.empty() && win->isOnCurrentDesktop()) {
            // TODO: Make sure desktop is active after startup if there's no other window active
            space.request_focus(win);
        }
    } else {
        focus_chain::self()->update(win, focus_chain::Update);
    }

    if (!contains(space.stacking_order->pre_stack, win)) {
        // Raise if it hasn't got any stacking position yet
        space.stacking_order->pre_stack.push_back(win);
    }
    if (!contains(space.stacking_order->sorted(), win)) {
        // It'll be updated later, and updateToolWindows() requires c to be in stacking_order.
        space.stacking_order->win_stack.push_back(win);
    }
    space.x_stacking_tree->mark_as_dirty();

    // This cannot be in manage(), because the client got added only now
    space.updateClientArea();
    update_layer(win);

    if (is_desktop(win)) {
        raise_window(&space, win);
        // If there's no active client, make this desktop the active one
        if (!space.activeClient() && space.should_get_focus.size() == 0)
            space.activateClient(
                find_desktop(&space, true, virtual_desktop_manager::self()->current()));
    }

    check_active_modal<Win>();
    space.checkTransients(win);

    // Propagate new client
    space.stacking_order->update(true);

    if (is_utility(win) || is_menu(win) || is_toolbar(win)) {
        update_tool_windows(&space, true);
    }

    space.updateTabbox();
}

}
