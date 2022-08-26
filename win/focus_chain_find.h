/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "screen.h"

namespace KWin::win
{

/**
 * @brief Finds the best window to become the new active window in the focus chain for the given
 * virtual @p desktop on the given @p output.
 *
 * This method makes only sense to use if separate output focus is used. If separate output
 * focus is disabled the @p output is ignored. If no window for activation is found @c null is
 * returned.
 *
 * @param desktop The virtual desktop to look for a window for activation
 * @param output The output to constrain the search on with separate output focus
 * @return The window which could be activated or @c null if there is none.
 */
template<typename Win, typename Manager>
Win* focus_chain_get_for_activation(Manager& manager, uint desktop, base::output const* output)
{
    auto desk_it = manager.chains.desktops.find(desktop);
    if (desk_it == manager.chains.desktops.end()) {
        return nullptr;
    }

    auto const& chain = desk_it->second;

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        // TODO: move the check into Client
        auto win = *it;
        if (!win->isShown()) {
            continue;
        }
        if (manager.has_separate_screen_focus && win->central_output != output) {
            continue;
        }
        return win;
    }

    return nullptr;
}

template<typename Win, typename Manager>
Win* focus_chain_get_for_activation_on_current_output(Manager& manager, uint desktop)
{
    return focus_chain_get_for_activation<Win>(manager, desktop, get_current_output(manager.space));
}

template<typename Manager>
bool focus_chain_is_usable_focus_candidate(Manager& manager, Toplevel* window, Toplevel* prev)
{
    if (window == prev) {
        return false;
    }
    if (!window->isShown() || !window->isOnCurrentDesktop()) {
        return false;
    }

    if (!manager.has_separate_screen_focus) {
        return true;
    }

    return on_screen(window, prev ? prev->central_output : get_current_output(manager.space));
}

/**
 * @brief Queries the focus chain for @p desktop for the next window in relation to the given
 * @p reference.
 *
 * The method finds the first usable window which is not the @p reference Client. If no Client
 * can be found @c null is returned
 *
 * @param reference The reference window which should not be returned
 * @param desktop The virtual desktop whose focus chain should be used
 * @return *The next usable window or @c null if none can be found.
 */
template<typename Manager, typename Win>
Toplevel* focus_chain_next_for_desktop(Manager& manager, Win* reference, uint desktop)
{
    auto desk_it = manager.chains.desktops.find(desktop);
    if (desk_it == manager.chains.desktops.end()) {
        return nullptr;
    }

    auto const& chain = desk_it->second;

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (focus_chain_is_usable_focus_candidate(manager, *it, reference)) {
            return *it;
        }
    }

    return nullptr;
}

}