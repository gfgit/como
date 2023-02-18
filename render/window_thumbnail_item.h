/*
SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QQuickItem>
#include <QUuid>

#include <epoxy/gl.h>

namespace KWin
{

class EffectWindow;
class GLRenderTarget;
class GLTexture;

namespace scripting
{
class window;
}

namespace render
{
class ThumbnailTextureProvider;

class KWIN_EXPORT window_thumbnail_item : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QUuid wId READ wId WRITE setWId NOTIFY wIdChanged)
    Q_PROPERTY(KWin::scripting::window* client READ client WRITE setClient NOTIFY clientChanged)

    Q_PROPERTY(QSize sourceSize READ sourceSize WRITE setSourceSize NOTIFY sourceSizeChanged)
    Q_PROPERTY(qreal brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(qreal saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(QQuickItem* clipTo READ clipTo WRITE setClipTo NOTIFY clipToChanged)
public:
    explicit window_thumbnail_item(QQuickItem* parent = nullptr);
    ~window_thumbnail_item() override;

    QUuid wId() const;
    void setWId(const QUuid& wId);

    scripting::window* client() const;
    void setClient(scripting::window* window);

    qreal brightness() const;
    void setBrightness(qreal brightness);

    qreal saturation() const;
    void setSaturation(qreal saturation);

    QQuickItem* clipTo() const;
    void setClipTo(QQuickItem* clip);

    QSize sourceSize() const;
    void setSourceSize(const QSize& sourceSize);

    QSGTextureProvider* textureProvider() const override;
    bool isTextureProvider() const override;
    QSGNode* updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData*) override;

protected:
    void releaseResources() override;

Q_SIGNALS:
    void wIdChanged();
    void clientChanged();
    void brightnessChanged();
    void saturationChanged();
    void clipToChanged();
    void sourceSizeChanged();

private:
    bool use_gl_thumbnails() const;
    QImage fallbackImage() const;
    QRectF paintedRect() const;
    void invalidateOffscreenTexture();
    void updateOffscreenTexture();
    void destroyOffscreenTexture();
    void updateImplicitSize();
    void update_render_notifier();

    QSize m_sourceSize;
    QUuid m_wId;
    QPointer<scripting::window> m_client;
    bool m_dirty = false;

    mutable ThumbnailTextureProvider* m_provider = nullptr;
    QSharedPointer<GLTexture> m_offscreenTexture;
    QScopedPointer<GLRenderTarget> m_offscreenTarget;
    GLsync m_acquireFence = 0;
    qreal m_devicePixelRatio = 1;

    QMetaObject::Connection render_notifier;
};

}
}
