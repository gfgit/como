/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "js_engine_global_methods_wrapper.h"

#include "script.h"
#include "scripting.h"
#include "scripting_logging.h"

#include "input/redirect.h"
#include "main.h"

#include <KConfigGroup>
#include <KGlobalAccel>
#include <QAction>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>

namespace KWin::scripting
{

js_engine_global_methods_wrapper::js_engine_global_methods_wrapper(declarative_script* parent)
    : QObject(parent)
    , m_script(parent)
{
}

js_engine_global_methods_wrapper::~js_engine_global_methods_wrapper()
{
}

QVariant js_engine_global_methods_wrapper::readConfig(const QString& key, QVariant defaultValue)
{
    return m_script->config().readEntry(key, defaultValue);
}

void js_engine_global_methods_wrapper::registerWindow(QQuickWindow* window)
{
    QPointer<QQuickWindow> guard = window;
    connect(
        window,
        &QWindow::visibilityChanged,
        this,
        [guard](QWindow::Visibility visibility) {
            if (guard && visibility == QWindow::Hidden) {
                guard->destroy();
            }
        },
        Qt::QueuedConnection);
}

bool js_engine_global_methods_wrapper::registerShortcut(const QString& name,
                                                        const QString& text,
                                                        const QKeySequence& keys,
                                                        QJSValue function)
{
    if (!function.isCallable()) {
        qCDebug(KWIN_SCRIPTING) << "Fourth and final argument must be a javascript function";
        return false;
    }

    QAction* a = new QAction(this);
    a->setObjectName(name);
    a->setText(text);
    const QKeySequence shortcut = QKeySequence(keys);
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>{shortcut});
    kwinApp()->input->redirect->registerShortcut(shortcut, a);

    connect(a, &QAction::triggered, this, [=]() mutable {
        QJSValueList arguments;
        arguments << platform::self()->qmlEngine()->toScriptValue(a);
        function.call(arguments);
    });
    return true;
}

}
