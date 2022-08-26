/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "kglobalaccel_plugin.h"

#include "input/platform_qobject.h"
#include "input/singleton_interface.h"

#include <QDebug>

KGlobalAccelImpl::KGlobalAccelImpl(QObject* parent)
    : KGlobalAccelInterfaceV2(parent)
{
}

KGlobalAccelImpl::~KGlobalAccelImpl() = default;

bool KGlobalAccelImpl::grabKey(int key, bool grab)
{
    Q_UNUSED(key)
    Q_UNUSED(grab)
    return true;
}

void KGlobalAccelImpl::setEnabled(bool enabled)
{
    if (m_shuttingDown) {
        return;
    }
    auto input = KWin::input::singleton_interface::platform_qobject;
    if (!input) {
        qFatal("This plugin is intended to be used with KWin and this is not KWin, exiting now");
    } else {
        if (!m_inputDestroyedConnection) {
            m_inputDestroyedConnection
                = connect(input, &QObject::destroyed, this, [this] { m_shuttingDown = true; });
        }
    }

    if (input->register_global_accel) {
        input->register_global_accel(enabled ? this : nullptr);
    } else {
        qFatal("Input platform does not support KGlobalAccel");
    }
}

bool KGlobalAccelImpl::checkKeyPressed(int keyQt)
{
    return keyPressed(keyQt);
}

bool KGlobalAccelImpl::checkKeyReleased(int keyQt)
{
    return keyReleased(keyQt);
}
