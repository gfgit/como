/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wayland_server.h"

#include "base/wayland/idle_inhibition.h"
#include "platform.h"
#include "screens.h"
#include "service_utils.h"
#include "workspace.h"

#include "win/wayland/layer_shell.h"
#include "win/wayland/subsurface.h"
#include "win/wayland/window.h"
#include "win/wayland/xdg_activation.h"
#include "win/wayland/xdg_shell.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/client.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/filtered_display.h>
#include <Wrapland/Server/globals.h>
#include <Wrapland/Server/surface.h>

#include <QCryptographicHash>
#include <QFile>
#include <QThread>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <KScreenLocker/KsldApp>

using namespace Wrapland::Server;

namespace KWin
{

class KWinDisplay : public Wrapland::Server::FilteredDisplay
{
public:
    KWinDisplay()
        : Wrapland::Server::FilteredDisplay()
    {
    }

    static QByteArray sha256(const QString& fileName)
    {
        QFile f(fileName);
        if (f.open(QFile::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&f)) {
                return hash.result();
            }
        }
        return QByteArray();
    }

    bool isTrustedOrigin(Wrapland::Server::Client* client) const
    {
        const auto fullPathSha = sha256(QString::fromStdString(client->executablePath()));
        const auto localSha = sha256(QLatin1String("/proc/") + QString::number(client->processId())
                                     + QLatin1String("/exe"));
        const bool trusted = !localSha.isEmpty() && fullPathSha == localSha;

        if (!trusted) {
            qCWarning(KWIN_CORE) << "Could not trust" << client->executablePath().c_str() << "sha"
                                 << localSha << fullPathSha;
        }

        return trusted;
    }

    QStringList fetchRequestedInterfaces(Wrapland::Server::Client* client) const
    {
        return KWin::fetchRequestedInterfaces(client->executablePath().c_str());
    }

    const QSet<QByteArray> interfacesBlackList = {"org_kde_kwin_remote_access_manager",
                                                  "org_kde_plasma_window_management",
                                                  "org_kde_kwin_fake_input",
                                                  "org_kde_kwin_keystate"};
    QSet<QString> m_reported;

    bool allowInterface(Wrapland::Server::Client* client, const QByteArray& interfaceName) override
    {
        if (client->processId() == getpid()) {
            return true;
        }

        if (!interfacesBlackList.contains(interfaceName)) {
            return true;
        }

        if (client->executablePath().empty()) {
            qCDebug(KWIN_CORE) << "Could not identify process with pid" << client->processId();
            return false;
        }

        {
            auto requestedInterfaces = client->property("requestedInterfaces");
            if (requestedInterfaces.isNull()) {
                requestedInterfaces = fetchRequestedInterfaces(client);
                client->setProperty("requestedInterfaces", requestedInterfaces);
            }
            if (!requestedInterfaces.toStringList().contains(QString::fromUtf8(interfaceName))) {
                if (KWIN_CORE().isDebugEnabled()) {
                    const QString id = QString::fromStdString(client->executablePath())
                        + QLatin1Char('|') + QString::fromUtf8(interfaceName);
                    if (!m_reported.contains({id})) {
                        m_reported.insert(id);
                        qCDebug(KWIN_CORE)
                            << "Interface" << interfaceName << "not in X-KDE-Wayland-Interfaces of"
                            << client->executablePath().c_str();
                    }
                }
                return false;
            }
        }

        {
            auto trustedOrigin = client->property("isPrivileged");
            if (trustedOrigin.isNull()) {
                trustedOrigin = isTrustedOrigin(client);
                client->setProperty("isPrivileged", trustedOrigin);
            }

            if (!trustedOrigin.toBool()) {
                return false;
            }
        }
        qCDebug(KWIN_CORE) << "authorized" << client->executablePath().c_str() << interfaceName;
        return true;
    }
};

WaylandServer::WaylandServer(InitializationFlags flags)
    : m_display(std::make_unique<KWinDisplay>())
    , globals{std::make_unique<Wrapland::Server::globals>()}
    , m_initFlags{flags}

