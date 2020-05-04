/*
 * Copyright (c) 2004 Lubos Lunak <l.lunak@kde.org>
 * Copyright (c) 2020 Ismael Asensio <isma.af@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "rulesdialog.h"

#include <QDialogButtonBox>
#include <QQuickItem>
#include <QQuickView>
#include <QLayout>
#include <QPushButton>

#include <KLocalizedString>


namespace KWin
{

RulesDialog::RulesDialog(QWidget* parent, const char* name)
    : QDialog(parent)
    , m_rulesModel(new RulesModel(this))
{
    setObjectName(name);
    setModal(true);
    setWindowTitle(i18n("Edit Window-Specific Settings"));
    setWindowIcon(QIcon::fromTheme("preferences-system-windows-actions"));
    setLayout(new QVBoxLayout);

    // Init RuleEditor QML QuickView
    QQuickView *quickView = new QQuickView();
    quickView->setSource(QUrl::fromLocalFile(QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("kpackage/kcms/kcm_kwinrules/contents/ui/RulesEditor.qml"))));
    quickView->setResizeMode(QQuickView::SizeRootObjectToView);
    quickView->rootObject()->setProperty("rulesModel", QVariant::fromValue(m_rulesModel));

    m_quickWidget = QWidget::createWindowContainer(quickView, this);
    m_quickWidget->setMinimumSize(QSize(650, 575));
    layout()->addWidget(m_quickWidget);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, SIGNAL(accepted()), SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), SLOT(reject()));
    layout()->addWidget(buttons);
}

// window is set only for Alt+F3/Window-specific settings, because the dialog
// is then related to one specific window
Rules* RulesDialog::edit(Rules* r, const QVariantMap& info, bool show_hints)
{
    Q_UNUSED(show_hints);

    m_rules = r;

    m_rulesModel->importFromRules(m_rules);
    if (!info.isEmpty()) {
        m_rulesModel->setWindowProperties(info, true);
    }

    exec();

    return m_rules;
}

void RulesDialog::accept()
{
    m_rules = m_rulesModel->exportToRules();
    QDialog::accept();
}

}
