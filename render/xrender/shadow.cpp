/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "shadow.h"

#include "kwinxrenderutils.h"
#include "utils.h"

#include <QPainter>

namespace KWin::render::xrender
{

shadow::shadow(Toplevel* toplevel)
    : render::shadow(toplevel)
{
    for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
        m_pictures[i] = nullptr;
    }
}

shadow::~shadow()
{
    for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
        delete m_pictures[i];
    }
}

void shadow::layoutShadowRects(QRect& top,
                               QRect& topRight,
                               QRect& right,
                               QRect& bottomRight,
                               QRect& bottom,
                               QRect& bottomLeft,
                               QRect& left,
                               QRect& topLeft)
{
    WindowQuadList quads = shadowQuads();

    if (quads.count() == 0) {
        return;
    }

    WindowQuad topQuad = quads.select(WindowQuadShadowTop)[0];
    WindowQuad topRightQuad = quads.select(WindowQuadShadowTopRight)[0];
    WindowQuad topLeftQuad = quads.select(WindowQuadShadowTopLeft)[0];
    WindowQuad leftQuad = quads.select(WindowQuadShadowLeft)[0];
    WindowQuad rightQuad = quads.select(WindowQuadShadowRight)[0];
    WindowQuad bottomQuad = quads.select(WindowQuadShadowBottom)[0];
    WindowQuad bottomRightQuad = quads.select(WindowQuadShadowBottomRight)[0];
    WindowQuad bottomLeftQuad = quads.select(WindowQuadShadowBottomLeft)[0];

    top = QRect(topQuad.left(),
                topQuad.top(),
                (topQuad.right() - topQuad.left()),
                (topQuad.bottom() - topQuad.top()));
    topLeft = QRect(topLeftQuad.left(),
                    topLeftQuad.top(),
                    (topLeftQuad.right() - topLeftQuad.left()),
                    (topLeftQuad.bottom() - topLeftQuad.top()));
    topRight = QRect(topRightQuad.left(),
                     topRightQuad.top(),
                     (topRightQuad.right() - topRightQuad.left()),
                     (topRightQuad.bottom() - topRightQuad.top()));
    left = QRect(leftQuad.left(),
                 leftQuad.top(),
                 (leftQuad.right() - leftQuad.left()),
                 (leftQuad.bottom() - leftQuad.top()));
    right = QRect(rightQuad.left(),
                  rightQuad.top(),
                  (rightQuad.right() - rightQuad.left()),
                  (rightQuad.bottom() - rightQuad.top()));
    bottom = QRect(bottomQuad.left(),
                   bottomQuad.top(),
                   (bottomQuad.right() - bottomQuad.left()),
                   (bottomQuad.bottom() - bottomQuad.top()));
    bottomLeft = QRect(bottomLeftQuad.left(),
                       bottomLeftQuad.top(),
                       (bottomLeftQuad.right() - bottomLeftQuad.left()),
                       (bottomLeftQuad.bottom() - bottomLeftQuad.top()));
    bottomRight = QRect(bottomRightQuad.left(),
                        bottomRightQuad.top(),
                        (bottomRightQuad.right() - bottomRightQuad.left()),
                        (bottomRightQuad.bottom() - bottomRightQuad.top()));
}

void shadow::buildQuads()
{
    render::shadow::buildQuads();

    if (shadowQuads().count() == 0) {
        return;
    }

    QRect stlr, str, strr, srr, sbrr, sbr, sblr, slr;
    layoutShadowRects(str, strr, srr, sbrr, sbr, sblr, slr, stlr);
}

bool shadow::prepareBackend()
{
    if (hasDecorationShadow()) {
        const QImage shadowImage = decorationShadowImage();
        QPainter p;
        int x = 0;
        int y = 0;
        auto drawElement = [this, &x, &y, &p, &shadowImage](auto element) {
            QPixmap pix(elementSize(element));
            pix.fill(Qt::transparent);
            p.begin(&pix);
            p.drawImage(0, 0, shadowImage, x, y, pix.width(), pix.height());
            p.end();
            setShadowElement(pix, element);
            return pix.size();
        };
        x += drawElement(shadow_element::top_left).width();
        x += drawElement(shadow_element::top).width();
        y += drawElement(shadow_element::top_right).height();
        drawElement(shadow_element::right);
        x = 0;
        y += drawElement(shadow_element::left).height();
        x += drawElement(shadow_element::bottom_left).width();
        x += drawElement(shadow_element::bottom).width();
        drawElement(shadow_element::bottom_right).width();
    }
    const uint32_t values[] = {XCB_RENDER_REPEAT_NORMAL};
    for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
        delete m_pictures[i];
        m_pictures[i] = new XRenderPicture(shadowPixmap(static_cast<shadow_element>(i)).toImage());
        xcb_render_change_picture(connection(), *m_pictures[i], XCB_RENDER_CP_REPEAT, values);
    }
    return true;
}

xcb_render_picture_t shadow::picture(shadow_element element) const
{
    if (!m_pictures[enum_index(element)]) {
        return XCB_RENDER_PICTURE_NONE;
    }
    return *m_pictures[enum_index(element)];
}

}
