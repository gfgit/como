/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "overvieweffect.h"
#include "kwineffects/effects_handler.h"
#include "overviewconfig.h"

#include <KLocalizedString>
#include <QAction>
#include <QDebug>
#include <QQuickItem>
#include <QTimer>

namespace KWin
{

OverviewEffect::OverviewEffect()
    : m_state(new TogglableState(this))
    , m_border(new TogglableTouchBorder(m_state))
    , m_shutdownTimer(new QTimer(this))
{
    auto gesture = new TogglableGesture(m_state);
    gesture->addTouchpadPinchGesture(PinchDirection::Contracting, 4);
    gesture->addTouchscreenSwipeGesture(SwipeDirection::Up, 3);

    connect(m_state, &TogglableState::activated, this, &OverviewEffect::activate);
    connect(m_state, &TogglableState::deactivated, this, &OverviewEffect::deactivate);
    connect(m_state,
            &TogglableState::inProgressChanged,
            this,
            &OverviewEffect::gestureInProgressChanged);
    connect(m_state,
            &TogglableState::partialActivationFactorChanged,
            this,
            &OverviewEffect::partialActivationFactorChanged);
    connect(m_state, &TogglableState::statusChanged, this, [this](TogglableState::Status status) {
        if (status == TogglableState::Status::Activating) {
            m_searchText = QString();
        }
        setRunning(status != TogglableState::Status::Inactive);
    });

    m_shutdownTimer->setSingleShot(true);
    connect(m_shutdownTimer, &QTimer::timeout, this, &OverviewEffect::realDeactivate);

    const QKeySequence defaultToggleShortcut = Qt::META | Qt::Key_W;
    auto toggleAction = m_state->toggleAction();
    connect(toggleAction, &QAction::triggered, m_state, &TogglableState::toggle);
    toggleAction->setObjectName(QStringLiteral("Overview"));
    toggleAction->setText(i18n("Toggle Overview"));
    m_toggleShortcut
        = effects->registerGlobalShortcutAndDefault({defaultToggleShortcut}, toggleAction);

    connect(effects, &EffectsHandler::screenAboutToLock, this, &OverviewEffect::realDeactivate);

    initConfig<OverviewConfig>();
    reconfigure(ReconfigureAll);

    setSource(QUrl::fromLocalFile(
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               QStringLiteral("kwin/effects/overview/qml/main.qml"))));
}

OverviewEffect::~OverviewEffect()
{
}

void OverviewEffect::reconfigure(ReconfigureFlags)
{
    OverviewConfig::self()->read();
    setLayout(OverviewConfig::layoutMode());
    setAnimationDuration(animationTime(300));

    for (const ElectricBorder& border : qAsConst(m_borderActivate)) {
        effects->unreserveElectricBorder(border, this);
    }

    m_borderActivate.clear();

    const QList<int> activateBorders = OverviewConfig::borderActivate();
    for (const int& border : activateBorders) {
        m_borderActivate.append(ElectricBorder(border));
        effects->reserveElectricBorder(ElectricBorder(border), this);
    }

    m_border->setBorders(OverviewConfig::touchBorderActivate());
}

int OverviewEffect::animationDuration() const
{
    return m_animationDuration;
}

void OverviewEffect::setAnimationDuration(int duration)
{
    if (m_animationDuration != duration) {
        m_animationDuration = duration;
        Q_EMIT animationDurationChanged();
    }
}

int OverviewEffect::layout() const
{
    return m_layout;
}

bool OverviewEffect::ignoreMinimized() const
{
    return OverviewConfig::ignoreMinimized();
}

void OverviewEffect::setLayout(int layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        Q_EMIT layoutChanged();
    }
}

int OverviewEffect::requestedEffectChainPosition() const
{
    return 70;
}

bool OverviewEffect::borderActivated(ElectricBorder border)
{
    if (m_borderActivate.contains(border)) {
        m_state->toggle();
        return true;
    }
    return false;
}

void OverviewEffect::activate()
{
    if (effects->isScreenLocked()) {
        return;
    }

    m_state->activate();
}

void OverviewEffect::deactivate()
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

void OverviewEffect::realDeactivate()
{
    m_state->setStatus(TogglableState::Status::Inactive);
}

void OverviewEffect::grabbedKeyboardEvent(QKeyEvent* keyEvent)
{
    if (m_toggleShortcut.contains(keyEvent->key() | keyEvent->modifiers())) {
        if (keyEvent->type() == QEvent::KeyPress) {
            m_state->toggle();
        }
        return;
    }
    QuickSceneEffect::grabbedKeyboardEvent(keyEvent);
}

} // namespace KWin
