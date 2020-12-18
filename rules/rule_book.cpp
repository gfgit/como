/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "rule_book.h"

#include <KXMessages>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <kconfig.h>

#ifndef KCMRULES
#include "workspace.h"
#include "x11client.h"
#endif

#include "rule_book_settings.h"
#include "rule_settings.h"

namespace KWin
{

#ifndef KCMRULES

// Workspace
KWIN_SINGLETON_FACTORY(RuleBook)

RuleBook::RuleBook(QObject* parent)
    : QObject(parent)
    , m_updateTimer(new QTimer(this))
    , m_updatesDisabled(false)
    , m_temporaryRulesMessages()
{
    initWithX11();
    connect(kwinApp(), &Application::x11ConnectionChanged, this, &RuleBook::initWithX11);
    connect(m_updateTimer, SIGNAL(timeout()), SLOT(save()));
    m_updateTimer->setInterval(1000);
    m_updateTimer->setSingleShot(true);
}

RuleBook::~RuleBook()
{
    save();
    deleteAll();
}

void RuleBook::initWithX11()
{
    auto c = kwinApp()->x11Connection();
    if (!c) {
        m_temporaryRulesMessages.reset();
        return;
    }
    m_temporaryRulesMessages.reset(
        new KXMessages(c, kwinApp()->x11RootWindow(), "_KDE_NET_WM_TEMPORARY_RULES", nullptr));
    connect(m_temporaryRulesMessages.data(),
            SIGNAL(gotMessage(QString)),
            SLOT(temporaryRulesMessage(QString)));
}

void RuleBook::deleteAll()
{
    qDeleteAll(m_rules);
    m_rules.clear();
}

WindowRules RuleBook::find(Toplevel const* window, bool ignore_temporary)
{
    QVector<Rules*> ret;
    for (QList<Rules*>::Iterator it = m_rules.begin(); it != m_rules.end();) {
        if (ignore_temporary && (*it)->isTemporary()) {
            ++it;
            continue;
        }
        if ((*it)->match(window)) {
            Rules* rule = *it;
            qCDebug(KWIN_CORE) << "Rule found:" << rule << ":" << window;
            if (rule->isTemporary())
                it = m_rules.erase(it);
            else
                ++it;
            ret.append(rule);
            continue;
        }
        ++it;
    }
    return WindowRules(ret);
}

void RuleBook::edit(Toplevel* window, bool whole_app)
{
    save();
    QStringList args;
    args << QStringLiteral("--uuid") << window->internalId().toString();
    if (whole_app)
        args << QStringLiteral("--whole-app");
    QProcess* p = new Process(this);
    p->setArguments(args);
    p->setProcessEnvironment(kwinApp()->processStartupEnvironment());
    const QFileInfo buildDirBinary{QDir{QCoreApplication::applicationDirPath()},
                                   QStringLiteral("kwin_rules_dialog")};
    p->setProgram(buildDirBinary.exists() ? buildDirBinary.absoluteFilePath()
                                          : QStringLiteral(KWIN_RULES_DIALOG_BIN));
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            p,
            &QProcess::deleteLater);
    connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            qCDebug(KWIN_CORE) << "Failed to start" << p->program();
        }
    });
    p->start();
}

void RuleBook::load()
{
    deleteAll();
    if (!m_config) {
        m_config
            = KSharedConfig::openConfig(QStringLiteral(KWIN_NAME "rulesrc"), KConfig::NoGlobals);
    } else {
        m_config->reparseConfiguration();
    }
    m_rules = RuleBookSettings(m_config).rules().toList();
}

void RuleBook::save()
{
    m_updateTimer->stop();
    if (!m_config) {
        qCWarning(KWIN_CORE) << "RuleBook::save invoked without prior invocation of RuleBook::load";
        return;
    }
    QVector<Rules*> filteredRules;
    for (const auto& rule : qAsConst(m_rules)) {
        if (!rule->isTemporary()) {
            filteredRules.append(rule);
        }
    }
    RuleBookSettings settings(m_config);
    settings.setRules(filteredRules);
    settings.save();
}

void RuleBook::temporaryRulesMessage(const QString& message)
{
    bool was_temporary = false;
    for (QList<Rules*>::ConstIterator it = m_rules.constBegin(); it != m_rules.constEnd(); ++it)
        if ((*it)->isTemporary())
            was_temporary = true;
    Rules* rule = new Rules(message, true);
    m_rules.prepend(rule); // highest priority first
    if (!was_temporary)
        QTimer::singleShot(60000, this, SLOT(cleanupTemporaryRules()));
}

void RuleBook::cleanupTemporaryRules()
{
    bool has_temporary = false;
    for (QList<Rules*>::Iterator it = m_rules.begin(); it != m_rules.end();) {
        if ((*it)->discardTemporary(false)) { // deletes (*it)
            it = m_rules.erase(it);
        } else {
            if ((*it)->isTemporary())
                has_temporary = true;
            ++it;
        }
    }
    if (has_temporary)
        QTimer::singleShot(60000, this, SLOT(cleanupTemporaryRules()));
}

void RuleBook::discardUsed(Toplevel* window, bool withdrawn)
{
    bool updated = false;
    for (QList<Rules*>::Iterator it = m_rules.begin(); it != m_rules.end();) {
        if (window->control->rules().contains(*it)) {
            if ((*it)->discardUsed(withdrawn)) {
                updated = true;
            }
            if ((*it)->isEmpty()) {
                window->control->remove_rule(*it);
                Rules* r = *it;
                it = m_rules.erase(it);
                delete r;
                continue;
            }
        }
        ++it;
    }
    if (updated)
        requestDiskStorage();
}

void RuleBook::requestDiskStorage()
{
    m_updateTimer->start();
}

void RuleBook::setUpdatesDisabled(bool disable)
{
    m_updatesDisabled = disable;
    if (!disable) {
        for (auto client : Workspace::self()->allClientList()) {
            client->updateWindowRules(Rules::All);
        }
    }
}

bool RuleBook::areUpdatesDisabled() const
{
    return m_updatesDisabled;
}

#endif

}
