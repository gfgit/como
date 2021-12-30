/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "buffer.h"
#include "egl_backend.h"
#include "egl_output.h"
#include "output.h"
#include "wlr_helpers.h"

#include "base/backend/wlroots/output.h"
#include "base/wayland/output_helpers.h"
#include "input/wayland/platform.h"
#include "main.h"
#include "render/wayland/compositor.h"
#include "render/wayland/effects.h"
#include "render/wayland/egl.h"
#include "screens.h"
#include "wayland_server.h"

#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

output& get_output(std::unique_ptr<render::wayland::output>& output)
{
    return static_cast<wlroots::output&>(*output);
}

platform::platform(base::backend::wlroots::platform& base)
    : render::platform(base)
    , base{base}
{
}

platform::~platform()
{
    if (egl_display_to_terminate != EGL_NO_DISPLAY) {
        eglTerminate(egl_display_to_terminate);
    }
}

void platform::init()
{
    // TODO(romangg): Has to be here because in the integration tests base.backend is not yet
    //                available in the ctor. Can we change that?
#if HAVE_WLR_OUTPUT_INIT_RENDER
    renderer = wlr_renderer_autocreate(base.backend);
    allocator = wlr_allocator_autocreate(base.backend, renderer);
#endif

    init_drm_leasing();

    if (!wlr_backend_start(base.backend)) {
        throw std::exception();
    }

    base.screens.updateAll();
}

gl::backend* platform::createOpenGLBackend(render::compositor& /*compositor*/)
{
    if (!egl) {
        egl = std::make_unique<egl_backend>(
            *this, base::backend::wlroots::get_headless_backend(base.backend));
    }
    return egl.get();
}

void platform::render_stop(bool on_shutdown)
{
    assert(egl);
    if (on_shutdown) {
        wayland::unbind_egl_display(*egl, egl->data);
        egl->tear_down();
    }
}

void platform::createEffectsHandler(render::compositor* compositor, render::scene* scene)
{
    new wayland::effects_handler_impl(compositor, scene);
}

QVector<CompositingType> platform::supportedCompositors() const
{
    if (selected_compositor != NoCompositing) {
        return {selected_compositor};
    }
    return QVector<CompositingType>{OpenGLCompositing};
}

void platform::init_drm_leasing()
{
#if HAVE_WLR_DRM_LEASE
    auto drm_backend = base::backend::wlroots::get_drm_backend(base.backend);
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
                    process_drm_leased(lease);
                } catch (...) {
                    qCWarning(KWIN_WL) << "Creating lease failed.";
                    lease->finish();
                }
            });
#endif
}

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

void platform::process_drm_leased([[maybe_unused]] Wrapland::Server::drm_lease_v1* lease)
{
#if HAVE_WLR_DRM_LEASE
    std::vector<output*> outputs;

    qCDebug(KWIN_WL) << "Client tries to lease DRM resources.";

    if (lease->connectors().empty()) {
        qCDebug(KWIN_WL) << "Lease request has no connectors specified.";
        throw;
    }

    for (auto& con : lease->connectors()) {
        auto out = static_cast<base::backend::wlroots::output*>(
            base::wayland::find_output(base, con->output()));
        assert(out);
        outputs.push_back(&get_output(out->render));
    }

    auto outputs_array = outputs_array_wrap(outputs.size());

    size_t i{0};
    for (auto& out : outputs) {
        out->egl->cleanup_framebuffer();
        outputs_array.data[i] = static_cast<base::backend::wlroots::output&>(out->base).native;
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

    QObject::connect(
        lease, &Wrapland::Server::drm_lease_v1::resourceDestroyed, this, [this, wlr_lease] {
            wlr_drm_lease_terminate(wlr_lease);
            static_cast<render::wayland::compositor*>(compositor.get())->unlock();
        });

    static_cast<render::wayland::compositor*>(compositor.get())->lock();
    lease->grant(wlr_lease->fd);
    qCDebug(KWIN_WL) << "DRM resources have been leased to client";
#endif
}

}
