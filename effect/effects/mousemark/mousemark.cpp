/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

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
#include "mousemark.h"

// KConfigSkeleton
#include "mousemarkconfig.h"

#include <kwinconfig.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>
#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <KGlobalAccel>
#include <KLocalizedString>
#include <QAction>
#include <QPainter>

#include <cmath>

namespace KWin
{

#define NULL_POINT (QPoint(-1, -1)) // null point is (0,0), which is valid :-/

MouseMarkEffect::MouseMarkEffect()
{
    initConfig<MouseMarkConfig>();
    QAction* a = new QAction(this);
    a->setObjectName(QStringLiteral("ClearMouseMarks"));
    a->setText(i18n("Clear All Mouse Marks"));
    KGlobalAccel::self()->setDefaultShortcut(
        a, QList<QKeySequence>() << Qt::SHIFT + Qt::META + Qt::Key_F11);
    KGlobalAccel::self()->setShortcut(a,
                                      QList<QKeySequence>() << Qt::SHIFT + Qt::META + Qt::Key_F11);
    effects->registerGlobalShortcut(Qt::SHIFT + Qt::META + Qt::Key_F11, a);
    connect(a, &QAction::triggered, this, &MouseMarkEffect::clear);
    a = new QAction(this);
    a->setObjectName(QStringLiteral("ClearLastMouseMark"));
    a->setText(i18n("Clear Last Mouse Mark"));
    KGlobalAccel::self()->setDefaultShortcut(
        a, QList<QKeySequence>() << Qt::SHIFT + Qt::META + Qt::Key_F12);
    KGlobalAccel::self()->setShortcut(a,
                                      QList<QKeySequence>() << Qt::SHIFT + Qt::META + Qt::Key_F12);
    effects->registerGlobalShortcut(Qt::SHIFT + Qt::META + Qt::Key_F12, a);
    connect(a, &QAction::triggered, this, &MouseMarkEffect::clearLast);

    connect(effects, &EffectsHandler::mouseChanged, this, &MouseMarkEffect::slotMouseChanged);
    connect(effects,
            &EffectsHandler::screenLockingChanged,
            this,
            &MouseMarkEffect::screenLockingChanged);
    reconfigure(ReconfigureAll);
    arrow_start = NULL_POINT;
    effects->startMousePolling(); // We require it to detect activation as well
}

MouseMarkEffect::~MouseMarkEffect()
{
    effects->stopMousePolling();
}

static int width_2 = 1;
void MouseMarkEffect::reconfigure(ReconfigureFlags)
{
    MouseMarkConfig::self()->read();
    width = MouseMarkConfig::lineWidth();
    width_2 = width / 2;
    color = MouseMarkConfig::color();
    color.setAlphaF(1.0);
}

void MouseMarkEffect::paintScreen(int mask, const QRegion& region, ScreenPaintData& data)
{
    effects->paintScreen(mask, region, data); // paint normal screen
    if (marks.isEmpty() && drawing.isEmpty())
        return;
    if (effects->isOpenGLCompositing()) {
        if (!GLPlatform::instance()->isGLES()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glEnable(GL_LINE_SMOOTH);
            glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        }
        glLineWidth(width);
        GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        vbo->setUseColor(true);
        vbo->setColor(color);
        ShaderBinder binder(ShaderTrait::UniformColor);
        binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, data.projectionMatrix());
        QVector<float> verts;
        for (auto const& mark : qAsConst(marks)) {
            verts.clear();
            verts.reserve(mark.size() * 2);
            for (auto const& p : qAsConst(mark)) {
                verts << p.x() << p.y();
            }
            vbo->setData(verts.size() / 2, 2, verts.data(), nullptr);
            vbo->render(GL_LINE_STRIP);
        }
        if (!drawing.isEmpty()) {
            verts.clear();
            verts.reserve(drawing.size() * 2);
            for (auto const& p : qAsConst(drawing)) {
                verts << p.x() << p.y();
            }
            vbo->setData(verts.size() / 2, 2, verts.data(), nullptr);
            vbo->render(GL_LINE_STRIP);
        }
        glLineWidth(1.0);
        if (!GLPlatform::instance()->isGLES()) {
            glDisable(GL_LINE_SMOOTH);
            glDisable(GL_BLEND);
        }
    } else if (effects->compositingType() == QPainterCompositing) {
        QPainter* painter = effects->scenePainter();
        painter->save();
        QPen pen(color);
        pen.setWidth(width);
        painter->setPen(pen);
        for (auto const& mark : qAsConst(marks)) {
            drawMark(painter, mark);
        }
        drawMark(painter, drawing);
        painter->restore();
    }
}

