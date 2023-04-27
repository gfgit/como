/*
SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "showfps_config.h"

// KConfigSkeleton
#include "showfpsconfig.h"
#include <config-kwin.h>
#include <kwineffects_interface.h>

#include <KLocalizedString>
#include <KPluginFactory>

K_PLUGIN_CLASS(KWin::ShowFpsEffectConfig)

namespace KWin
{

ShowFpsEffectConfig::ShowFpsEffectConfig(QWidget* parent, const QVariantList& args)
    : KCModule(parent, args)
{
    m_ui.setupUi(this);

    ShowFpsConfig::instance(KWIN_CONFIG);
    addConfig(ShowFpsConfig::self(), this);
}

void ShowFpsEffectConfig::save()
{
    KCModule::save();
    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("showfps"));
}

} // namespace

#include "showfps_config.moc"
