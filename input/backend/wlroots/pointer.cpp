/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer.h"

#include "control/pointer.h"
#include "platform.h"
#include "utils.h"

extern "C" {
#include <wlr/backend/libinput.h>
}

namespace KWin::input::backend::wlroots
{

using er = base::event_receiver<pointer>;

static void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;

    pointer->backend = nullptr;

    if (pointer->plat) {
        remove_all(pointer->plat->pointers, pointer);
        Q_EMIT pointer->plat->pointer_removed(pointer);
    }
}

static void handle_motion(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_motion*>(data);

    auto event = motion_event{
        QPointF(wlr_event->delta_x, wlr_event->delta_y),
        QPointF(wlr_event->unaccel_dx, wlr_event->unaccel_dy),
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->motion(event);
}

static void handle_motion_absolute(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_motion_absolute*>(data);

    auto event = motion_absolute_event{
        QPointF(wlr_event->x, wlr_event->y),
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->motion_absolute(event);
}

static void handle_button(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_button*>(data);

    auto event = button_event{
        wlr_event->button,
        wlr_event->state == WLR_BUTTON_RELEASED ? button_state::released : button_state::pressed,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->button_changed(event);
}

static void handle_axis(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_axis*>(data);

    auto get_source = [](auto wlr_source) {
        switch (wlr_source) {
        case WLR_AXIS_SOURCE_WHEEL:
            return axis_source::wheel;
        case WLR_AXIS_SOURCE_FINGER:
            return axis_source::finger;
        case WLR_AXIS_SOURCE_CONTINUOUS:
            return axis_source::continuous;
        case WLR_AXIS_SOURCE_WHEEL_TILT:
            return axis_source::wheel_tilt;
        default:
            return axis_source::unknown;
        }
    };

    auto event = axis_event{
        get_source(wlr_event->source),
        static_cast<axis_orientation>(wlr_event->orientation),
        wlr_event->delta,
        wlr_event->delta_discrete,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->axis_changed(event);
}

static void handle_swipe_begin(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_swipe_begin*>(data);

    auto event = swipe_begin_event{
        wlr_event->fingers,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->swipe_begin(event);
}

static void handle_swipe_update(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_swipe_update*>(data);

    auto event = swipe_update_event{
        wlr_event->fingers,
        QPointF(wlr_event->dx, wlr_event->dy),
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->swipe_update(event);
}

static void handle_swipe_end(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_swipe_end*>(data);

    auto event = swipe_end_event{
        wlr_event->cancelled,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->swipe_end(event);
}

static void handle_pinch_begin(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_pinch_begin*>(data);

    auto event = pinch_begin_event{
        wlr_event->fingers,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->pinch_begin(event);
}

static void handle_pinch_update(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_pinch_update*>(data);

    auto event = pinch_update_event{
        wlr_event->fingers,
        QPointF(wlr_event->dx, wlr_event->dy),
        wlr_event->scale,
        wlr_event->rotation,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->pinch_update(event);
}

static void handle_pinch_end(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_pointer_pinch_end*>(data);

    auto event = pinch_end_event{
        wlr_event->cancelled,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->pinch_end(event);
}

pointer::pointer(wlr_input_device* dev, platform* plat)
    : input::pointer(plat)
{
    backend = dev->pointer;

    if (auto libinput = get_libinput_device(dev)) {
        control = new pointer_control(libinput, plat);
    }

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;
    wl_signal_add(&dev->events.destroy, &destroyed.event);

    motion_rec.receiver = this;
    motion_rec.event.notify = handle_motion;
    wl_signal_add(&backend->events.motion, &motion_rec.event);

    motion_absolute_rec.receiver = this;
    motion_absolute_rec.event.notify = handle_motion_absolute;
    wl_signal_add(&backend->events.motion_absolute, &motion_absolute_rec.event);

    button_rec.receiver = this;
    button_rec.event.notify = handle_button;
    wl_signal_add(&backend->events.button, &button_rec.event);

    axis_rec.receiver = this;
    axis_rec.event.notify = handle_axis;
    wl_signal_add(&backend->events.axis, &axis_rec.event);

    swipe_begin_rec.receiver = this;
    swipe_begin_rec.event.notify = handle_swipe_begin;
    wl_signal_add(&backend->events.swipe_begin, &swipe_begin_rec.event);

    swipe_update_rec.receiver = this;
    swipe_update_rec.event.notify = handle_swipe_update;
    wl_signal_add(&backend->events.swipe_update, &swipe_update_rec.event);

    swipe_end_rec.receiver = this;
    swipe_end_rec.event.notify = handle_swipe_end;
    wl_signal_add(&backend->events.swipe_end, &swipe_end_rec.event);

    pinch_begin_rec.receiver = this;
    pinch_begin_rec.event.notify = handle_pinch_begin;
    wl_signal_add(&backend->events.pinch_begin, &pinch_begin_rec.event);

    pinch_update_rec.receiver = this;
    pinch_update_rec.event.notify = handle_pinch_update;
    wl_signal_add(&backend->events.pinch_update, &pinch_update_rec.event);

    pinch_end_rec.receiver = this;
    pinch_end_rec.event.notify = handle_pinch_end;
    wl_signal_add(&backend->events.pinch_end, &pinch_end_rec.event);
}

}