{
    qRegisterMetaType<Wrapland::Server::Output::DpmsMode>();
}

WaylandServer::WaylandServer(std::string const& socket, InitializationFlags flags)
    : WaylandServer(flags)

{
    m_display->set_socket_name(socket);
    m_display->start(Wrapland::Server::Display::StartMode::ConnectToSocket);
    create_globals();
}

WaylandServer::WaylandServer(int socket_fd, InitializationFlags flags)
    : WaylandServer(flags)

{
    m_display->add_socket_fd(socket_fd);
    m_display->start(Wrapland::Server::Display::StartMode::ConnectClientsOnly);
    create_globals();
}

WaylandServer::~WaylandServer()
{
    destroyInputMethodConnection();
}

void WaylandServer::destroyInternalConnection()
{
    emit terminatingInternalClientConnection();
    if (m_internalConnection.client) {
        // delete all connections hold by plugins like e.g. widget style
        const auto connections = Wrapland::Client::ConnectionThread::connections();
        for (auto c : connections) {
            if (c == m_internalConnection.client) {
                continue;
            }
            Q_EMIT c->establishedChanged(false);
        }

        delete m_internalConnection.registry;
        delete m_internalConnection.compositor;
        delete m_internalConnection.seat;
        delete m_internalConnection.shm;
        dispatch();
        delete m_internalConnection.queue;
        m_internalConnection.client->deleteLater();
        m_internalConnection.clientThread->quit();
        m_internalConnection.clientThread->wait();
        delete m_internalConnection.clientThread;
        m_internalConnection.client = nullptr;
        m_internalConnection.server->destroy();
        m_internalConnection.server = nullptr;
    }
}

void WaylandServer::terminateClientConnections()
{
    destroyInternalConnection();
    destroyInputMethodConnection();

    for (auto client : m_display->clients()) {
        client->destroy();
    }
}

