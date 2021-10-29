/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "main_x11.h"

#include <config-kwin.h>

#include "debug/x11_console.h"
#include "platform.h"
#include "input/backend/x11/platform.h"
#include "input/x11/redirect.h"
#include "render/x11/compositor.h"
#include "screenlockerwatcher.h"
#include "seat/backend/logind/session.h"
#include "sm.h"
#include "win/x11/space.h"
#include "workspace.h"
#include "xcbutils.h"

#include <KConfigGroup>
#include <KCrash>
#include <KLocalizedString>
#include <KPluginLoader>
#include <KPluginMetaData>
#include <KSelectionOwner>
#include <KQuickAddons/QtQuickSettings>

#include <qplatformdefs.h>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QX11Info>
#include <QtDBus>

// system
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H
#include <iostream>

Q_LOGGING_CATEGORY(KWIN_CORE, "kwin_core", QtWarningMsg)

namespace KWin
{

static void sighandler(int)
{
    QApplication::exit();
}

class AlternativeWMDialog : public QDialog
{
public:
    AlternativeWMDialog()
        : QDialog() {
        QWidget* mainWidget = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(mainWidget);
        QString text = i18n(
                           "KWin is unstable.\n"
                           "It seems to have crashed several times in a row.\n"
                           "You can select another window manager to run:");
        QLabel* textLabel = new QLabel(text, mainWidget);
        layout->addWidget(textLabel);
        wmList = new QComboBox(mainWidget);
        wmList->setEditable(true);
        layout->addWidget(wmList);

        addWM(QStringLiteral("metacity"));
        addWM(QStringLiteral("openbox"));
        addWM(QStringLiteral("fvwm2"));
        addWM(QStringLiteral(KWIN_INTERNAL_NAME_X11));

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(mainWidget);
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setDefault(true);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        mainLayout->addWidget(buttons);

        raise();
    }

    void addWM(const QString& wm) {
        // TODO: Check if WM is installed
        if (!QStandardPaths::findExecutable(wm).isEmpty())
            wmList->addItem(wm);
    }
    QString selectedWM() const {
        return wmList->currentText();
    }

private:
    QComboBox* wmList;
};

class KWinSelectionOwner : public KSelectionOwner
{
    Q_OBJECT
public:
    explicit KWinSelectionOwner(int screen)
        : KSelectionOwner(make_selection_atom(screen), screen)
    {
    }

private:
    bool genericReply(xcb_atom_t target_P, xcb_atom_t property_P, xcb_window_t requestor_P) override {
        if (target_P == xa_version) {
            int32_t version[] = { 2, 0 };
            xcb_change_property(connection(), XCB_PROP_MODE_REPLACE, requestor_P,
                                property_P, XCB_ATOM_INTEGER, 32, 2, version);
        } else
            return KSelectionOwner::genericReply(target_P, property_P, requestor_P);
        return true;
    }

    void replyTargets(xcb_atom_t property_P, xcb_window_t requestor_P) override {
        KSelectionOwner::replyTargets(property_P, requestor_P);
        xcb_atom_t atoms[ 1 ] = { xa_version };
        // PropModeAppend !
        xcb_change_property(connection(), XCB_PROP_MODE_APPEND, requestor_P,
                            property_P, XCB_ATOM_ATOM, 32, 1, atoms);
    }

    void getAtoms() override {
        KSelectionOwner::getAtoms();
        if (xa_version == XCB_ATOM_NONE) {
            const QByteArray name(QByteArrayLiteral("VERSION"));
            ScopedCPointer<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(
                connection(),
                xcb_intern_atom_unchecked(connection(), false, name.length(), name.constData()),
                nullptr));
            if (!atom.isNull()) {
                xa_version = atom->atom;
            }
        }
    }

    xcb_atom_t make_selection_atom(int screen_P) {
        if (screen_P < 0)
            screen_P = QX11Info::appScreen();
        QByteArray screen(QByteArrayLiteral("WM_S"));
        screen.append(QByteArray::number(screen_P));
        ScopedCPointer<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(
            connection(),
            xcb_intern_atom_unchecked(connection(), false, screen.length(), screen.constData()),
            nullptr));
        if (atom.isNull()) {
            return XCB_ATOM_NONE;
        }
        return atom->atom;
    }
    static xcb_atom_t xa_version;
};
xcb_atom_t KWinSelectionOwner::xa_version = XCB_ATOM_NONE;

