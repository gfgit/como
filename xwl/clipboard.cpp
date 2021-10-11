/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#include "clipboard.h"

#include "selection_source.h"
#include "transfer.h"
#include "xwayland.h"

#include "wayland_server.h"
#include "workspace.h"

#include "win/x11/window.h"

#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/seat.h>

#include <xcb/xcb_event.h>
#include <xcb/xfixes.h>

#include <xwayland_logging.h>

namespace KWin
{
namespace Xwl
{

Clipboard::Clipboard(xcb_atom_t atom, x11_data const& x11)
{
    data = create_selection_data<server_source, internal_source>(atom, x11);

    register_x11_selection(this, QSize(10, 10));

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::selectionChanged,
                     data.qobject.get(),
                     [this] { handle_wl_selection_change(this); });
}

Clipboard::server_source* Clipboard::get_current_source() const
{
    return waylandServer()->seat()->selection();
}

std::function<void(Clipboard::server_source*)> Clipboard::get_selection_setter() const
{
    return [](server_source* src) { waylandServer()->seat()->setSelection(src); };
}

} // namespace Xwl
} // namespace KWin
