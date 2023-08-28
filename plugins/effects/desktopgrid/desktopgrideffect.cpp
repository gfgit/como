/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "desktopgrideffect.h"
#include "desktopgridconfig.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>

#include <KLocalizedString>
#include <QAction>
#include <QQuickItem>
#include <QTimer>
#include <cmath>

namespace KWin
{

DesktopGridEffect::DesktopGridEffect()
    : m_shutdownTimer(new QTimer(this))
    , m_state(new EffectTogglableState(this))
    , m_border(new EffectTogglableTouchBorder(m_state))
{
    qmlRegisterUncreatableType<DesktopGridEffect>(
        "org.kde.kwin.private.desktopgrid",
        1,
        0,
        "DesktopGridEffect",
        QStringLiteral("Cannot create elements of type DesktopGridEffect"));

    m_shutdownTimer->setSingleShot(true);
    connect(m_shutdownTimer, &QTimer::timeout, this, &DesktopGridEffect::realDeactivate);
    connect(effects, &EffectsHandler::screenAboutToLock, this, &DesktopGridEffect::realDeactivate);
    connect(effects,
            &EffectsHandler::desktopGridWidthChanged,
            this,
            &DesktopGridEffect::gridColumnsChanged);
    connect(effects,
            &EffectsHandler::desktopGridHeightChanged,
            this,
            &DesktopGridEffect::gridRowsChanged);
    connect(m_state, &EffectTogglableState::activated, this, &DesktopGridEffect::activate);
    connect(m_state, &EffectTogglableState::deactivated, this, &DesktopGridEffect::deactivate);
    connect(m_state,
            &EffectTogglableState::inProgressChanged,
            this,
            &DesktopGridEffect::gestureInProgressChanged);
    connect(m_state,
            &EffectTogglableState::partialActivationFactorChanged,
            this,
            &DesktopGridEffect::partialActivationFactorChanged);
    connect(m_state,
            &EffectTogglableState::statusChanged,
            this,
            [this](EffectTogglableState::Status status) {
                setRunning(status != EffectTogglableState::Status::Inactive);
            });

    auto toggleAction = m_state->toggleAction();
    toggleAction->setObjectName(QStringLiteral("ShowDesktopGrid"));
    toggleAction->setText(i18n("Show Desktop Grid"));
    m_toggleShortcut = effects->registerGlobalShortcutAndDefault(
        {static_cast<Qt::Key>(Qt::META) + Qt::Key_F8}, toggleAction);

    auto gesture = new EffectTogglableGesture(m_state);
    gesture->addTouchpadSwipeGesture(SwipeDirection::Up, 4);

    initConfig<DesktopGridConfig>();
    reconfigure(ReconfigureAll);

    setSource(QUrl::fromLocalFile(
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               QStringLiteral("kwin/effects/desktopgrid/qml/main.qml"))));
}

DesktopGridEffect::~DesktopGridEffect()
{
}

void DesktopGridEffect::reconfigure(ReconfigureFlags)
{
    DesktopGridConfig::self()->read();
    setLayout(DesktopGridConfig::layoutMode());
    setAnimationDuration(animationTime(300));

    for (const ElectricBorder& border : qAsConst(m_borderActivate)) {
        effects->unreserveElectricBorder(border, this);
    }

    m_borderActivate.clear();

    const QList<int> activateBorders = DesktopGridConfig::borderActivate();
    for (const int& border : activateBorders) {
        m_borderActivate.append(ElectricBorder(border));
        effects->reserveElectricBorder(ElectricBorder(border), this);
    }

    m_border->setBorders(DesktopGridConfig::touchBorderActivate());

    Q_EMIT showAddRemoveChanged();
    Q_EMIT desktopNameAlignmentChanged();
    Q_EMIT desktopLayoutModeChanged();
    Q_EMIT customLayoutRowsChanged();
}

int DesktopGridEffect::requestedEffectChainPosition() const
{
    return 70;
}

bool DesktopGridEffect::borderActivated(ElectricBorder border)
{
    if (m_borderActivate.contains(border)) {
        m_state->toggle();
        return true;
    }
    return false;
}

