/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

namespace KWin::win::x11
{

class screen_edges_filter : public base::x11::event_filter
{
public:
    screen_edges_filter();

    bool event(xcb_generic_event_t* event) override;
};

}