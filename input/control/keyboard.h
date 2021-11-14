/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device.h"

#include "input/xkb.h"

#include <kwin_export.h>

namespace KWin::input::control
{

class KWIN_EXPORT keyboard : public device
{
    Q_OBJECT

public:
    explicit keyboard(platform* plat);

    virtual bool is_alpha_numeric_keyboard() const = 0;
    virtual void update_leds(input::keyboard_leds leds) = 0;
};

}