void WaylandServer::create_globals()
{
    if (!m_display->running()) {
        qCCritical(KWIN_CORE) << "Wayland server failed to start.";
        throw std::exception();
    }

    globals->compositor = m_display->createCompositor();

    connect(compositor(),
            &Wrapland::Server::Compositor::surfaceCreated,
            this,
            [this](Surface* surface) {
                // check whether we have a Toplevel with the Surface's id
                Workspace* ws = Workspace::self();
                if (!ws) {
                    // it's possible that a Surface gets created before Workspace is created
                    return;
                }
                if (surface->client() != xWaylandConnection()) {
                    // setting surface is only relevat for Xwayland clients
                    return;
                }
                auto check = [surface](const Toplevel* t) {
                    // Match on surface id and exclude windows already having a surface. This way we
                    // only find Xwayland toplevels. Wayland native windows always have a surface.
                    return t->surfaceId() == surface->id() && !t->surface();
                };
                if (Toplevel* t = ws->findToplevel(check)) {
                    t->setSurface(surface);
                }
            });

    globals->xdg_shell = m_display->createXdgShell();
    connect(xdg_shell(), &XdgShell::toplevelCreated, this, [this](XdgShellToplevel* toplevel) {
        if (!Workspace::self()) {
            // it's possible that a Surface gets created before Workspace is created
            return;
        }
        if (toplevel->client() == m_screenLockerClientConnection) {
            ScreenLocker::KSldApp::self()->lockScreenShown();
        }
        auto window = win::wayland::create_toplevel_window(toplevel);

        // TODO: Also relevant for popups?
        auto it = std::find_if(
            m_plasmaShellSurfaces.begin(),
            m_plasmaShellSurfaces.end(),
            [window](auto shell_surface) { return window->surface() == shell_surface->surface(); });
        if (it != m_plasmaShellSurfaces.end()) {
            win::wayland::install_plasma_shell_surface(window, *it);
            m_plasmaShellSurfaces.erase(it);
        }

        if (auto menu = globals->appmenu_manager->appmenuForSurface(window->surface())) {
            win::wayland::install_appmenu(window, menu);
        }
        if (auto palette = globals->server_side_decoration_palette_manager->paletteForSurface(
                toplevel->surface()->surface())) {
            win::wayland::install_palette(window, palette);
        }

        windows.push_back(window);

        if (window->readyForPainting()) {
            Q_EMIT window_added(window);
        } else {
            connect(window, &win::wayland::window::windowShown, this, &WaylandServer::window_shown);
        }

        // not directly connected as the connection is tied to client instead of this
        connect(
            globals->xdg_foreign.get(),
            &Wrapland::Server::XdgForeign::parentChanged,
            window,
            [this]([[maybe_unused]] Wrapland::Server::Surface* parent,
                   Wrapland::Server::Surface* child) { Q_EMIT foreignTransientChanged(child); });
    });
    connect(xdg_shell(), &XdgShell::popupCreated, this, [this](XdgShellPopup* popup) {
        if (!Workspace::self()) {
            // it's possible that a Surface gets created before Workspace is created
            return;
        }
        auto window = win::wayland::create_popup_window(popup);
        windows.push_back(window);

        if (window->readyForPainting()) {
            Q_EMIT window_added(window);
        } else {
            connect(window, &win::wayland::window::windowShown, this, &WaylandServer::window_shown);
        }
    });

    globals->xdg_decoration_manager = m_display->createXdgDecorationManager(xdg_shell());
    connect(globals->xdg_decoration_manager.get(),
            &XdgDecorationManager::decorationCreated,
            this,
            [this](XdgDecoration* deco) {
                if (auto win = find_window(deco->toplevel()->surface()->surface())) {
                    win::wayland::install_deco(win, deco);
                }
            });

    m_display->createShm();
    globals->seats.push_back(m_display->createSeat());

    globals->pointer_gestures_v1 = m_display->createPointerGestures();
    globals->pointer_constraints_v1 = m_display->createPointerConstraints();
    globals->data_device_manager = m_display->createDataDeviceManager();
    globals->primary_selection_device_manager = m_display->createPrimarySelectionDeviceManager();
    globals->data_control_manager_v1 = m_display->create_data_control_manager_v1();
    globals->kde_idle = m_display->createIdle();

    auto idleInhibition = new base::wayland::idle_inhibition(globals->kde_idle.get());
    connect(this,
            &WaylandServer::window_added,
            idleInhibition,
            &base::wayland::idle_inhibition::register_window);
    globals->idle_inhibit_manager_v1 = m_display->createIdleInhibitManager();

    globals->plasma_shell = m_display->createPlasmaShell();
    connect(globals->plasma_shell.get(),
            &PlasmaShell::surfaceCreated,
            [this](PlasmaShellSurface* surface) {
                if (auto win = find_window(surface->surface())) {
                    assert(win->toplevel || win->popup || win->layer_surface);
                    win::wayland::install_plasma_shell_surface(win, surface);
                } else {
                    m_plasmaShellSurfaces << surface;
                    connect(surface, &QObject::destroyed, this, [this, surface] {
                        m_plasmaShellSurfaces.removeOne(surface);
                    });
                }
            });
    globals->appmenu_manager = m_display->createAppmenuManager();
    connect(
        globals->appmenu_manager.get(), &AppmenuManager::appmenuCreated, [this](Appmenu* appMenu) {
            if (auto win = find_window(appMenu->surface())) {
                if (win->control) {
                    // Need to check that as plasma-integration creates them blindly even for
                    // xdg-shell popups.
                    win::wayland::install_appmenu(win, appMenu);
                }
            }
        });

    globals->server_side_decoration_palette_manager
        = m_display->createServerSideDecorationPaletteManager();
    connect(globals->server_side_decoration_palette_manager.get(),
            &ServerSideDecorationPaletteManager::paletteCreated,
            [this](ServerSideDecorationPalette* palette) {
                if (auto win = find_window(palette->surface())) {
                    if (win->control) {
                        win::wayland::install_palette(win, palette);
                    }
                }
            });

    globals->plasma_window_manager = m_display->createPlasmaWindowManager();
    globals->plasma_window_manager->setShowingDesktopState(
        PlasmaWindowManager::ShowingDesktopState::Disabled);
    connect(globals->plasma_window_manager.get(),
            &PlasmaWindowManager::requestChangeShowingDesktop,
            this,
            [](PlasmaWindowManager::ShowingDesktopState state) {
                if (!workspace()) {
                    return;
                }
                bool set = false;
                switch (state) {
                case PlasmaWindowManager::ShowingDesktopState::Disabled:
                    set = false;
                    break;
                case PlasmaWindowManager::ShowingDesktopState::Enabled:
                    set = true;
                    break;
                default:
                    Q_UNREACHABLE();
                    break;
                }
                if (set == workspace()->showingDesktop()) {
                    return;
                }
                workspace()->setShowingDesktop(set);
            });

    globals->plasma_virtual_desktop_manager = m_display->createPlasmaVirtualDesktopManager();
    globals->plasma_window_manager->setVirtualDesktopManager(virtual_desktop_management());

    globals->shadow_manager = m_display->createShadowManager();
    globals->dpms_manager = m_display->createDpmsManager();

    globals->output_management_v1 = m_display->createOutputManagementV1();
    connect(globals->output_management_v1.get(),
            &OutputManagementV1::configurationChangeRequested,
            this,
            [](Wrapland::Server::OutputConfigurationV1* config) {
                kwinApp()->platform->requestOutputsChange(config);
            });

    globals->subcompositor = m_display->createSubCompositor();
    connect(subcompositor(),
            &Wrapland::Server::Subcompositor::subsurfaceCreated,
            this,
            [this](auto subsurface) {
                auto window = new win::wayland::window(subsurface->surface());

                windows.push_back(window);
                QObject::connect(subsurface,
                                 &Wrapland::Server::Subsurface::resourceDestroyed,
                                 this,
                                 [this, window] { remove_all(windows, window); });

                win::wayland::assign_subsurface_role(window);

                for (auto& win : windows) {
                    if (win->surface() == subsurface->parentSurface()) {
                        win::wayland::set_subsurface_parent(window, win);
                        if (window->readyForPainting()) {
                            Q_EMIT window_added(window);
                            adopt_transient_children(window);
                            return;
                        }
                        break;
                    }
                }
                // Must wait till a parent is mapped and subsurface is ready for painting.
                connect(
                    window, &win::wayland::window::windowShown, this, &WaylandServer::window_shown);
            });

    globals->layer_shell_v1 = m_display->createLayerShellV1();
    connect(layer_shell(),
            &Wrapland::Server::LayerShellV1::surface_created,
            this,
            [this](auto layer_surface) {
                auto window = new win::wayland::window(layer_surface->surface());
                if (layer_surface->surface()->client() == m_screenLockerClientConnection) {
                    ScreenLocker::KSldApp::self()->lockScreenShown();
                }

                windows.push_back(window);
                QObject::connect(layer_surface,
                                 &Wrapland::Server::LayerSurfaceV1::resourceDestroyed,
                                 this,
                                 [this, window] { remove_all(windows, window); });

                win::wayland::assign_layer_surface_role(window, layer_surface);

                if (window->readyForPainting()) {
                    Q_EMIT window_added(window);
                } else {
                    connect(window,
                            &win::wayland::window::windowShown,
                            this,
                            &WaylandServer::window_shown);
                }
            });

    globals->xdg_activation_v1 = m_display->createXdgActivationV1();
    globals->xdg_foreign = m_display->createXdgForeign();

    globals->key_state = m_display->createKeyState();
    globals->viewporter = m_display->createViewporter();

    globals->relative_pointer_manager_v1 = m_display->createRelativePointerManager();
}

