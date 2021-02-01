/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "net.h"
#include "remnant.h"

#include "rules/rules.h"

#include <KDesktopFile>
#include <klocalizedstring.h>

#include <QDir>
#include <QLatin1String>

namespace KWin::win
{

template<typename Win>
QString caption(Win* win)
{
    if (auto remnant = win->remnant()) {
        return remnant->caption;
    }
    QString cap = win->caption.normal + win->caption.suffix;
    if (win->control->unresponsive()) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

template<typename Win>
QString shortcut_caption_suffix(Win* win)
{
    if (win->control->shortcut().isEmpty()) {
        return QString();
    }
    return QLatin1String(" {") + win->control->shortcut().toString() + QLatin1Char('}');
}

template<typename Win>
void set_desktop_file_name(Win* win, QByteArray name)
{
    name = win->control->rules().checkDesktopFile(name).toUtf8();
    if (name == win->control->desktop_file_name()) {
        return;
    }
    win->control->set_desktop_file_name(name);
    win->updateWindowRules(Rules::DesktopFile);
    Q_EMIT win->desktopFileNameChanged();
}

template<typename Win>
QString icon_from_desktop_file(Win* win)
{
    auto const desktopFileName = QString::fromUtf8(win->control->desktop_file_name());
    QString desktopFilePath;

    if (QDir::isAbsolutePath(desktopFileName)) {
        desktopFilePath = desktopFileName;
    }

    if (desktopFilePath.isEmpty()) {
        desktopFilePath
            = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopFileName);
    }
    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileName + QLatin1String(".desktop"));
    }

    KDesktopFile df(desktopFilePath);
    return df.readIcon();
}

/**
 * Tells if @p win is "special", in contrast normal windows are with a border, can be moved by the
 * user, can be closed, etc.
 */
template<typename Win>
bool is_special_window(Win* win)
{
    return is_desktop(win) || is_dock(win) || is_splash(win) || is_toolbar(win)
        || is_notification(win) || is_critical_notification(win) || is_on_screen_display(win);
}

/**
 * Looks for another window with same captionNormal and captionSuffix.
 * If no such window exists @c nullptr is returned.
 */
template<typename Win>
Win* find_client_with_same_caption(Win const* win)
{
    auto fetchNameInternalPredicate = [win](Win const* cl) {
        return (!is_special_window(cl) || is_toolbar(cl)) && cl != win
            && cl->caption.normal == win->caption.normal
            && cl->caption.suffix == win->caption.suffix;
    };
    return workspace()->findAbstractClient(fetchNameInternalPredicate);
}

}
