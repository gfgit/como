/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "keyboard.h"
#include "pointer.h"
#include "switch.h"
#include "touch.h"

#include "input/logging.h"

#include <cassert>

namespace KWin::input::backend::wlroots
{

void handle_device(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    base::event_receiver<platform>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto input = event_receiver_struct->receiver;

    auto device = reinterpret_cast<wlr_input_device*>(data);

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        qCDebug(KWIN_INPUT) << "Keyboard device added:" << device->name;
        Q_EMIT input->keyboard_added(new keyboard(device, input));
        break;
    case WLR_INPUT_DEVICE_POINTER:
        qCDebug(KWIN_INPUT) << "Pointer device added:" << device->name;
        Q_EMIT input->pointer_added(new pointer(device, input));
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        qCDebug(KWIN_INPUT) << "Switch device added:" << device->name;
        Q_EMIT input->switch_added(new switch_device(device, input));
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        qCDebug(KWIN_INPUT) << "Touch device added:" << device->name;
        Q_EMIT input->touch_added(new touch(device, input));
        break;
    default:
        // TODO(romangg): Handle other device types.
        qCDebug(KWIN_INPUT) << "Device type unhandled.";
    }
}

platform::platform(base::wayland::platform const& base)
    : wayland::platform(base)
{
    add_device.receiver = this;
    add_device.event.notify = handle_device;

    auto wlroots_base = dynamic_cast<base::backend::wlroots const*>(&base);
    assert(wlroots_base);
    wl_signal_add(&wlroots_base->backend->events.new_input, &add_device.event);
}

}
