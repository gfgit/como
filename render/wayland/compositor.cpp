/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor.h"

#include "output.h"
#include "presentation.h"
#include "utils.h"

#include "base/backend/wlroots/output.h"
#include "base/wayland/platform.h"
#include "render/backend/wlroots/output.h"
#include "render/cursor.h"
#include "render/gl/scene.h"
#include "render/platform.h"
#include "render/qpainter/scene.h"
#include "render/scene.h"
#include "wayland_server.h"
#include "win/scene.h"
#include "workspace.h"

#include "wayland_logging.h"

namespace KWin::render::wayland
{

base::wayland::platform& get_platform(base::platform& platform)
{
    return static_cast<base::wayland::platform&>(platform);
}

base::backend::wlroots::output* get_output(base::output* out)
{
    return static_cast<base::backend::wlroots::output*>(out);
}

void compositor::addRepaint(QRegion const& region)
{
    if (locked) {
        return;
    }
    for (auto& output : get_platform(platform.base).outputs) {
        get_output(output)->render->add_repaint(region);
    }
}

void compositor::check_idle()
{
    for (auto& output : get_platform(platform.base).outputs) {
        if (!get_output(output)->render->idle) {
            return;
        }
    }
    scene()->idle();
}

compositor::compositor(render::platform& platform)
    : render::compositor(platform)
    , presentation(new render::wayland::presentation(this))
{
    if (!presentation->init_clock(platform.base.get_clockid())) {
        qCCritical(KWIN_WL) << "Presentation clock failed. Exit.";
        qApp->quit();
    }

    // For now we use the software cursor as our wlroots backend does not support yet a hardware
    // cursor.
    software_cursor->set_enabled(true);

    connect(kwinApp(),
            &Application::x11ConnectionAboutToBeDestroyed,
            this,
            &compositor::destroyCompositorSelection);

    connect(&platform.base, &base::platform::output_removed, this, [this](auto output) {
        if (auto workspace = Workspace::self()) {
            for (auto& win : workspace->windows()) {
                remove_all(win->repaint_outputs, output);
            }
        }
    });

    connect(workspace(), &Workspace::destroyed, this, [this] {
        for (auto& output : get_platform(this->platform.base).outputs) {
            get_output(output)->render->delay_timer.stop();
        }
    });

    start();
}

compositor::~compositor() = default;

void compositor::schedule_repaint(Toplevel* window)
{
    if (locked) {
        return;
    }

    for (auto& output : get_platform(this->platform.base).outputs) {
        if (!win::visible_rect(window).intersected(output->geometry()).isEmpty()) {
            get_output(output)->render->set_delay_timer();
        }
    }
}

void compositor::schedule_frame_callback(Toplevel* window)
{
    if (locked) {
        return;
    }

    if (auto max_out = static_cast<base::wayland::output*>(max_coverage_output(window))) {
        get_output(max_out)->render->request_frame(window);
    }
}

void compositor::toggleCompositing()
{
    // For the shortcut. Not possible on Wayland because we always composite.
}

bool compositor::is_locked() const
{
    return locked > 0;
}

void compositor::lock()
{
    locked++;
}

void compositor::unlock()
{
    assert(locked > 0);
    locked--;

    if (!locked) {
        addRepaintFull();
    }
}

void compositor::start()
{
    if (!render::compositor::setupStart()) {
        // Internal setup failed, abort.
        return;
    }

    if (Workspace::self()) {
        startupWithWorkspace();
    } else {
        connect(kwinApp(), &Application::workspaceCreated, this, &compositor::startupWithWorkspace);
    }
}

render::scene* compositor::create_scene(QVector<CompositingType> const& support)
{
    for (auto type : support) {
        if (type == OpenGLCompositing) {
            qCDebug(KWIN_WL) << "Creating OpenGL scene.";
            return gl::create_scene(*this);
        }
        if (type == QPainterCompositing) {
            qCDebug(KWIN_WL) << "Creating QPainter scene.";
            return qpainter::create_scene(*this);
        }
    }
    return nullptr;
}

std::deque<Toplevel*> compositor::performCompositing()
{
    for (auto& output : get_platform(platform.base).outputs) {
        get_output(output)->render->run();
    }

    return std::deque<Toplevel*>();
}

}