void MouseMarkEffect::drawMark(QPainter* painter, const Mark& mark)
{
    if (mark.count() <= 1) {
        return;
    }
    for (int i = 0; i < mark.count() - 1; ++i) {
        painter->drawLine(mark[i], mark[i + 1]);
    }
}

void MouseMarkEffect::slotMouseChanged(const QPoint& pos,
                                       const QPoint&,
                                       Qt::MouseButtons,
                                       Qt::MouseButtons,
                                       Qt::KeyboardModifiers modifiers,
                                       Qt::KeyboardModifiers)
{
    if (modifiers == (Qt::META | Qt::SHIFT | Qt::CTRL)) { // start/finish arrow
        if (arrow_start != NULL_POINT) {
            marks.append(createArrow(arrow_start, pos));
            arrow_start = NULL_POINT;
            effects->addRepaintFull();
            return;
        } else
            arrow_start = pos;
    }
    if (arrow_start != NULL_POINT)
        return;
    // TODO the shortcuts now trigger this right before they're activated
    if (modifiers == (Qt::META | Qt::SHIFT)) { // activated
        if (drawing.isEmpty())
            drawing.append(pos);
        if (drawing.last() == pos)
            return;
        QPoint pos2 = drawing.last();
        drawing.append(pos);
        QRect repaint = QRect(qMin(pos.x(), pos2.x()),
                              qMin(pos.y(), pos2.y()),
                              qMax(pos.x(), pos2.x()),
                              qMax(pos.y(), pos2.y()));
        repaint.adjust(-width, -width, width, width);
        effects->addRepaint(repaint);
    } else if (!drawing.isEmpty()) {
        marks.append(drawing);
        drawing.clear();
    }
}

void MouseMarkEffect::clear()
{
    drawing.clear();
    marks.clear();
    effects->addRepaintFull();
}

void MouseMarkEffect::clearLast()
{
    if (arrow_start != NULL_POINT) {
        arrow_start = NULL_POINT;
    } else if (!drawing.isEmpty()) {
        drawing.clear();
        effects->addRepaintFull();
    } else if (!marks.isEmpty()) {
        marks.pop_back();
        effects->addRepaintFull();
    }
}

MouseMarkEffect::Mark MouseMarkEffect::createArrow(QPoint arrow_start, QPoint arrow_end)
{
    Mark ret;
    double angle = atan2(static_cast<double>(arrow_end.y() - arrow_start.y()),
                         static_cast<double>(arrow_end.x() - arrow_start.x()));
    ret += arrow_start
        + QPoint(50 * cos(angle + M_PI / 6),
                 50 * sin(angle + M_PI / 6)); // right one
    ret += arrow_start;
    ret += arrow_end;
    ret += arrow_start; // it's connected lines, so go back with the middle one
    ret += arrow_start + QPoint(50 * cos(angle - M_PI / 6),
                                50 * sin(angle - M_PI / 6)); // left one
    return ret;
}

void MouseMarkEffect::screenLockingChanged(bool locked)
{
    if (!marks.isEmpty() || !drawing.isEmpty()) {
        effects->addRepaintFull();
    }
    // disable mouse polling while screen is locked.
    if (locked) {
        effects->stopMousePolling();
    } else {
        effects->startMousePolling();
    }
}

bool MouseMarkEffect::isActive() const
{
    return (!marks.isEmpty() || !drawing.isEmpty()) && !effects->isScreenLocked();
}

int MouseMarkEffect::requestedEffectChainPosition() const
{
    return 10;
}

}
