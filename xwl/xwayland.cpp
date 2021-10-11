/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2014 Martin Gräßlin <mgraesslin@kde.org>
Copyright 2019 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "xwayland.h"
#include "databridge.h"

#include "main_wayland.h"
#include "utils.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xcbutils.h"

#include <KLocalizedString>
#include <KSelectionOwner>

#include <QAbstractEventDispatcher>
#include <QFile>
#include <QFutureWatcher>
#include <QProcess>
#include <QSocketNotifier>
#include <QThread>
#include <QtConcurrentRun>

// system
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SYS_PROCCTL_H
#include <unistd.h>
#endif

#include <iostream>
#include <sys/socket.h>

static void readDisplay(int pipe)
{
    QFile readPipe;
    if (!readPipe.open(pipe, QIODevice::ReadOnly)) {
        std::cerr << "FATAL ERROR failed to open pipe to start X Server" << std::endl;
        exit(1);
    }
    auto displayNumber = readPipe.readLine();

    displayNumber.prepend(QByteArray(":"));
    displayNumber.remove(displayNumber.size() - 1, 1);
    std::cout << "X-Server started on display " << displayNumber.constData() << std::endl;

    setenv("DISPLAY", displayNumber.constData(), true);

    // close our pipe
    close(pipe);
}

namespace KWin::Xwl
{

Xwayland::Xwayland(ApplicationWaylandAbstract* app, std::function<void(int)> status_callback)
    : XwaylandInterface()
    , m_app(app)
    , status_callback{status_callback}
{
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        throw std::runtime_error("Failed to create pipe to start Xwayland");
    }

    int sx[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) < 0) {
        throw std::runtime_error("Failed to open socket to open XCB connection");
    }

    int fd = dup(sx[1]);
    if (fd < 0) {
        throw std::system_error(std::error_code(20, std::generic_category()),
                                "Failed to dup socket to open XCB connection");
    }

    auto const waylandSocket = waylandServer()->createXWaylandConnection();
    if (waylandSocket == -1) {
        close(fd);
        throw std::runtime_error("Failed to open socket for Xwayland");
    }
    auto const wlfd = dup(waylandSocket);
    if (wlfd < 0) {
        close(fd);
        throw std::system_error(std::error_code(20, std::generic_category()),
                                "Failed to dup socket for Xwayland");
    }

    m_xcbConnectionFd = sx[0];

    m_xwaylandProcess = new Process(this);
    m_xwaylandProcess->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_xwaylandProcess->setProgram(QStringLiteral("Xwayland"));

    QProcessEnvironment env = m_app->processStartupEnvironment();
    env.insert("WAYLAND_SOCKET", QByteArray::number(wlfd));
    env.insert("EGL_PLATFORM", QByteArrayLiteral("DRM"));

    if (qEnvironmentVariableIsSet("KWIN_XWAYLAND_DEBUG")) {
        env.insert("WAYLAND_DEBUG", QByteArrayLiteral("1"));
    }

    m_xwaylandProcess->setProcessEnvironment(env);
    m_xwaylandProcess->setArguments({QStringLiteral("-displayfd"),
                                     QString::number(pipeFds[1]),
                                     QStringLiteral("-rootless"),
                                     QStringLiteral("-wm"),
                                     QString::number(fd)});

    m_xwaylandFailConnection = connect(
        m_xwaylandProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                std::cerr << "FATAL ERROR: failed to start Xwayland" << std::endl;
            } else {
                std::cerr << "FATAL ERROR: Xwayland failed, going to exit now" << std::endl;
            }
            this->status_callback(1);
        });

    auto const xDisplayPipe = pipeFds[0];
    connect(m_xwaylandProcess, &QProcess::started, this, [this, xDisplayPipe] {
        QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
        QObject::connect(watcher,
                         &QFutureWatcher<void>::finished,
                         this,
                         &Xwayland::continueStartupWithX,
                         Qt::QueuedConnection);
        QObject::connect(watcher,
                         &QFutureWatcher<void>::finished,
                         watcher,
                         &QFutureWatcher<void>::deleteLater,
                         Qt::QueuedConnection);
        watcher->setFuture(QtConcurrent::run(readDisplay, xDisplayPipe));
    });

    m_xwaylandProcess->start();
    close(pipeFds[1]);
}