//************************************
// ApplicationX11
//************************************

ApplicationX11::ApplicationX11(int &argc, char **argv)
    : Application(OperationModeX11, argc, argv)
    , owner()
    , m_replace(false)
{
    setX11Connection(QX11Info::connection());
    setX11RootWindow(QX11Info::appRootWindow());
}

ApplicationX11::~ApplicationX11()
{
    setTerminating();
    workspace.reset();
    compositor.reset();
    if (!owner.isNull() && owner->ownerWindow() != XCB_WINDOW_NONE)   // If there was no --replace (no new WM)
        Xcb::setInputFocus(XCB_INPUT_FOCUS_POINTER_ROOT);
}

void ApplicationX11::setReplace(bool replace)
{
    m_replace = replace;
}

void ApplicationX11::lostSelection()
{
    sendPostedEvents();
    workspace.reset();
    compositor.reset();
    // Remove windowmanager privileges
    Xcb::selectInput(rootWindow(), XCB_EVENT_MASK_PROPERTY_CHANGE);
    quit();
}

render::compositor* ApplicationX11::get_compositor()
{
    return compositor.get();
}

debug::console* ApplicationX11::create_debug_console()
{
    return new debug::x11_console;
}

void ApplicationX11::start()
{
    prepare_start();
    ScreenLockerWatcher::self()->initialize();

    render.reset(new render::backend::x11::X11StandalonePlatform(base));
    platform = render.get();

    crashChecking();
    Application::setX11ScreenNumber(QX11Info::appScreen());

    owner.reset(new KWinSelectionOwner(Application::x11ScreenNumber()));
    connect(owner.data(), &KSelectionOwner::failedToClaimOwnership, []{
        fputs(i18n("kwin: unable to claim manager selection, another wm running? (try using --replace)\n").toLocal8Bit().constData(), stderr);
        ::exit(1);
    });
    connect(owner.data(), &KSelectionOwner::lostOwnership, this, &ApplicationX11::lostSelection);
    connect(owner.data(), &KSelectionOwner::claimedOwnership, [this]{
        setupEventFilters();
        createOptions();

        // Check  whether another windowmanager is running
        const uint32_t maskValues[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
        ScopedCPointer<xcb_generic_error_t> redirectCheck(xcb_request_check(connection(),
                                                                            xcb_change_window_attributes_checked(connection(),
                                                                                                                 rootWindow(),
                                                                                                                 XCB_CW_EVENT_MASK,
                                                                                                                 maskValues)));
        if (!redirectCheck.isNull()) {
            fputs(i18n("kwin: another window manager is running (try using --replace)\n").toLocal8Bit().constData(), stderr);
            if (!wasCrash()) // if this is a crash-restart, DrKonqi may have stopped the process w/o killing the connection
                ::exit(1);
        }

        session.reset(new seat::backend::logind::session());

        auto input = new input::backend::x11::platform;
        this->input.reset(input);
        input::add_redirect(input, std::make_unique<input::x11::redirect>());
        input::backend::x11::create_cursor(input);

        try {
            render->init();
        } catch (std::exception const&) {
            std::cerr <<  "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
            ::exit(1);
        }

        compositor = std::make_unique<render::x11::compositor>();
        workspace = std::make_unique<win::x11::space>();
        Q_EMIT workspaceCreated();

        Q_EMIT startup_finished();

        // Trigger possible errors, there's still a chance to abort.
        Xcb::sync();
        kwinApp()->notifyKSplash();
    });

    // we need to do an XSync here, otherwise the QPA might crash us later on
    Xcb::sync();
    owner->claim(m_replace || wasCrash(), true);

    createAtoms();
}

bool ApplicationX11::notify(QObject* o, QEvent* e)
{
    if (e->spontaneous() && Workspace::self()->workspaceEvent(e))
        return true;
    return QApplication::notify(o, e);
}

void ApplicationX11::setupCrashHandler()
{
    KCrash::setEmergencySaveFunction(ApplicationX11::crashHandler);
}

void ApplicationX11::crashChecking()
{
    setupCrashHandler();
    if (crashes >= 4) {
        // Something has gone seriously wrong
        AlternativeWMDialog dialog;
        QString cmd = QStringLiteral(KWIN_INTERNAL_NAME_X11);
        if (dialog.exec() == QDialog::Accepted)
            cmd = dialog.selectedWM();
        else
            ::exit(1);
        if (cmd.length() > 500) {
            qCDebug(KWIN_CORE) << "Command is too long, truncating";
            cmd = cmd.left(500);
        }
        qCDebug(KWIN_CORE) << "Starting" << cmd << "and exiting";
        char buf[1024];
        sprintf(buf, "%s &", cmd.toLatin1().data());
        system(buf);
        ::exit(1);
    }
    if (crashes >= 2) {
        // Disable compositing if we have had too many crashes
        qCDebug(KWIN_CORE) << "Too many crashes recently, disabling compositing";
        KConfigGroup compgroup(KSharedConfig::openConfig(), "Compositing");
        compgroup.writeEntry("Enabled", false);
    }
    // Reset crashes count if we stay up for more that 15 seconds
    QTimer::singleShot(15 * 1000, this, &Application::resetCrashesCount);
}

void ApplicationX11::notifyKSplash()
{
    // Tell KSplash that KWin has started
    QDBusMessage ksplashProgressMessage = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KSplash"),
                                                                            QStringLiteral("/KSplash"),
                                                                            QStringLiteral("org.kde.KSplash"),
                                                                            QStringLiteral("setStage"));
    ksplashProgressMessage.setArguments(QList<QVariant>() << QStringLiteral("wm"));
    QDBusConnection::sessionBus().asyncCall(ksplashProgressMessage);
}

