/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "win/transient.h"

#include "group.h"

namespace KWin::win::x11
{

template<typename Win>
void set_transient_lead(Win* win, xcb_window_t lead_id);

class transient : public win::transient
{
public:
    xcb_window_t lead_id{XCB_WINDOW_NONE};
    xcb_window_t original_lead_id{XCB_WINDOW_NONE};

    transient(window* win)
        : win::transient(win)
    {
    }

    void remove_child(Toplevel* child) override
    {
        // window is transient for m_client, but m_client is going away
        // make window instead a group transient.
        win::transient::remove_child(child);

        if (!child->transient()->lead()) {
            if (auto x11_child = qobject_cast<window*>(child)) {
                static_cast<transient*>(x11_child->transient())->lead_id = XCB_WINDOW_NONE;
                set_transient_lead(x11_child, XCB_WINDOW_NONE);
            }
        }
    }
};

/**

 Transiency stuff: ICCCM 4.1.2.6, NETWM 7.3

 WM_TRANSIENT_FOR is basically means "this is my mainwindow".
 For NET::Unknown windows, transient windows are considered to be NET::Dialog
 windows, for compatibility with non-NETWM clients. KWin may adjust the value
 of this property in some cases (window pointing to itself or creating a loop,
 keeping NET::Splash windows above other windows from the same app, etc.).

 X11Client::transient_for_id is the value of the WM_TRANSIENT_FOR property, after
 possibly being adjusted by KWin. X11Client::transient_for points to the Client
 this Client is transient for, or is NULL. If X11Client::transient_for_id is
 poiting to the root window, the window is considered to be transient
 for the whole window group, as suggested in NETWM 7.3.

 In the case of group transient window, X11Client::transient_for is NULL,
 and X11Client::groupTransient() returns true. Such window is treated as
 if it were transient for every window in its window group that has been
 mapped _before_ it (or, to be exact, was added to the same group before it).
 Otherwise two group transients can create loops, which can lead very very
 nasty things (bug #67914 and all its dupes).

 X11Client::original_transient_for_id is the value of the property, which
 may be different if X11Client::transient_for_id if e.g. forcing NET::Splash
 to be kept on top of its window group, or when the mainwindow is not mapped
 yet, in which case the window is temporarily made group transient,
 and when the mainwindow is mapped, transiency is re-evaluated.

 This can get a bit complicated with with e.g. two Konqueror windows created
 by the same process. They should ideally appear like two independent applications
 to the user. This should be accomplished by all windows in the same process
 having the same window group (needs to be changed in Qt at the moment), and
 using non-group transients poiting to their relevant mainwindow for toolwindows
 etc. KWin should handle both group and non-group transient dialogs well.

 In other words:
 - non-transient windows     : isTransient() == false
 - normal transients         : transientFor() != NULL
 - group transients          : groupTransient() == true

 - list of mainwindows       : mainClients()  (call once and loop over the result)
 - list of transients        : transients()
 - every window in the group : group()->members()
*/

template<typename Win>
transient* x11_transient(Win* win)
{
    return static_cast<win::x11::transient*>(win->transient());
}

template<typename Win>
Xcb::TransientFor fetch_transient(Win* win)
{
    return Xcb::TransientFor(win->xcb_window());
}

template<typename Win>
void read_transient_property(Win* win, Xcb::TransientFor& transientFor)
{
    xcb_window_t lead_id = XCB_WINDOW_NONE;

    bool failed = false;
    if (!transientFor.getTransientFor(&lead_id)) {
        lead_id = XCB_WINDOW_NONE;
        failed = true;
    }

    x11_transient(win)->original_lead_id = lead_id;
    lead_id = verify_transient_for(win, lead_id, !failed);

    set_transient_lead(win, lead_id);
}

template<typename Win>
void set_transient_lead(Win* win, xcb_window_t lead_id)
{
    auto x11_tr = x11_transient(win);

    if (lead_id == x11_tr->lead_id) {
        return;
    }

    for (auto lead : win->transient()->leads()) {
        lead->transient()->remove_child(win);
    }

    x11_tr->lead_id = lead_id;

    if (lead_id != XCB_WINDOW_NONE && lead_id != rootWindow()) {
        auto lead = workspace()->findClient(predicate_match::window, lead_id);

        if (contains(win->transient()->children, lead)) {
            // Ensure we do not add a loop.
            // TODO(romangg): Is this already ensured with verify_transient_for?
            win->transient()->remove_child(lead);
        }
        lead->transient()->add_child(win);
    }

    check_group(win, nullptr);
    workspace()->updateClientLayer(win);
    workspace()->resetUpdateToolWindowsTimer();
}

template<typename Win>
void clean_grouping(Win* win)
{
    x11_transient(win)->lead_id = XCB_WINDOW_NONE;
    x11_transient(win)->original_lead_id = XCB_WINDOW_NONE;

    update_group(win, false);
}

/**
 * Updates the group transient relations between group members when @arg win gets added or removed.
 */
template<typename Win>
void update_group(Win* win, bool add)
{
    assert(win->in_group);

    if (add) {
        if (!contains(win->in_group->members(), win)) {
            win->in_group->addMember(win);
        }
        auto const win_is_group_tr = win->groupTransient();
        auto const win_is_normal_tr = !win_is_group_tr && win->transient()->lead();

        // This added window must be set as transient child for all windows that have no direct
        // or indirect transient relation with it (that way we ensure there are no cycles).
        for (auto member : win->in_group->members()) {
            if (member == win) {
                continue;
            }

            auto const member_is_group_tr = member->groupTransient();
            auto const member_is_normal_tr = !member_is_group_tr && member->transient()->lead();

            if (win_is_group_tr) {
                // Prefer to add 'win' (the new window to the group) as a child but ensure that we
                // have no cycle.
                if (!member_is_normal_tr && !member->transient()->is_follower_of(win)) {
                    member->transient()->add_child(win);
                    continue;
                }
            }

            if (member_is_group_tr && !win_is_normal_tr
                && !win->transient()->is_follower_of(member)) {
                win->transient()->add_child(member);
            }
        }
    } else {
        win->in_group->ref();
        win->in_group->removeMember(win);

        for (auto member : win->in_group->members()) {
            if (x11_transient(win)->lead_id == member->xcb_window()) {
                if (!contains(member->transient()->children, win)) {
                    member->transient()->add_child(win);
                }
            } else {
                member->transient()->remove_child(win);
            }
        }

        // Restore indirect group transient relations between members that have been cut off because
        // off the removal of this.
        for (auto& member : win->in_group->members()) {
            if (!member->groupTransient()) {
                continue;
            }

            for (auto lead : win->in_group->members()) {
                if (lead == member) {
                    continue;
                }
                if (!member->transient()->is_follower_of(lead)
                    && !lead->transient()->is_follower_of(member)) {
                    // This is not fully correct since relative distances between indirect
                    // transients might be shuffeled but since X11 group transients are rarely used
                    // today let's ignore it for now.
                    lead->transient()->add_child(member);
                }
            }
        }

        win->in_group->deref();
        win->in_group = nullptr;
    }
}

/**
 * Check that the window is not transient for itself, and similar nonsense.
 */
template<typename Win>
xcb_window_t verify_transient_for(Win* win, xcb_window_t new_transient_for, bool set)
{
    xcb_window_t new_property_value = new_transient_for;

    // make sure splashscreens are shown above all their app's windows, even though
    // they're in Normal layer
    if (win::is_splash(win) && new_transient_for == XCB_WINDOW_NONE) {
        new_transient_for = rootWindow();
    }

    if (new_transient_for == XCB_WINDOW_NONE) {
        if (set) {
            // sometimes WM_TRANSIENT_FOR is set to None, instead of root window
            new_property_value = new_transient_for = rootWindow();
        } else {
            return XCB_WINDOW_NONE;
        }
    }
    if (new_transient_for == win->xcb_window()) {
        // pointing to self
        // also fix the property itself
        qCWarning(KWIN_CORE) << "Client " << win << " has WM_TRANSIENT_FOR poiting to itself.";
        new_property_value = new_transient_for = rootWindow();
    }

    //  The transient_for window may be embedded in another application,
    //  so kwin cannot see it. Try to find the managed client for the
    //  window and fix the transient_for property if possible.
    auto before_search = new_transient_for;

    while (new_transient_for != XCB_WINDOW_NONE && new_transient_for != rootWindow()
           && !workspace()->findClient(predicate_match::window, new_transient_for)) {
        Xcb::Tree tree(new_transient_for);
        if (tree.isNull()) {
            break;
        }
        new_transient_for = tree->parent;
    }

    if (auto new_transient_for_client
        = workspace()->findClient(predicate_match::window, new_transient_for)) {
        if (new_transient_for != before_search) {
            qCDebug(KWIN_CORE) << "Client " << win
                               << " has WM_TRANSIENT_FOR poiting to non-toplevel window "
                               << before_search << ", child of " << new_transient_for_client
                               << ", adjusting.";

            // also fix the property
            new_property_value = new_transient_for;
        }
    } else {
        // nice try
        new_transient_for = before_search;
    }

    // loop detection
    // group transients cannot cause loops, because they're considered transient only for
    // non-transient windows in the group
    int count = 20;
    auto loop_pos = new_transient_for;

    while (loop_pos != XCB_WINDOW_NONE && loop_pos != rootWindow()) {
        auto pos = workspace()->findClient(predicate_match::window, loop_pos);
        if (pos == nullptr) {
            break;
        }

        loop_pos = x11_transient(pos)->lead_id;

        if (--count == 0 || pos == win) {
            qCWarning(KWIN_CORE) << "Client " << win << " caused WM_TRANSIENT_FOR loop.";
            new_transient_for = rootWindow();
        }
    }

    if (new_transient_for != rootWindow()
        && workspace()->findClient(predicate_match::window, new_transient_for) == nullptr) {
        // it's transient for a specific window, but that window is not mapped
        new_transient_for = rootWindow();
    }

    if (new_property_value != x11_transient(win)->original_lead_id) {
        Xcb::setTransientFor(win->xcb_window(), new_property_value);
    }

    return new_transient_for;
}

template<typename Win>
void check_active_modal()
{
    // If the active window got new modal transient, activate it.
    auto win = qobject_cast<Win*>(workspace()->mostRecentlyActivatedClient());
    if (!win) {
        return;
    }

    auto new_modal = qobject_cast<Win*>(win->findModal());

    if (new_modal && new_modal != win) {
        if (!new_modal->control) {
            // postpone check until end of manage()
            return;
        }
        workspace()->activateClient(new_modal);
    }
}

template<typename Win>
void check_group(Win* win, Group* group)
{
    // First get all information about the current group.
    if (!group) {
        auto lead = win->transient()->lead();

        if (lead) {
            // Move the window to the right group (e.g. a dialog provided
            // by this app, but transient for another, so make it part of that group).
            group = lead->group();
        } else if (win->info->groupLeader() != XCB_WINDOW_NONE) {
            group = workspace()->findGroup(win->info->groupLeader());
            if (!group) {
                // doesn't exist yet
                group = new Group(win->info->groupLeader());
            }
        } else {
            group = find_client_leader_group(win);
            if (!group) {
                group = new Group(XCB_WINDOW_NONE);
            }
        }
    }

    if (win->in_group && win->in_group != group) {
        update_group(win, false);
    }

    win->in_group = group;

    if (win->in_group) {
        update_group(win, true);
    }

    check_active_modal<Win>();
    workspace()->updateClientLayer(win);
}

template<typename Win>
void change_client_leader_group(Win* win, Group* group)
{
    auto lead_id = x11_transient(win)->lead_id;
    if (lead_id != XCB_WINDOW_NONE && lead_id != rootWindow()) {
        // Transients are in the group of their lead.
        return;
    }

    if (win->info->groupLeader()) {
        // A leader is already set. Don't change it.
        return;
    }

    // Will ultimately change the group.
    check_group(win, group);
}

/**
 *  Tries to find a group that has member windows with the same client leader like @ref win.
 */
template<typename Win>
Group* find_client_leader_group(Win const* win)
{
    Group* ret = nullptr;

    for (auto const& other : workspace()->allClientList()) {
        if (other == win) {
            continue;
        }
        if (other->wmClientLeader() != win->wmClientLeader()) {
            continue;
        }

        if (!ret || ret != other->group()) {
            // Found new group.
            ret = other->group();
            continue;
        }

        // There are already two groups with the same client leader.
        // This most probably means the app uses group transients without
        // setting group for its windows. Merging the two groups is a bad
        // hack, but there's no really good solution for this case.
        auto old_group_members = other->group()->members();

        // The old group auto-deletes when being empty.
        for (size_t pos = 0; pos < old_group_members.size(); ++pos) {
            auto member = old_group_members[pos];
            if (member == win) {
                // 'win' will be removed from this group after we return.
                continue;
            }
            change_client_leader_group(member, ret);
        }
    }

    return ret;
}

}
