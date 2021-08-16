/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2018 Roman Gilg <subdiff@gmail.com>

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

#include "device_redirect.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>

namespace KWin
{
class Toplevel;

namespace Decoration
{
class DecoratedClientImpl;
}

namespace input
{
class redirect;
class touch;

class touch_redirect : public device_redirect
{
    Q_OBJECT
public:
    touch_redirect();
    ~touch_redirect() override;

    bool positionValid() const override;
    bool focusUpdatesBlocked() override;
    void init() override;

    void processDown(qint32 id, const QPointF& pos, quint32 time, input::touch* device = nullptr);
    void processUp(qint32 id, quint32 time, input::touch* device = nullptr);
    void processMotion(qint32 id, const QPointF& pos, quint32 time, input::touch* device = nullptr);
    void cancel();
    void frame();

    void insertId(qint32 internalId, qint32 wraplandId);
    void removeId(qint32 internalId);
    qint32 mappedId(qint32 internalId);

    void setDecorationPressId(qint32 id)
    {
        m_decorationId = id;
    }
    qint32 decorationPressId() const
    {
        return m_decorationId;
    }
    void setInternalPressId(qint32 id)
    {
        m_internalId = id;
    }
    qint32 internalPressId() const
    {
        return m_internalId;
    }

    QPointF position() const override
    {
        return m_lastPosition;
    }

private:
    void cleanupInternalWindow(QWindow* old, QWindow* now) override;
    void cleanupDecoration(Decoration::DecoratedClientImpl* old,
                           Decoration::DecoratedClientImpl* now) override;

    void focusUpdate(Toplevel* focusOld, Toplevel* focusNow) override;

    bool m_inited = false;
    qint32 m_decorationId = -1;
    qint32 m_internalId = -1;
    /**
     * external/wrapland
     */
    QHash<qint32, qint32> m_idMapper;
    QMetaObject::Connection m_focusGeometryConnection;
    bool m_windowUpdatedInCycle = false;
    QPointF m_lastPosition;

    int m_touches = 0;
};

}
}
