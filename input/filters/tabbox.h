/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event.h"
#include "input/event_filter.h"

namespace KWin::input
{

class tabbox_filter : public event_filter
{
public:
    bool key(key_event const& event) override;
    bool key_repeat(key_event const& event) override;

    bool button(button_event const& event) override;
    bool motion(motion_event const& event) override;
    bool axis(axis_event const& event) override;
};

}
