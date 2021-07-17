/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wlroots.h"

#include <Wrapland/Server/display.h>

namespace KWin::platform_base
{

void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    event_receiver<wlroots>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto wlr = event_receiver_struct->receiver;

    wlr->backend = nullptr;
}

wlroots::wlroots(Wrapland::Server::Display* display)
{
    wlr_log_init(WLR_DEBUG, nullptr);
    backend = wlr_backend_autocreate(display->native());
    init(backend);
}

wlroots::wlroots(wlr_backend* backend)
    : backend{backend}
{
    init(backend);
}

void wlroots::init(wlr_backend* backend)
{
    // TODO(romangg): Make this dependent on KWIN_WL debug verbosity.
    wlr_log_init(WLR_DEBUG, nullptr);

    this->backend = backend;

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;
    wl_signal_add(&backend->events.destroy, &destroyed.event);
}

wlroots::wlroots(wlroots&& other) noexcept
{
    *this = std::move(other);
    other.backend = nullptr;
}

wlroots& wlroots::operator=(wlroots&& other) noexcept
{
    backend = other.backend;
    destroyed = other.destroyed;
    other.backend = nullptr;
    return *this;
}

wlroots::~wlroots()
{
    if (backend) {
        wlr_backend_destroy(backend);
    }
}

}