/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "types.h"

#include "base/backend/wlroots.h"
#include "base/platform.h"
#include "main.h"
#include "render/backend/wlroots/backend.h"
#include "wayland_server.h"

#include <Wrapland/Client/xdg_shell.h>

#include <QtTest>
#include <memory>
#include <vector>

struct wlr_input_device;

namespace Wrapland
{
namespace Client
{
class SubSurface;
class Surface;
}
}

namespace KWin
{
namespace render::wayland
{
class compositor;
}
namespace win::wayland
{
class space;
class window;
}
namespace xwl
{
class xwayland;
}

class Toplevel;

class KWIN_EXPORT WaylandTestApplication : public ApplicationWaylandAbstract
{
    Q_OBJECT
public:
    wayland_base base;
    std::unique_ptr<WaylandServer> server;
    std::unique_ptr<xwl::xwayland> xwayland;
    std::unique_ptr<win::wayland::space> workspace;

    wlr_input_device* pointer{nullptr};
    wlr_input_device* keyboard{nullptr};
    wlr_input_device* touch{nullptr};

    std::vector<Test::client> clients;

    WaylandTestApplication(OperationMode mode,
                           std::string const& socket_name,
                           wayland_start_options flags,
                           int& argc,
                           char** argv);
    ~WaylandTestApplication() override;

    bool is_screen_locked() const override;

    wayland_base& get_base() override;
    WaylandServer* get_wayland_server() override;
    render::compositor* get_compositor() override;
    debug::console* create_debug_console() override;

    void start();

private:
    void handle_server_addons_created();
    void create_xwayland();

    std::unique_ptr<render::backend::wlroots::backend> render;
    std::unique_ptr<render::wayland::compositor> compositor;
};

namespace Test
{

KWIN_EXPORT WaylandTestApplication* app();

/**
 * Creates a Wayland Connection in a dedicated thread and creates various
 * client side objects which can be used to create windows.
 * @see destroy_wayland_connection
 */
KWIN_EXPORT void setup_wayland_connection(global_selection globals = {});

/**
 * Destroys the Wayland Connection created with @link{setup_wayland_connection}.
 * This can be called from cleanup in order to ensure that no Wayland Connection
 * leaks into the next test method.
 * @see setup_wayland_connection
 */
KWIN_EXPORT void destroy_wayland_connection();

KWIN_EXPORT client& get_client();
KWIN_EXPORT std::vector<client>& get_all_clients();

KWIN_EXPORT bool wait_for_wayland_pointer();
KWIN_EXPORT bool wait_for_wayland_touch();
KWIN_EXPORT bool wait_for_wayland_keyboard();

KWIN_EXPORT void flush_wayland_connection();
KWIN_EXPORT void flush_wayland_connection(client const& clt);

KWIN_EXPORT std::unique_ptr<Wrapland::Client::Surface> create_surface();
KWIN_EXPORT std::unique_ptr<Wrapland::Client::Surface> create_surface(client const& clt);
KWIN_EXPORT std::unique_ptr<Wrapland::Client::SubSurface>
create_subsurface(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                  std::unique_ptr<Wrapland::Client::Surface> const& parentSurface);

enum class CreationSetup {
    CreateOnly,
    CreateAndConfigure, /// commit and wait for the configure event, making this surface ready to
                        /// commit buffers
};

KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellToplevel>
create_xdg_shell_toplevel(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          CreationSetup = CreationSetup::CreateAndConfigure);
KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellToplevel>
create_xdg_shell_toplevel(client const& clt,
                          std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          CreationSetup = CreationSetup::CreateAndConfigure);

KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellPopup>
create_xdg_shell_popup(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                       std::unique_ptr<Wrapland::Client::XdgShellToplevel> const& parent_toplevel,
                       Wrapland::Client::XdgPositioner const& positioner,
                       CreationSetup = CreationSetup::CreateAndConfigure);
KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellPopup>
create_xdg_shell_popup(client const& clt,
                       std::unique_ptr<Wrapland::Client::Surface> const& surface,
                       std::unique_ptr<Wrapland::Client::XdgShellToplevel> const& parent_toplevel,
                       Wrapland::Client::XdgPositioner const& positioner,
                       CreationSetup = CreationSetup::CreateAndConfigure);

/**
 * Commits the XdgShellToplevel to the given surface, and waits for the configure event from the
 * compositor
 */
KWIN_EXPORT void
init_xdg_shell_toplevel(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        std::unique_ptr<Wrapland::Client::XdgShellToplevel> const& shell_toplevel);
KWIN_EXPORT void
init_xdg_shell_popup(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                     std::unique_ptr<Wrapland::Client::XdgShellPopup> const& popup);

/**
 * Creates a shared memory buffer of @p size in @p color and attaches it to the @p surface.
 * The @p surface gets damaged and committed, thus it's rendered.
 */
KWIN_EXPORT void render(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QSize& size,
                        const QColor& color,
                        const QImage::Format& format = QImage::Format_ARGB32_Premultiplied);
KWIN_EXPORT void render(client const& clt,
                        std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QSize& size,
                        const QColor& color,
                        const QImage::Format& format = QImage::Format_ARGB32_Premultiplied);

/**
 * Creates a shared memory buffer using the supplied image @p img and attaches it to the @p surface
 */
KWIN_EXPORT void render(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QImage& img);
KWIN_EXPORT void render(client const& clt,
                        std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QImage& img);

/**
 * Renders and then waits untill the new window is shown. Returns the created window.
 * If no window gets shown during @p timeout @c null is returned.
 */
KWIN_EXPORT win::wayland::window*
render_and_wait_for_shown(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          QSize const& size,
                          QColor const& color,
                          QImage::Format const& format = QImage::Format_ARGB32_Premultiplied,
                          int timeout = 5000);
KWIN_EXPORT win::wayland::window*
render_and_wait_for_shown(client const& clt,
                          std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          QSize const& size,
                          QColor const& color,
                          QImage::Format const& format = QImage::Format_ARGB32_Premultiplied,
                          int timeout = 5000);

/**
 * Waits for the @p client to be destroyed.
 */
KWIN_EXPORT bool wait_for_destroyed(KWin::Toplevel* window);

/**
 * Locks the screen and waits till the screen is locked.
 * @returns @c true if the screen could be locked, @c false otherwise
 */
KWIN_EXPORT void lock_screen();

/**
 * Unlocks the screen and waits till the screen is unlocked.
 * @returns @c true if the screen could be unlocked, @c false otherwise
 */
KWIN_EXPORT void unlock_screen();

KWIN_EXPORT void pointer_motion_absolute(QPointF const& position, uint32_t time);

KWIN_EXPORT void pointer_button_pressed(uint32_t button, uint32_t time);
KWIN_EXPORT void pointer_button_released(uint32_t button, uint32_t time);

KWIN_EXPORT void pointer_axis_horizontal(double delta, uint32_t time, int32_t discrete_delta);
KWIN_EXPORT void pointer_axis_vertical(double delta, uint32_t time, int32_t discrete_delta);

KWIN_EXPORT void keyboard_key_pressed(uint32_t key, uint32_t time);
KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time);

KWIN_EXPORT void keyboard_key_pressed(uint32_t key, uint32_t time, wlr_input_device* keyboard);
KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time, wlr_input_device* keyboard);