Wrapland::Server::Display* WaylandServer::display() const
{
    return m_display.get();
}

Wrapland::Server::Compositor* WaylandServer::compositor() const
{
    return globals->compositor.get();
}

Wrapland::Server::Subcompositor* WaylandServer::subcompositor() const
{
    return globals->subcompositor.get();
}

Wrapland::Server::LinuxDmabufV1* WaylandServer::linux_dmabuf()
{
    if (!globals->linux_dmabuf_v1) {
        globals->linux_dmabuf_v1 = m_display->createLinuxDmabuf();
    }
    return globals->linux_dmabuf_v1.get();
}

Wrapland::Server::Viewporter* WaylandServer::viewporter() const
{
    return globals->viewporter.get();
}

Wrapland::Server::PresentationManager* WaylandServer::presentation_manager() const
{
    return globals->presentation_manager.get();
}

Wrapland::Server::Seat* WaylandServer::seat() const
{
    if (globals->seats.empty()) {
        return nullptr;
    }
    return globals->seats.front().get();
}

Wrapland::Server::data_device_manager* WaylandServer::data_device_manager() const
{
    return globals->data_device_manager.get();
}

Wrapland::Server::primary_selection_device_manager*
WaylandServer::primary_selection_device_manager() const
{
    return globals->primary_selection_device_manager.get();
}

