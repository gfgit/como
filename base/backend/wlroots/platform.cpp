/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "output.h"

#include "base/wayland/output_helpers.h"
#include "config-kwin.h"
#include "main.h"
#include "render/backend/wlroots/egl_output.h"
#include "render/backend/wlroots/output.h"
#include "render/backend/wlroots/platform.h"
#include "render/wayland/compositor.h"
#include "wayland_logging.h"
#include "wayland_server.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/drm_lease_v1.h>
#include <stdexcept>

namespace KWin::base::backend::wlroots
{

static auto align_horizontal{false};

static void handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    event_receiver<platform>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto wlr = event_receiver_struct->receiver;

    wlr->backend = nullptr;
}

void add_new_output(wlroots::platform& platform, wlr_output* native)
{
    auto const screens_width = std::max(platform.screens.size().width(), 0);

#if HAVE_WLR_OUTPUT_INIT_RENDER
    auto& render = static_cast<render::backend::wlroots::platform&>(*platform.render);
    wlr_output_init_render(native, render.allocator, render.renderer);
#endif

    if (!wl_list_empty(&native->modes)) {
        auto mode = wlr_output_preferred_mode(native);
        wlr_output_set_mode(native, mode);
        wlr_output_enable(native, true);
        if (!wlr_output_test(native)) {
            throw std::runtime_error("wlr_output_test failed");
        }
        if (!wlr_output_commit(native)) {
            throw std::runtime_error("wlr_output_commit failed");
        }
    }

    auto output = new wlroots::output(native, &platform);

    platform.all_outputs.push_back(output);
    platform.outputs.push_back(output);

    Q_EMIT platform.output_added(output);

    if (align_horizontal) {
        auto shifted_geo = output->geometry();
        shifted_geo.moveLeft(screens_width);
        output->force_geometry(shifted_geo);
    }

    platform.screens.updateAll();
}

void handle_new_output(struct wl_listener* listener, void* data)
{
    base::event_receiver<wlroots::platform>* new_output_struct
        = wl_container_of(listener, new_output_struct, event);
    auto platform = new_output_struct->receiver;
    auto native = reinterpret_cast<wlr_output*>(data);

    try {
        add_new_output(*platform, native);
    } catch (std::runtime_error const& e) {
        qCWarning(KWIN_WL) << "Adding new output" << native->name << "failed:" << e.what();
    }
}

platform::platform(Wrapland::Server::Display* display)
    : platform(wlr_backend_autocreate(display->native()))
{
}

platform::platform(wlr_backend* backend)
    : destroyed{std::make_unique<event_receiver<platform>>()}
    , new_output{std::make_unique<event_receiver<platform>>()}
{
    align_horizontal = qgetenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL") == QByteArrayLiteral("1");

    // TODO(romangg): Make this dependent on KWIN_WL debug verbosity.
    wlr_log_init(WLR_DEBUG, nullptr);

    this->backend = backend;

    destroyed->receiver = this;
    destroyed->event.notify = handle_destroy;
    wl_signal_add(&backend->events.destroy, &destroyed->event);

    new_output->receiver = this;
    new_output->event.notify = handle_new_output;
    wl_signal_add(&backend->events.new_output, &new_output->event);
}

platform::platform(platform&& other) noexcept
{
    *this = std::move(other);
}

platform& platform::operator=(platform&& other) noexcept
{
    backend = other.backend;
    destroyed = std::move(other.destroyed);
    destroyed->receiver = this;
    new_output = std::move(other.new_output);
    new_output->receiver = this;
    other.backend = nullptr;
    return *this;
}

platform::~platform()
{
    for (auto output : all_outputs) {
        static_cast<wlroots::output*>(output)->platform = nullptr;
        delete output;
    }
    if (backend) {
        wlr_backend_destroy(backend);
    }
}

wlr_session* platform::session() const
{
    return wlr_backend_get_session(backend);
}

clockid_t platform::get_clockid() const
{
    return wlr_backend_get_presentation_clock(backend);
}

#if HAVE_WLR_DRM_LEASE
struct outputs_array_wrap {
    outputs_array_wrap(size_t size)
        : size{size}
    {
        data = new wlr_output*[size];
    }
    ~outputs_array_wrap()
    {
        delete[] data;
    }
    wlr_output** data{nullptr};
    size_t size;
};

void process_drm_leased(wlroots::platform& platform, Wrapland::Server::drm_lease_v1* lease)
{
    std::vector<render::backend::wlroots::output*> outputs;

    qCDebug(KWIN_WL) << "Client tries to lease DRM resources.";

    if (lease->connectors().empty()) {
        qCDebug(KWIN_WL) << "Lease request has no connectors specified.";
        throw;
    }

    for (auto& con : lease->connectors()) {
        auto out = static_cast<output*>(wayland::find_output(platform, con->output()));
        assert(out);
        outputs.push_back(&static_cast<render::backend::wlroots::output&>(*out->render));
    }

    auto outputs_array = outputs_array_wrap(outputs.size());

    size_t i{0};
    for (auto& out : outputs) {
        out->egl->cleanup_framebuffer();
        outputs_array.data[i] = static_cast<output&>(out->base).native;
        i++;
    }

    auto wlr_lease = wlr_drm_create_lease(outputs_array.data, outputs_array.size, nullptr);
    if (!wlr_lease) {
        qCWarning(KWIN_WL) << "Error in wlroots backend on lease creation.";
        for (auto& out : outputs) {
            out->egl->reset_framebuffer();
        }
        throw;
    }

    auto compositor
        = static_cast<render::backend::wlroots::platform&>(*platform.render).compositor.get();

    QObject::connect(lease,
                     &Wrapland::Server::drm_lease_v1::resourceDestroyed,
                     &platform,
                     [compositor, wlr_lease] {
                         wlr_drm_lease_terminate(wlr_lease);
                         static_cast<render::wayland::compositor*>(compositor)->unlock();
                     });

    static_cast<render::wayland::compositor*>(compositor)->lock();
    lease->grant(wlr_lease->fd);
    qCDebug(KWIN_WL) << "DRM resources have been leased to client";
}

void platform::setup_drm_leasing()
{
    auto drm_backend = get_drm_backend(backend);
    if (!drm_backend) {
        return;
    }

    auto server = waylandServer();
    server->createDrmLeaseDevice();

    connect(server->drm_lease_device(),
            &Wrapland::Server::drm_lease_device_v1::needs_new_client_fd,
            this,
            [device = server->drm_lease_device(), drm_backend] {
                // TODO(romangg): wait in case not DRM master at the moment.
                auto fd = wlr_drm_backend_get_non_master_fd(drm_backend);
                device->update_fd(fd);
            });
    connect(server->drm_lease_device(),
            &Wrapland::Server::drm_lease_device_v1::leased,
            this,
            [this](auto lease) {
                try {
                    process_drm_leased(*this, lease);
                } catch (...) {
                    qCWarning(KWIN_WL) << "Creating lease failed.";
                    lease->finish();
                }
            });
}
#else
void platform::setup_drm_leasing()
{
}
#endif

}