KWIN_EXPORT void touch_down(int32_t id, QPointF const& position, uint32_t time);
KWIN_EXPORT void touch_up(int32_t id, uint32_t time);
KWIN_EXPORT void touch_motion(int32_t id, QPointF const& position, uint32_t time);
KWIN_EXPORT void touch_cancel();

KWIN_EXPORT void prepare_app_env(std::string const& qpa_plugin_path);
KWIN_EXPORT void prepare_sys_env(std::string const& socket_name);
KWIN_EXPORT std::string create_socket_name(std::string base);

template<typename Test>
int create_test(std::string const& test_name, wayland_start_options flags, int argc, char* argv[])
{
    auto const socket_name = create_socket_name(test_name);
    auto mode = Application::OperationModeXwayland;
#ifdef NO_XWAYLAND
    mode = KWin::Application::OperationModeWaylandOnly;
#endif

    try {
        prepare_app_env(argv[0]);
        auto app = WaylandTestApplication(mode, socket_name, flags, argc, argv);
        prepare_sys_env(socket_name);
        Test test;
        return QTest::qExec(&test, argc, argv);
    } catch (std::exception const&) {
        ::exit(1);
    }
}

}
}

#define WAYLANDTEST_MAIN_FLAGS(Tester, flags)                                                      \
    int main(int argc, char* argv[])                                                               \
    {                                                                                              \
        return KWin::Test::create_test<Tester>(#Tester, flags, argc, argv);                        \
    }

#define WAYLANDTEST_MAIN(Tester) WAYLANDTEST_MAIN_FLAGS(Tester, KWin::wayland_start_options::none)