Wrapland::Server::XdgShell* WaylandServer::xdg_shell() const
{
    return globals->xdg_shell.get();
}

Wrapland::Server::XdgActivationV1* WaylandServer::xdg_activation() const
{
    return globals->xdg_activation_v1.get();
}

Wrapland::Server::PlasmaVirtualDesktopManager* WaylandServer::virtual_desktop_management() const
{
    return globals->plasma_virtual_desktop_manager.get();
}

Wrapland::Server::LayerShellV1* WaylandServer::layer_shell() const
{
    return globals->layer_shell_v1.get();
}

Wrapland::Server::PlasmaWindowManager* WaylandServer::window_management() const
{
    return globals->plasma_window_manager.get();
}

Wrapland::Server::KdeIdle* WaylandServer::kde_idle() const
{
    return globals->kde_idle.get();
}

Wrapland::Server::drm_lease_device_v1* WaylandServer::drm_lease_device() const
{
    return globals->drm_lease_device_v1.get();
}

void WaylandServer::create_presentation_manager()
{
    Q_ASSERT(!globals->presentation_manager);
    globals->presentation_manager = m_display->createPresentationManager();
}

Surface* WaylandServer::findForeignParentForSurface(Surface* surface)
{
    return globals->xdg_foreign->parentOf(surface);
}

void WaylandServer::window_shown(Toplevel* window)
{
    disconnect(window, &Toplevel::windowShown, this, &WaylandServer::window_shown);
    Q_EMIT window_added(static_cast<win::wayland::window*>(window));
    adopt_transient_children(window);
}

void WaylandServer::adopt_transient_children(Toplevel* window)
{
    std::for_each(
        windows.cbegin(), windows.cend(), [&window](auto win) { win->checkTransient(window); });
}

void WaylandServer::initWorkspace()
{
    auto ws = workspace();

    VirtualDesktopManager::self()->setVirtualDesktopManagement(virtual_desktop_management());

    if (window_management()) {
        connect(ws, &Workspace::showingDesktopChanged, this, [this](bool set) {
            using namespace Wrapland::Server;
            window_management()->setShowingDesktopState(
                set ? PlasmaWindowManager::ShowingDesktopState::Enabled
                    : PlasmaWindowManager::ShowingDesktopState::Disabled);
        });
    }

    connect(xdg_activation(),
            &Wrapland::Server::XdgActivationV1::token_requested,
            ws,
            [ws](auto token) { win::wayland::xdg_activation_create_token(ws, token); });
    connect(xdg_activation(),
            &Wrapland::Server::XdgActivationV1::activate,
            ws,
            [ws, this](auto const& token, auto surface) {
                auto win = find_window(surface);
                if (!win) {
                    qCDebug(KWIN_CORE) << "No window found to xdg-activate" << surface;
                    return;
                }
                win::wayland::xdg_activation_activate(ws, win, token);
            });

    // For Xwayland windows
    QObject::connect(ws, &Workspace::surface_id_changed, this, [this](auto window, auto id) {
        if (auto surface = compositor()->getSurface(id, xWaylandConnection())) {
            window->setSurface(surface);
        }
    });
}

