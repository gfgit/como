/*
    SPDX-FileCopyrightText: 2022 David Edmundson <davidedmundson@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "blendchanges.h"

#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>
#include <kwingl/utils.h>

#include <QDBusConnection>
#include <QTimer>

namespace KWin
{
BlendChanges::BlendChanges()
    : DeformEffect()
{
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/BlendChanges"),
                                                 QStringLiteral("org.kde.KWin.BlendChanges"),
                                                 this,
                                                 QDBusConnection::ExportAllSlots);

    setLive(false);
    m_timeline.setEasingCurve(QEasingCurve::InOutCubic);
}

BlendChanges::~BlendChanges() = default;

bool BlendChanges::supported()
{
    return effects->compositingType() == OpenGLCompositing && effects->animationsSupported();
}

void KWin::BlendChanges::start(int delay)
{
    int animationDuration = animationTime(400);

    if (!supported() || m_state != Off) {
        return;
    }
    if (effects->hasActiveFullScreenEffect()) {
        return;
    }

    const EffectWindowList allWindows = effects->stackingOrder();
    for (auto window : allWindows) {
        redirect(window);
    }

    QTimer::singleShot(delay, this, [this, animationDuration]() {
        m_timeline.setDuration(std::chrono::milliseconds(animationDuration));
        effects->addRepaintFull();
        m_state = Blending;
    });

    m_state = ShowingCache;
}

void BlendChanges::drawWindow(EffectWindow* window,
                              int mask,
                              const QRegion& region,
                              WindowPaintData& data)
{
    // draw the new picture underneath at full opacity
    if (m_state != ShowingCache) {
        Effect::drawWindow(window, mask, region, data);
    }
    // then the old on top, it works better than changing both alphas with the current blend mode
    if (m_state != Off) {
        DeformEffect::drawWindow(window, mask, region, data);
    }
}

void BlendChanges::deform(EffectWindow* window,
                          int mask,
                          WindowPaintData& data,
                          WindowQuadList& quads)
{
    Q_UNUSED(window)
    Q_UNUSED(mask)
    Q_UNUSED(quads)
    data.setOpacity(1.0 - m_timeline.value());
}

bool BlendChanges::isActive() const
{
    return m_state != Off;
}

void BlendChanges::postPaintScreen()
{
    if (m_timeline.done()) {
        m_lastPresentTime = std::chrono::milliseconds::zero();
        m_timeline.reset();
        m_state = Off;

        const EffectWindowList allWindows = effects->stackingOrder();
        for (auto window : allWindows) {
            unredirect(window);
        }
    }
    effects->addRepaintFull();
}

void BlendChanges::prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    if (m_state == Off) {
        return;
    }
    if (m_state == Blending) {
        std::chrono::milliseconds delta(0);
        if (m_lastPresentTime.count()) {
            delta = presentTime - m_lastPresentTime;
        }
        m_lastPresentTime = presentTime;
        m_timeline.update(delta);
    }

    effects->prePaintScreen(data, presentTime);
}

} // namespace KWin
