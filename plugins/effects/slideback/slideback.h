/*
SPDX-FileCopyrightText: 2009 Michael Zanetti <michael_zanetti@gmx.net>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SLIDEBACK_H
#define KWIN_SLIDEBACK_H

#include <kwineffects/effect.h>
#include <kwineffects/motions.h>

namespace KWin
{

class SlideBackEffect : public Effect
{
    Q_OBJECT
public:
    SlideBackEffect();

    void prePaintWindow(effect::window_prepaint_data& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(effect::window_paint_data& data) override;
    void postPaintWindow(EffectWindow* w) override;

    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void postPaintScreen() override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 50;
    }

public Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow* w);
    void slotWindowDeleted(KWin::EffectWindow* w);
    void slotWindowUnminimized(KWin::EffectWindow* w);
    void slotStackingOrderChanged();
    void slotTabBoxAdded();
    void slotTabBoxClosed();

private:
    WindowMotionManager motionManager;
    EffectWindowList usableOldStackingOrder;
    EffectWindowList oldStackingOrder;
    EffectWindowList coveringWindows;
    EffectWindowList elevatedList;
    EffectWindow *m_justMapped, *m_upmostWindow;
    QHash<EffectWindow*, QRect> destinationList;
    int m_tabboxActive;
    QList<QRegion> clippedRegions;
    std::chrono::milliseconds m_lastPresentTime = std::chrono::milliseconds::zero();

    QRect getSlideDestination(const QRect& windowUnderGeometry, const QRect& windowOverGeometry);
    bool isWindowUsable(EffectWindow* w);
    bool intersects(EffectWindow* windowUnder, const QRect& windowOverGeometry);
    EffectWindowList usableWindows(const EffectWindowList& allWindows);
    QRect getModalGroupGeometry(EffectWindow* w);
    void windowRaised(EffectWindow* w);
};

} // namespace

#endif
