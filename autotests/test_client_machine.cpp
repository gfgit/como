/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "testutils.h"

#include "base/x11/xcb/window.h"
#include "win/x11/client_machine.h"

#include <QApplication>
#include <QX11Info>
#include <QtTest>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(KWIN_CORE, "kwin_core")

using namespace KWin;

class TestClientMachine : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void hostName_data();
    void hostName();
    void emptyHostName();

private:
    void setClientMachineProperty(xcb_window_t window, const QByteArray& hostname);
    QByteArray m_hostName;
    QByteArray m_fqdn;
    xcb_connection_t* connection{nullptr};
    xcb_window_t root_window{XCB_WINDOW_NONE};
};

void TestClientMachine::setClientMachineProperty(xcb_window_t window, const QByteArray& hostname)
{
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        XCB_ATOM_WM_CLIENT_MACHINE,
                        XCB_ATOM_STRING,
                        8,
                        hostname.length(),
                        hostname.constData());
}

void TestClientMachine::initTestCase()
{
#ifdef HOST_NAME_MAX
    char hostnamebuf[HOST_NAME_MAX];
#else
    char hostnamebuf[256];
#endif
    if (gethostname(hostnamebuf, sizeof hostnamebuf) >= 0) {
        hostnamebuf[sizeof(hostnamebuf) - 1] = 0;
        m_hostName = hostnamebuf;
    }
    addrinfo* res;
    addrinfo addressHints;
    memset(&addressHints, 0, sizeof(addressHints));
    addressHints.ai_family = PF_UNSPEC;
    addressHints.ai_socktype = SOCK_STREAM;
    addressHints.ai_flags |= AI_CANONNAME;
    if (getaddrinfo(m_hostName.constData(), nullptr, &addressHints, &res) == 0) {
        if (res->ai_canonname) {
            m_fqdn = QByteArray(res->ai_canonname);
        }
    }
    freeaddrinfo(res);

    connection = QX11Info::connection();
    root_window = QX11Info::appRootWindow();
}

void TestClientMachine::cleanupTestCase()
{
}

void TestClientMachine::hostName_data()
{
    QTest::addColumn<QByteArray>("hostName");
    QTest::addColumn<QByteArray>("expectedHost");
    QTest::addColumn<bool>("local");

    QTest::newRow("empty") << QByteArray() << QByteArray("localhost") << true;
    QTest::newRow("localhost") << QByteArray("localhost") << QByteArray("localhost") << true;
    QTest::newRow("hostname") << m_hostName << m_hostName << true;
    QTest::newRow("HOSTNAME") << m_hostName.toUpper() << m_hostName.toUpper() << true;
    QByteArray cutted(m_hostName);
    cutted.remove(0, 1);
    QTest::newRow("ostname") << cutted << cutted << false;
    QByteArray domain("random.name.not.exist.tld");
    QTest::newRow("domain") << domain << domain << false;
    QTest::newRow("fqdn") << m_fqdn << m_fqdn << true;
    QTest::newRow("FQDN") << m_fqdn.toUpper() << m_fqdn.toUpper() << true;
    cutted = m_fqdn;
    cutted.remove(0, 1);
    QTest::newRow("qdn") << cutted << cutted << false;
}

void TestClientMachine::hostName()
{
    const QRect geometry(0, 0, 10, 10);
    const uint32_t values[] = {true};
    base::x11::xcb::window window(connection,
                                  root_window,
                                  geometry,
                                  XCB_WINDOW_CLASS_INPUT_ONLY,
                                  XCB_CW_OVERRIDE_REDIRECT,
                                  values);
    QFETCH(QByteArray, hostName);
    QFETCH(bool, local);
    setClientMachineProperty(window, hostName);

    win::x11::client_machine clientMachine;
    QSignalSpy spy(&clientMachine, &win::x11::client_machine::localhostChanged);

    base::x11::data data;
    data.connection = connection;
    data.root_window = root_window;
    clientMachine.resolve(data, window, XCB_WINDOW_NONE);
    QTEST(clientMachine.hostname(), "expectedHost");

    int i = 0;
    while (clientMachine.is_resolving() && i++ < 50) {
        // name is being resolved in an external thread, so let's wait a little bit
        QTest::qWait(250);
    }

    QCOMPARE(clientMachine.is_local(), local);
    QCOMPARE(spy.isEmpty(), !local);
}

void TestClientMachine::emptyHostName()
{
    const QRect geometry(0, 0, 10, 10);
    const uint32_t values[] = {true};
    base::x11::xcb::window window(connection,
                                  root_window,
                                  geometry,
                                  XCB_WINDOW_CLASS_INPUT_ONLY,
                                  XCB_CW_OVERRIDE_REDIRECT,
                                  values);
    win::x11::client_machine clientMachine;
    QSignalSpy spy(&clientMachine, &win::x11::client_machine::localhostChanged);

    base::x11::data data;
    data.connection = connection;
    data.root_window = root_window;
    clientMachine.resolve(data, window, XCB_WINDOW_NONE);
    QCOMPARE(clientMachine.hostname(), win::x11::client_machine::localhost());
    QVERIFY(clientMachine.is_local());
    // should be local
    QCOMPARE(spy.isEmpty(), false);
}

Q_CONSTRUCTOR_FUNCTION(forceXcb)
QTEST_MAIN(TestClientMachine)
#include "test_client_machine.moc"
