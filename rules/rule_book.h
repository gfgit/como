/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_RULES_RULE_BOOK_H
#define KWIN_RULES_RULE_BOOK_H

#include <QRect>
#include <QVector>
#include <netwm_def.h>

#include "options.h"
#include "placement.h"
#include "utils.h"
#include "window_rules.h"

class QDebug;
class KConfig;
class KXMessages;

namespace KWin
{
class AbstractClient;
class Rules;

#ifndef KCMRULES
class KWIN_EXPORT RuleBook : public QObject
{
    Q_OBJECT
public:
    ~RuleBook() override;
    WindowRules find(const AbstractClient*, bool);
    void discardUsed(AbstractClient* c, bool withdraw);
    void setUpdatesDisabled(bool disable);
    bool areUpdatesDisabled() const;
    void load();
    void edit(AbstractClient* c, bool whole_app);
    void requestDiskStorage();

    void setConfig(const KSharedConfig::Ptr& config)
    {
        m_config = config;
    }

private Q_SLOTS:
    void temporaryRulesMessage(const QString&);
    void cleanupTemporaryRules();
    void save();

private:
    void deleteAll();
    void initWithX11();
    QTimer* m_updateTimer;
    bool m_updatesDisabled;
    QList<Rules*> m_rules;
    QScopedPointer<KXMessages> m_temporaryRulesMessages;
    KSharedConfig::Ptr m_config;

    KWIN_SINGLETON(RuleBook)
};

#endif

}

#endif
