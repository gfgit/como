/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform.h"
#include "platform/wlroots.h"

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/backend/multi.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Dev>
inline libinput_device* get_libinput_device(Dev dev)
{
    auto casted_dev = reinterpret_cast<wlr_input_device*>(dev);
    if (wlr_input_device_is_libinput(casted_dev)) {
        return wlr_libinput_get_device_handle(casted_dev);
    }
    return nullptr;
}

class platform : public input::platform
{
    Q_OBJECT
public:
    platform(platform_base::wlroots* base);
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform();

private:
    event_receiver<platform> add_device;
    platform_base::wlroots* base;
};

}