void ApplicationX11::crashHandler(int signal)
{
    crashes++;

    fprintf(stderr, "Application::crashHandler() called with signal %d; recent crashes: %d\n", signal, crashes);
    char cmd[1024];
    sprintf(cmd, "%s --crashes %d &",
            QFile::encodeName(QCoreApplication::applicationFilePath()).constData(), crashes);

    sleep(1);
    system(cmd);
}

} // namespace

int main(int argc, char * argv[])
{
    KWin::Application::setupMalloc();
    KWin::Application::setupLocalizedString();

    int primaryScreen = 0;
    xcb_connection_t *c = xcb_connect(nullptr, &primaryScreen);
    if (!c || xcb_connection_has_error(c)) {
        fprintf(stderr, "%s: FATAL ERROR while trying to open display %s\n",
                argv[0], qgetenv("DISPLAY").constData());
        exit(1);
    }

    xcb_disconnect(c);
    c = nullptr;

    if (signal(SIGTERM, KWin::sighandler) == SIG_IGN)
        signal(SIGTERM, SIG_IGN);
    if (signal(SIGINT, KWin::sighandler) == SIG_IGN)
        signal(SIGINT, SIG_IGN);
    if (signal(SIGHUP, KWin::sighandler) == SIG_IGN)
        signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    // Disable the glib event loop integration, since it seems to be responsible
    // for several bug reports about high CPU usage (bug #239963)
    setenv("QT_NO_GLIB", "1", true);

    // enforce xcb plugin, unfortunately command line switch has precedence
    setenv("QT_QPA_PLATFORM", "xcb", true);

    qunsetenv("QT_DEVICE_PIXEL_RATIO");
    qunsetenv("QT_SCALE_FACTOR");
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    // KSMServer talks to us directly on DBus.
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);

    KWin::ApplicationX11 a(argc, argv);
    a.setupTranslator();

    KWin::Application::createAboutData();
    KQuickAddons::QtQuickSettings::init();

    // disables vsync for any QtQuick windows we create (BUG 406180)
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

    QCommandLineOption replaceOption(QStringLiteral("replace"), i18n("Replace already-running ICCCM2.0-compliant window manager"));

    QCommandLineParser parser;
    a.setupCommandLine(&parser);
    parser.addOption(replaceOption);

    parser.process(a);
    a.processCommandLine(&parser);
    a.setReplace(parser.isSet(replaceOption));

    // perform sanity checks
    if (a.platformName().toLower() != QStringLiteral("xcb")) {
        fprintf(stderr, "%s: FATAL ERROR expecting platform xcb but got platform %s\n",
                argv[0], qPrintable(a.platformName()));
        exit(1);
    }
    if (!QX11Info::display()) {
        fprintf(stderr, "%s: FATAL ERROR KWin requires Xlib support in the xcb plugin. Do not configure Qt with -no-xcb-xlib\n",
                argv[0]);
        exit(1);
    }

    a.start();

    return a.exec();
}

#include "main_x11.moc"