void WaylandServer::initScreenLocker()
{
    if (!hasScreenLockerIntegration()) {
        return;
    }

    auto* screenLockerApp = ScreenLocker::KSldApp::self();

    ScreenLocker::KSldApp::self()->setGreeterEnvironment(kwinApp()->processStartupEnvironment());
    ScreenLocker::KSldApp::self()->initialize();

    connect(ScreenLocker::KSldApp::self(),
            &ScreenLocker::KSldApp::aboutToLock,
            this,
            [this, screenLockerApp]() {
                if (m_screenLockerClientConnection) {
                    // Already sent data to KScreenLocker.
                    return;
                }
                int clientFd = createScreenLockerConnection();
                if (clientFd < 0) {
                    return;
                }
                ScreenLocker::KSldApp::self()->setWaylandFd(clientFd);

                for (auto* seat : m_display->seats()) {
                    connect(seat,
                            &Wrapland::Server::Seat::timestampChanged,
                            screenLockerApp,
                            &ScreenLocker::KSldApp::userActivity);
                }
            });

    connect(ScreenLocker::KSldApp::self(),
            &ScreenLocker::KSldApp::unlocked,
            this,
            [this, screenLockerApp]() {
                if (m_screenLockerClientConnection) {
                    m_screenLockerClientConnection->destroy();
                    delete m_screenLockerClientConnection;
                    m_screenLockerClientConnection = nullptr;
                }

                for (auto* seat : m_display->seats()) {
                    disconnect(seat,
                               &Wrapland::Server::Seat::timestampChanged,
                               screenLockerApp,
                               &ScreenLocker::KSldApp::userActivity);
                }
                ScreenLocker::KSldApp::self()->setWaylandFd(-1);
            });

    if (m_initFlags.testFlag(InitializationFlag::LockScreen)) {
        ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
    }

    Q_EMIT screenlocker_initialized();
}

WaylandServer::SocketPairConnection WaylandServer::createConnection()
{
    SocketPairConnection ret;
    int sx[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) < 0) {
        qCWarning(KWIN_CORE) << "Could not create socket";
        return ret;
    }
    ret.connection = m_display->createClient(sx[0]);
    ret.fd = sx[1];
    return ret;
}

int WaylandServer::createScreenLockerConnection()
{
    const auto socket = createConnection();
    if (!socket.connection) {
        return -1;
    }
    m_screenLockerClientConnection = socket.connection;
    connect(m_screenLockerClientConnection, &Wrapland::Server::Client::disconnected, this, [this] {
        m_screenLockerClientConnection = nullptr;
    });
    return socket.fd;
}

int WaylandServer::createXWaylandConnection()
{
    const auto socket = createConnection();
    if (!socket.connection) {
        return -1;
    }
    m_xwayland.client = socket.connection;
    m_xwayland.destroyConnection
        = connect(m_xwayland.client, &Wrapland::Server::Client::disconnected, this, [] {
              qFatal("Xwayland Connection died");
          });
    return socket.fd;
}

void WaylandServer::destroyXWaylandConnection()
{
    if (!m_xwayland.client) {
        return;
    }
    disconnect(m_xwayland.destroyConnection);
    m_xwayland.client->destroy();
    m_xwayland.client = nullptr;
}

int WaylandServer::createInputMethodConnection()
{
    const auto socket = createConnection();
    if (!socket.connection) {
        return -1;
    }
    m_inputMethodServerConnection = socket.connection;
    return socket.fd;
}

void WaylandServer::destroyInputMethodConnection()
{
    if (!m_inputMethodServerConnection) {
        return;
    }
    m_inputMethodServerConnection->destroy();
    m_inputMethodServerConnection = nullptr;
}

void WaylandServer::createDrmLeaseDevice()
{
    if (!drm_lease_device()) {
        globals->drm_lease_device_v1 = m_display->createDrmLeaseDeviceV1();
    }
}

void WaylandServer::create_addons(std::function<void()> callback)
{
    auto handle_client_created = [this, callback](auto client_created) {
        initWorkspace();
        if (client_created && hasScreenLockerIntegration()) {
            initScreenLocker();
        }
        callback();
    };
    createInternalConnection(handle_client_created);
}

