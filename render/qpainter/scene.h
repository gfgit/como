/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#pragma once

#include "backend.h"

#include "render/scene.h"

namespace KWin::render
{

class shadow;

namespace qpainter
{

class KWIN_EXPORT scene : public render::scene
{
    Q_OBJECT

public:
    explicit scene(qpainter::backend* backend, render::compositor& compositor);
    ~scene() override;

    int64_t paint_output(base::output* output,
                         QRegion damage,
                         std::deque<Toplevel*> const& windows,
                         std::chrono::milliseconds presentTime) override;

    void paintGenericScreen(paint_type mask, ScreenPaintData data) override;

    CompositingType compositingType() const override;
    bool initFailed() const override;
    render::effect_frame* createEffectFrame(effect_frame_impl* frame) override;
    render::shadow* createShadow(Toplevel* toplevel) override;
    Decoration::Renderer* createDecorationRenderer(Decoration::DecoratedClientImpl* impl) override;
    void handle_screen_geometry_change(QSize const& size) override;

    bool animationsSupported() const override
    {
        return false;
    }

    QPainter* scenePainter() const override;
    QImage* qpainterRenderBuffer() const override;

    qpainter::backend* backend() const
    {
        return m_backend.data();
    }

protected:
    void paintBackground(QRegion region) override;
    render::window* createWindow(Toplevel* toplevel) override;
    void paintCursor() override;
    void paintEffectQuickView(EffectQuickView* w) override;

private:
    QScopedPointer<qpainter::backend> m_backend;
    QScopedPointer<QPainter> m_painter;
};

KWIN_EXPORT render::scene* create_scene(render::compositor& compositor);

inline QPainter* scene::scenePainter() const
{
    return m_painter.data();
}

}
}
