/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "netinfo.h"

#include "win/stacking_order.h"

#include "screenedge.h"
#include "utils.h"
#include "workspace.h"
#include "xcbutils.h"

#include <vector>

namespace KWin::win::x11
{

class KWIN_EXPORT space : public Workspace
{
    Q_OBJECT
public:
    space();
    ~space() override;

protected:
    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        win::space_areas& areas) override;
};

template<typename Space, typename Window>
void restore_session_stacking_order(Space space, Window* c)
{
    if (c->sm_stacking_order < 0) {
        return;
    }

    Blocker blocker(space->stacking_order);
    remove_all(space->stacking_order->pre_stack, c);

    for (auto it = space->stacking_order->pre_stack.begin(); // from bottom
         it != space->stacking_order->pre_stack.end();
         ++it) {
        auto current = qobject_cast<Window*>(*it);
        if (!current) {
            continue;
        }
        if (current->sm_stacking_order > c->sm_stacking_order) {
            space->stacking_order->pre_stack.insert(it, c);
            return;
        }
    }
    space->stacking_order->pre_stack.push_back(c);
}

/**
 * Some fullscreen effects have to raise the screenedge on top of an input window, thus all windows
 * this function puts them back where they belong for regular use and is some cheap variant of
 * the regular propagate_clients function in that it completely ignores managed clients and
 * everything else and also does not update the NETWM property. Called from
 * Effects::destroyInputWindow so far.
 */
template<typename Space>
void stack_screen_edges_under_override_redirect(Space* /*space*/)
{
    if (!rootInfo()) {
        return;
    }

    std::vector<xcb_window_t> windows;
    windows.push_back(rootInfo()->supportWindow());

    auto const edges_wins = workspace()->edges->windows();
    windows.insert(windows.end(), edges_wins.begin(), edges_wins.end());

    Xcb::restackWindows(windows);
}

}