void WaylandServer::createInternalConnection(std::function<void(bool)> callback)
{
    const auto socket = createConnection();
    if (!socket.connection) {
        callback(false);
        return;
    }
    m_internalConnection.server = socket.connection;
    using namespace Wrapland::Client;
    m_internalConnection.client = new ConnectionThread();
    m_internalConnection.client->setSocketFd(socket.fd);
    m_internalConnection.clientThread = new QThread;
    m_internalConnection.client->moveToThread(m_internalConnection.clientThread);
    m_internalConnection.clientThread->start();

    connect(m_internalConnection.client,
            &ConnectionThread::establishedChanged,
            this,
            [this, callback](bool established) {
                if (!established) {
                    return;
                }
                auto registry = new Registry;
                auto eventQueue = new EventQueue;
                eventQueue->setup(m_internalConnection.client);
                registry->setEventQueue(eventQueue);
                registry->create(m_internalConnection.client);
                m_internalConnection.registry = registry;
                m_internalConnection.queue = eventQueue;

                connect(
                    registry,
                    &Registry::interfacesAnnounced,
                    this,
                    [this, callback, registry] {
                        auto create_interface
                            = [registry](Registry::Interface iface_code, auto creator) {
                                  auto iface = registry->interface(iface_code);
                                  assert(iface.name != 0);
                                  return (registry->*creator)(iface.name, iface.version, nullptr);
                              };

                        m_internalConnection.shm
                            = create_interface(Registry::Interface::Shm, &Registry::createShmPool);
                        m_internalConnection.compositor = create_interface(
                            Registry::Interface::Compositor, &Registry::createCompositor);
                        m_internalConnection.seat
                            = create_interface(Registry::Interface::Seat, &Registry::createSeat);
                        callback(true);
                    },
                    Qt::QueuedConnection);

                registry->setup();
            });
    m_internalConnection.client->establishConnection();
}

void WaylandServer::remove_window(win::wayland::window* window)
{
    remove_all(windows, window);
    Q_EMIT window_removed(window);
}

void WaylandServer::dispatch()
{
    if (!m_display) {
        return;
    }
    if (m_internalConnection.server) {
        m_internalConnection.server->flush();
    }
    m_display->dispatchEvents(0);
}

win::wayland::window* WaylandServer::find_window(Wrapland::Server::Surface* surface) const
{
    if (!surface) {
        return nullptr;
    }
    auto it = std::find_if(windows.cbegin(), windows.cend(), [surface](auto win) {
        return win->surface() == surface;
    });
    return it != windows.cend() ? *it : nullptr;
}

Toplevel* WaylandServer::findToplevel(Surface* surface) const
{
    return find_window(surface);
}

bool WaylandServer::is_screen_locked() const
{
    if (!hasScreenLockerIntegration()) {
        return false;
    }
    return ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::Locked
        || ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::AcquiringLock;
}

bool WaylandServer::hasScreenLockerIntegration() const
{
    return !m_initFlags.testFlag(InitializationFlag::NoLockScreenIntegration);
}

bool WaylandServer::hasGlobalShortcutSupport() const
{
    return !m_initFlags.testFlag(InitializationFlag::NoGlobalShortcuts);
}

void WaylandServer::simulateUserActivity()
{
    if (globals->kde_idle) {
        globals->kde_idle->simulateUserActivity();
    }
}

void WaylandServer::updateKeyState(input::xkb::LEDs leds)
{
    if (!globals->key_state)
        return;

    globals->key_state->setState(KeyState::Key::CapsLock,
                                 leds & input::xkb::LED::CapsLock ? KeyState::State::Locked
                                                                  : KeyState::State::Unlocked);
    globals->key_state->setState(KeyState::Key::NumLock,
                                 leds & input::xkb::LED::NumLock ? KeyState::State::Locked
                                                                 : KeyState::State::Unlocked);
    globals->key_state->setState(KeyState::Key::ScrollLock,
                                 leds & input::xkb::LED::ScrollLock ? KeyState::State::Locked
                                                                    : KeyState::State::Unlocked);
}

}
