/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "helpers.h"

#include "base/backend/wlroots/platform.h"
#include "base/platform.h"
#include "main.h"
#include "render/backend/wlroots/backend.h"
#include "wayland_server.h"

#include <memory>
#include <vector>

struct wlr_input_device;

namespace KWin
{
namespace render::wayland
{
class compositor;
}
namespace win::wayland
{
class space;
}
namespace xwl
{
class xwayland;
}

class KWIN_EXPORT WaylandTestApplication : public ApplicationWaylandAbstract
{
    Q_OBJECT
public:
    std::unique_ptr<WaylandServer> server;
    base::backend::wlroots::platform base;
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

    base::wayland::platform& get_base() override;
    WaylandServer* get_wayland_server() override;
    render::compositor* get_compositor() override;
    debug::console* create_debug_console() override;

    void start();

    /// Sets @ref count horizontally lined up outputs with a default size of 1280x1024 at scale 1.
    void set_outputs(size_t count);
    void set_outputs(std::vector<QRect> const& geometries);
    void set_outputs(std::vector<Test::output> const& outputs);

private:
    void handle_server_addons_created();
    void create_xwayland();

    std::unique_ptr<render::backend::wlroots::backend> render;
    std::unique_ptr<render::wayland::compositor> compositor;
};

namespace Test
{

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
