/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "render/platform.h"

namespace KWin::base
{

platform::platform()
    : screens{*this}
{
}

platform::~platform() = default;

clockid_t platform::get_clockid() const
{
    return CLOCK_MONOTONIC;
}

}
