/*
 *   Copyright © 2010 Fredrik Höglund <fredrik@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; see the file COPYING.  if not, write to
 *   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *   Boston, MA 02110-1301, USA.
 */

#include "blur_config.h"
// KConfigSkeleton
#include "blurconfig.h"
#include <config-kwin.h>

#include <KPluginFactory>
#include <kwineffects_interface.h>

K_PLUGIN_CLASS(KWin::BlurEffectConfig)

namespace KWin
{

BlurEffectConfig::BlurEffectConfig(QWidget* parent, const QVariantList& args)
    : KCModule(parent, args)
{
    ui.setupUi(this);
    BlurConfig::instance(KWIN_CONFIG);
    addConfig(BlurConfig::self(), this);

    load();
}

BlurEffectConfig::~BlurEffectConfig()
{
}

void BlurEffectConfig::save()
{
    KCModule::save();

    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("blur"));
}

} // namespace KWin

#include "blur_config.moc"
