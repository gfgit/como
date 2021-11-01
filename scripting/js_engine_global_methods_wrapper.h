/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QJSValue>
#include <QObject>
#include <QVariant>

class QQuickWindow;

namespace KWin::scripting
{
class DeclarativeScript;

class js_engine_global_methods_wrapper : public QObject
{
    Q_OBJECT
public:
    //------------------------------------------------------------------
    // enums copy&pasted from kwinglobals.h for exporting

    enum ClientAreaOption {
        ///< geometry where a window will be initially placed after being mapped
        PlacementArea,
        ///< window movement snapping area?  ignore struts
        MovementArea,
        ///< geometry to which a window will be maximized
        MaximizeArea,
        ///< like MaximizeArea, but ignore struts - used e.g. for topmenu
        MaximizeFullArea,
        ///< area for fullscreen windows
        FullScreenArea,
        ///< whole workarea (all screens together)
        WorkArea,
        ///< whole area (all screens together), ignore struts
        FullArea,
        ///< one whole screen, ignore struts
        ScreenArea
    };
    Q_ENUM(ClientAreaOption)
    explicit js_engine_global_methods_wrapper(DeclarativeScript* parent);
    ~js_engine_global_methods_wrapper() override;

public Q_SLOTS:
    QVariant readConfig(const QString& key, QVariant defaultValue = QVariant());
    void registerWindow(QQuickWindow* window);
    bool registerShortcut(const QString& name,
                          const QString& text,
                          const QKeySequence& keys,
                          QJSValue function);

private:
    DeclarativeScript* m_script;
};

}
