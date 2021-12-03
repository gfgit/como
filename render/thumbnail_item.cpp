/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "thumbnail_item.h"

#include "effects.h"
#include "render/compositor.h"
#include "toplevel.h"
#include "win/control.h"
#include "workspace.h"

#include <QDebug>
#include <QPainter>
#include <QQuickWindow>

namespace KWin::render
{

basic_thumbnail_item::basic_thumbnail_item(QQuickItem* parent)
    : QQuickPaintedItem(parent)
    , m_brightness(1.0)
    , m_saturation(1.0)
    , m_clipToItem()
{
    connect(render::compositor::self(),
            &render::compositor::compositingToggled,
            this,
            &basic_thumbnail_item::compositingToggled);
    compositingToggled();
    QTimer::singleShot(0, this, &basic_thumbnail_item::init);
}

basic_thumbnail_item::~basic_thumbnail_item()
{
}

void basic_thumbnail_item::compositingToggled()
{
    m_parent.clear();
    if (effects) {
        connect(
            effects, &EffectsHandler::windowAdded, this, &basic_thumbnail_item::effectWindowAdded);
        connect(effects, &EffectsHandler::windowDamaged, this, &basic_thumbnail_item::repaint);
        effectWindowAdded();
    }
}

void basic_thumbnail_item::init()
{
    findParentEffectWindow();
    if (m_parent) {
        m_parent->registerThumbnail(this);
    }
}

void basic_thumbnail_item::findParentEffectWindow()
{
    if (effects) {
        QQuickWindow* qw = window();
        if (!qw) {
            qCDebug(KWIN_CORE) << "No QQuickWindow assigned yet";
            return;
        }
        if (auto w = static_cast<render::effects_window_impl*>(effects->findWindow(qw))) {
            m_parent = QPointer<render::effects_window_impl>(w);
        }
    }
}

void basic_thumbnail_item::effectWindowAdded()
{
    // the window might be added before the EffectWindow is created
    // by using this slot we can register the thumbnail when it is finally created
    if (m_parent.isNull()) {
        findParentEffectWindow();
        if (m_parent) {
            m_parent->registerThumbnail(this);
        }
    }
}

void basic_thumbnail_item::setBrightness(qreal brightness)
{
    if (qFuzzyCompare(brightness, m_brightness)) {
        return;
    }
    m_brightness = brightness;
    update();
    Q_EMIT brightnessChanged();
}

void basic_thumbnail_item::setSaturation(qreal saturation)
{
    if (qFuzzyCompare(saturation, m_saturation)) {
        return;
    }
    m_saturation = saturation;
    update();
    Q_EMIT saturationChanged();
}

void basic_thumbnail_item::setClipTo(QQuickItem* clip)
{
    m_clipToItem = QPointer<QQuickItem>(clip);
    Q_EMIT clipToChanged();
}

window_thumbnail_item::window_thumbnail_item(QQuickItem* parent)
    : basic_thumbnail_item(parent)
    , m_wId(nullptr)
    , m_client(nullptr)
{
}

window_thumbnail_item::~window_thumbnail_item()
{
}

void window_thumbnail_item::setWId(const QUuid& wId)
{
    if (m_wId == wId) {
        return;
    }
    m_wId = wId;
    if (m_wId != nullptr) {
        setClient(workspace()->findAbstractClient(
            [this](Toplevel const* c) { return c->internalId() == m_wId; }));
    } else if (m_client) {
        m_client = nullptr;
        Q_EMIT clientChanged();
    }
    Q_EMIT wIdChanged(wId);
}

void window_thumbnail_item::setClient(Toplevel* window)
{
    if (m_client == window) {
        return;
    }
    m_client = window;
    if (m_client) {
        setWId(m_client->internalId());
    } else {
        setWId({});
    }
    Q_EMIT clientChanged();
}

void window_thumbnail_item::paint(QPainter* painter)
{
    if (effects) {
        return;
    }
    auto client = workspace()->findAbstractClient(
        [this](Toplevel const* c) { return c->internalId() == m_wId; });
    if (!client) {
        return;
    }
    auto pixmap = client->control->icon().pixmap(boundingRect().size().toSize());
    const QSize size(boundingRect().size().toSize() - pixmap.size());
    painter->drawPixmap(
        boundingRect()
            .adjusted(
                size.width() / 2.0, size.height() / 2.0, -size.width() / 2.0, -size.height() / 2.0)
            .toRect(),
        pixmap);
}

void window_thumbnail_item::repaint(KWin::EffectWindow* w)
{
    if (static_cast<KWin::render::effects_window_impl*>(w)->window()->internalId() == m_wId) {
        update();
    }
}

desktop_thumbnail_item::desktop_thumbnail_item(QQuickItem* parent)
    : basic_thumbnail_item(parent)
    , m_desktop(0)
{
}

desktop_thumbnail_item::~desktop_thumbnail_item()
{
}

void desktop_thumbnail_item::setDesktop(int desktop)
{
    desktop = qBound<int>(1, desktop, VirtualDesktopManager::self()->count());
    if (desktop == m_desktop) {
        return;
    }
    m_desktop = desktop;
    update();
    Q_EMIT desktopChanged(m_desktop);
}

void desktop_thumbnail_item::paint(QPainter* painter)
{
    Q_UNUSED(painter)
    if (effects) {
        return;
    }
    // TODO: render icon
}

void desktop_thumbnail_item::repaint(EffectWindow* w)
{
    if (w->isOnDesktop(m_desktop)) {
        update();
    }
}

} // namespace KWin