Xwayland::~Xwayland()
{
    data_bridge.reset();

    disconnect(m_xwaylandFailConnection);

    Workspace::self()->clear_x11();

    if (m_app->x11Connection()) {
        Xcb::setInputFocus(XCB_INPUT_FOCUS_POINTER_ROOT);
        m_app->destroyAtoms();
        Q_EMIT m_app->x11ConnectionAboutToBeDestroyed();
        m_app->setX11Connection(nullptr);
        xcb_disconnect(m_app->x11Connection());
    }

    if (m_xwaylandProcess->state() != QProcess::NotRunning) {
        disconnect(m_xwaylandProcess, nullptr, this, nullptr);
        m_xwaylandProcess->terminate();
        m_xwaylandProcess->waitForFinished(5000);
    }

    delete m_xwaylandProcess;
    m_xwaylandProcess = nullptr;

    waylandServer()->destroyXWaylandConnection();
}

void Xwayland::continueStartupWithX()
{
    auto screenNumber = 0;

    if (m_xcbConnectionFd == -1) {
        basic_data.connection = xcb_connect(nullptr, &screenNumber);
    } else {
        basic_data.connection = xcb_connect_to_fd(m_xcbConnectionFd, nullptr);
    }

    if (int error = xcb_connection_has_error(basic_data.connection)) {
        std::cerr << "FATAL ERROR connecting to Xwayland server: " << error << std::endl;
        status_callback(1);
        return;
    }

    auto iter = xcb_setup_roots_iterator(xcb_get_setup(basic_data.connection));
    basic_data.screen = iter.data;
    assert(basic_data.screen);

    m_app->setX11Connection(basic_data.connection, false);

    // we don't support X11 multi-head in Wayland
    m_app->setX11ScreenNumber(screenNumber);
    m_app->setX11RootWindow(defaultScreen()->root);

    xcb_read_notifier.reset(
        new QSocketNotifier(xcb_get_file_descriptor(basic_data.connection), QSocketNotifier::Read));

    auto processXcbEvents = [this] {
        while (auto event = xcb_poll_for_event(basic_data.connection)) {
            if (data_bridge->filterEvent(event)) {
                free(event);
                continue;
            }
            long result = 0;
            QThread::currentThread()->eventDispatcher()->filterNativeEvent(
                QByteArrayLiteral("xcb_generic_event_t"), event, &result);
            free(event);
        }
        xcb_flush(basic_data.connection);
    };

    connect(xcb_read_notifier.get(), &QSocketNotifier::activated, this, processXcbEvents);
    connect(QThread::currentThread()->eventDispatcher(),
            &QAbstractEventDispatcher::aboutToBlock,
            this,
            processXcbEvents);
    connect(QThread::currentThread()->eventDispatcher(),
            &QAbstractEventDispatcher::awake,
            this,
            processXcbEvents);

    // create selection owner for WM_S0 - magic X display number expected by XWayland
    KSelectionOwner owner("WM_S0", basic_data.connection, m_app->x11RootWindow());
    owner.claim(true);

    m_app->createAtoms();
    m_app->setupEventFilters();

    // Check  whether another windowmanager is running
    uint32_t const maskValues[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
    ScopedCPointer<xcb_generic_error_t> redirectCheck(
        xcb_request_check(connection(),
                          xcb_change_window_attributes_checked(
                              connection(), rootWindow(), XCB_CW_EVENT_MASK, maskValues)));
    if (!redirectCheck.isNull()) {
        fputs(i18n("kwin_wayland: an X11 window manager is running on the X11 Display.\n")
                  .toLocal8Bit()
                  .constData(),
              stderr);
        status_callback(1);
        return;
    }

    auto env = m_app->processStartupEnvironment();
    env.insert(QStringLiteral("DISPLAY"), QString::fromUtf8(qgetenv("DISPLAY")));
    m_app->setProcessStartupEnvironment(env);

    status_callback(0);
    Q_EMIT m_app->x11ConnectionChanged();

    // Trigger possible errors, there's still a chance to abort
    Xcb::sync();

    data_bridge.reset(new DataBridge(basic_data));
}

DragEventReply Xwayland::dragMoveFilter(Toplevel* target, QPoint const& pos)
{
    if (!data_bridge) {
        return DragEventReply::Wayland;
    }
    return data_bridge->dragMoveFilter(target, pos);
}

}
