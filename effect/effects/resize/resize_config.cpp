/*
    SPDX-FileCopyrightText: 2010 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "resize_config.h"
// KConfigSkeleton
#include "resizeconfig.h"
#include <config-kwin.h>
#include <kwineffects_interface.h>

#include <KPluginFactory>
#include <kconfiggroup.h>

#include <QVBoxLayout>

K_PLUGIN_CLASS(KWin::ResizeEffectConfig)

namespace KWin
{

ResizeEffectConfigForm::ResizeEffectConfigForm(QWidget* parent)
    : QWidget(parent)
{
    setupUi(this);
}

ResizeEffectConfig::ResizeEffectConfig(QWidget* parent, const QVariantList& args)
    : KCModule(parent, args)
{
    m_ui = new ResizeEffectConfigForm(this);

    QVBoxLayout* layout = new QVBoxLayout(this);

    layout->addWidget(m_ui);

    ResizeConfig::instance(KWIN_CONFIG);
    addConfig(ResizeConfig::self(), m_ui);

    load();
}

void ResizeEffectConfig::save()
{
    KCModule::save();
    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("resize"));
}

} // namespace

#include "resize_config.moc"
