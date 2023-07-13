/*
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "showpaint_config.h"

#include <KActionCollection>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KShortcutsEditor>

#include <QAction>

K_PLUGIN_CLASS(KWin::ShowPaintEffectConfig)

namespace KWin
{

ShowPaintEffectConfig::ShowPaintEffectConfig(QObject* parent, const KPluginMetaData& data)
    : KCModule(parent, data)
    , m_ui(std::make_unique<Ui::ShowPaintEffectConfig>())
{
    m_ui->setupUi(widget());

    auto* actionCollection = new KActionCollection(this, QStringLiteral("kwin"));

    actionCollection->setComponentDisplayName(i18n("KWin"));
    actionCollection->setConfigGroup(QStringLiteral("ShowPaint"));
    actionCollection->setConfigGlobal(true);

    QAction* toggleAction = actionCollection->addAction(QStringLiteral("Toggle"));
    toggleAction->setText(i18n("Toggle Show Paint"));
    toggleAction->setProperty("isConfigurationAction", true);
    KGlobalAccel::self()->setDefaultShortcut(toggleAction, {});
    KGlobalAccel::self()->setShortcut(toggleAction, {});

    m_ui->shortcutsEditor->addCollection(actionCollection);

    connect(m_ui->shortcutsEditor, &KShortcutsEditor::keyChange, this, &KCModule::markAsChanged);
}

ShowPaintEffectConfig::~ShowPaintEffectConfig()
{
    // If save() is called, undo() has no effect.
    m_ui->shortcutsEditor->undo();
}

void ShowPaintEffectConfig::save()
{
    KCModule::save();
    m_ui->shortcutsEditor->save();
}

void ShowPaintEffectConfig::defaults()
{
    m_ui->shortcutsEditor->allDefault();
    KCModule::defaults();
}

} // namespace KWin

#include "showpaint_config.moc"