void DesktopGridEffect::grabbedKeyboardEvent(QKeyEvent* keyEvent)
{
    if (m_toggleShortcut.contains(keyEvent->key() | keyEvent->modifiers())) {
        if (keyEvent->type() == QEvent::KeyPress) {
            m_state->toggle();
        }
        return;
    }
    QuickSceneEffect::grabbedKeyboardEvent(keyEvent);
}

Qt::AlignmentFlag DesktopGridEffect::desktopNameAlignment() const
{
    return Qt::AlignmentFlag(DesktopGridConfig::desktopNameAlignment());
}

DesktopGridEffect::DesktopLayoutMode DesktopGridEffect::desktopLayoutMode() const
{
    return DesktopGridEffect::DesktopLayoutMode(DesktopGridConfig::desktopLayoutMode());
}

int DesktopGridEffect::customLayoutRows() const
{
    return DesktopGridConfig::customLayoutRows();
}

void DesktopGridEffect::addDesktop() const
{
    effects->setNumberOfDesktops(effects->numberOfDesktops() + 1);
}

void DesktopGridEffect::removeDesktop() const
{
    effects->setNumberOfDesktops(effects->numberOfDesktops() - 1);
}

void DesktopGridEffect::swapDesktops(int from, int to)
{
    QList<EffectWindow*> fromList;
    QList<EffectWindow*> toList;
    for (auto* w : effects->stackingOrder()) {
        if (!w->isNormalWindow() || !w->isOnCurrentActivity()) {
            continue;
        }
        if (w->isOnDesktop(from)) {
            fromList << w;
        } else if (w->isOnDesktop(to)) {
            toList << w;
        }
    }
    for (auto* w : fromList) {
        effects->windowToDesktop(w, to);
    }
    for (auto* w : toList) {
        effects->windowToDesktop(w, from);
    }
}

int DesktopGridEffect::gridRows() const
{
    switch (desktopLayoutMode()) {
    case DesktopLayoutMode::LayoutAutomatic:
        return ceil(sqrt(effects->numberOfDesktops()));
    case DesktopLayoutMode::LayoutCustom:
        return qBound(1, customLayoutRows(), effects->numberOfDesktops());
    case DesktopLayoutMode::LayoutPager:
    default:
        return effects->desktopGridSize().height();
    }
}

int DesktopGridEffect::gridColumns() const
{
    switch (desktopLayoutMode()) {
    case DesktopLayoutMode::LayoutAutomatic:
        return ceil(sqrt(effects->numberOfDesktops()));
    case DesktopLayoutMode::LayoutCustom:
        return qMax(1.0, ceil(qreal(effects->numberOfDesktops()) / customLayoutRows()));
    case DesktopLayoutMode::LayoutPager:
    default:
        return effects->desktopGridSize().width();
    }
}

int DesktopGridEffect::animationDuration() const
{
    return m_animationDuration;
}

void DesktopGridEffect::setAnimationDuration(int duration)
{
    if (m_animationDuration != duration) {
        m_animationDuration = duration;
        Q_EMIT animationDurationChanged();
    }
}

bool DesktopGridEffect::showAddRemove() const
{
    return DesktopGridConfig::showAddRemove();
}

int DesktopGridEffect::layout() const
{
    return m_layout;
}

void DesktopGridEffect::setLayout(int layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        Q_EMIT layoutChanged();
    }
}

bool DesktopGridEffect::gestureInProgress() const
{
    return m_state->inProgress();
}

void DesktopGridEffect::activate()
{
    if (effects->isScreenLocked()) {
        return;
    }

    m_state->activate();
}

void DesktopGridEffect::deactivate()
{
    auto const screens = effects->screens();
    for (auto const screen : screens) {
        if (auto view = viewForScreen(screen)) {
            QMetaObject::invokeMethod(view->rootItem(), "stop");
        }
    }
    m_shutdownTimer->start(animationDuration());

    m_state->deactivate();
}

void DesktopGridEffect::realDeactivate()
{
    m_state->setStatus(EffectTogglableState::Status::Inactive);
}

} // namespace KWin
