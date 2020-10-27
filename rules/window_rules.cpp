/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_rules.h"

#include <kconfig.h>

#ifndef KCMRULES
#include "client_machine.h"
#include "screens.h"
#include "win/win.h"
#include "workspace.h"
#include "x11client.h"
#endif

#include "rule_book.h"
#include "rule_book_settings.h"
#include "rule_settings.h"

namespace KWin
{

WindowRules::WindowRules(const QVector<Rules*>& r)
    : rules(r)
{
}

WindowRules::WindowRules()
{
}

bool WindowRules::contains(const Rules* rule) const
{
    return rules.contains(const_cast<Rules*>(rule));
}

void WindowRules::remove(Rules* rule)
{
    rules.removeOne(rule);
}

#ifndef KCMRULES
void WindowRules::discardTemporary()
{
    QVector<Rules*>::Iterator it2 = rules.begin();
    for (QVector<Rules*>::Iterator it = rules.begin(); it != rules.end();) {
        if ((*it)->discardTemporary(true))
            ++it;
        else {
            *it2++ = *it++;
        }
    }
    rules.erase(it2, rules.end());
}

void WindowRules::update(AbstractClient* c, int selection)
{
    bool updated = false;
    for (QVector<Rules*>::ConstIterator it = rules.constBegin(); it != rules.constEnd(); ++it)
        if ((*it)->update(c, selection)) // no short-circuiting here
            updated = true;
    if (updated)
        RuleBook::self()->requestDiskStorage();
}

QRect WindowRules::checkGeometry(QRect rect, bool init) const
{
    return QRect(checkPosition(rect.topLeft(), init), checkSize(rect.size(), init));
}

QPoint WindowRules::checkPosition(QPoint pos, bool init) const
{
    return check_set(pos, init, &Rules::applyPosition);
}

QSize WindowRules::checkSize(QSize s, bool init) const
{
    return check_set(s, init, &Rules::applySize);
}

bool WindowRules::checkIgnoreGeometry(bool ignore, bool init) const
{
    return check_set(ignore, init, &Rules::applyIgnoreGeometry);
}

int WindowRules::checkDesktop(int desktop, bool init) const
{
    return check_set(desktop, init, &Rules::applyDesktop);
}

QString WindowRules::checkActivity(QString activity, bool init) const
{
    return check_set(activity, init, &Rules::applyActivity);
}

win::maximize_mode WindowRules::checkMaximizeVert(win::maximize_mode mode, bool init) const
{
    return check_set(mode, init, &Rules::applyMaximizeVert);
}

win::maximize_mode WindowRules::checkMaximizeHoriz(win::maximize_mode mode, bool init) const
{
    return check_set(mode, init, &Rules::applyMaximizeHoriz);
}

bool WindowRules::checkMinimize(bool minimized, bool init) const
{
    return check_set(minimized, init, &Rules::applyMinimize);
}

ShadeMode WindowRules::checkShade(ShadeMode shade, bool init) const
{
    return check_set(shade, init, &Rules::applyShade);
}

bool WindowRules::checkSkipTaskbar(bool skip, bool init) const
{
    return check_set(skip, init, &Rules::applySkipTaskbar);
}

bool WindowRules::checkSkipPager(bool skip, bool init) const
{
    return check_set(skip, init, &Rules::applySkipPager);
}

bool WindowRules::checkSkipSwitcher(bool skip, bool init) const
{
    return check_set(skip, init, &Rules::applySkipSwitcher);
}

bool WindowRules::checkKeepAbove(bool above, bool init) const
{
    return check_set(above, init, &Rules::applyKeepAbove);
}

bool WindowRules::checkKeepBelow(bool below, bool init) const
{
    return check_set(below, init, &Rules::applyKeepBelow);
}

bool WindowRules::checkFullScreen(bool fs, bool init) const
{
    return check_set(fs, init, &Rules::applyFullScreen);
}

bool WindowRules::checkNoBorder(bool noborder, bool init) const
{
    return check_set(noborder, init, &Rules::applyNoBorder);
}

QString WindowRules::checkShortcut(QString s, bool init) const
{
    return check_set(s, init, &Rules::applyShortcut);
}

QString WindowRules::checkDesktopFile(QString desktopFile, bool init) const
{
    return check_set(desktopFile, init, &Rules::applyDesktopFile);
}

Placement::Policy WindowRules::checkPlacement(Placement::Policy placement) const
{
    return check_force(placement, &Rules::applyPlacement);
}

QSize WindowRules::checkMinSize(QSize s) const
{
    return check_force(s, &Rules::applyMinSize);
}

QSize WindowRules::checkMaxSize(QSize s) const
{
    return check_force(s, &Rules::applyMaxSize);
}

int WindowRules::checkOpacityActive(int s) const
{
    return check_force(s, &Rules::applyOpacityActive);
}

int WindowRules::checkOpacityInactive(int s) const
{
    return check_force(s, &Rules::applyOpacityInactive);
}

NET::WindowType WindowRules::checkType(NET::WindowType type) const
{
    return check_force(type, &Rules::applyType);
}

QString WindowRules::checkDecoColor(QString schemeFile) const
{
    return check_force(schemeFile, &Rules::applyDecoColor);
}

bool WindowRules::checkBlockCompositing(bool block) const
{
    return check_force(block, &Rules::applyBlockCompositing);
}

int WindowRules::checkFSP(int fsp) const
{
    return check_force(fsp, &Rules::applyFSP);
}

int WindowRules::checkFPP(int fpp) const
{
    return check_force(fpp, &Rules::applyFPP);
}

bool WindowRules::checkAcceptFocus(bool focus) const
{
    return check_force(focus, &Rules::applyAcceptFocus);
}

bool WindowRules::checkCloseable(bool closeable) const
{
    return check_force(closeable, &Rules::applyCloseable);
}

bool WindowRules::checkAutogrouping(bool autogroup) const
{
    return check_force(autogroup, &Rules::applyAutogrouping);
}

bool WindowRules::checkAutogroupInForeground(bool fg) const
{
    return check_force(fg, &Rules::applyAutogroupInForeground);
}

QString WindowRules::checkAutogroupById(QString id) const
{
    return check_force(id, &Rules::applyAutogroupById);
}

bool WindowRules::checkStrictGeometry(bool strict) const
{
    return check_force(strict, &Rules::applyStrictGeometry);
}

bool WindowRules::checkDisableGlobalShortcuts(bool disable) const
{
    return check_force(disable, &Rules::applyDisableGlobalShortcuts);
}

win::maximize_mode WindowRules::checkMaximize(win::maximize_mode mode, bool init) const
{
    auto vert = checkMaximizeVert(mode, init) & win::maximize_mode::vertical;
    auto horiz = checkMaximizeHoriz(mode, init) & win::maximize_mode::horizontal;
    return vert | horiz;
}

int WindowRules::checkScreen(int screen, bool init) const
{
    if (rules.count() == 0)
        return screen;
    int ret = screen;
    for (QVector<Rules*>::ConstIterator it = rules.constBegin(); it != rules.constEnd(); ++it) {
        if ((*it)->applyScreen(ret, init))
            break;
    }
    if (ret >= Screens::self()->count())
        ret = screen;
    return ret;
}

// Client

void AbstractClient::setupWindowRules(bool ignore_temporary)
{
    disconnect(this, &AbstractClient::captionChanged, this, &AbstractClient::evaluateWindowRules);
    m_rules = RuleBook::self()->find(this, ignore_temporary);
    // check only after getting the rules, because there may be a rule forcing window type
}

// Applies Force, ForceTemporarily and ApplyNow rules
// Used e.g. after the rules have been modified using the kcm.
void AbstractClient::applyWindowRules()
{
    // apply force rules
    // Placement - does need explicit update, just like some others below
    // Geometry : setGeometry() doesn't check rules
    auto client_rules = rules();
    QRect orig_geom = QRect(pos(), sizeForClientSize(clientSize())); // handle shading
    QRect geom = client_rules->checkGeometry(orig_geom);
    if (geom != orig_geom)
        setFrameGeometry(geom);
    // MinSize, MaxSize handled by Geometry
    // IgnoreGeometry
    win::set_desktop(this, desktop());
    workspace()->sendClientToScreen(this, screen());
    setOnActivities(activities());
    // Type
    win::maximize(this, maximizeMode());

    // Minimize : functions don't check
    win::set_minimized(this, client_rules->checkMinimize(control()->minimized()));

    setShade(shadeMode());
    win::set_original_skip_taskbar(this, control()->skip_taskbar());
    win::set_skip_pager(this, control()->skip_pager());
    win::set_skip_switcher(this, control()->skip_switcher());
    win::set_keep_above(this, control()->keep_above());
    win::set_keep_below(this, control()->keep_below());
    setFullScreen(isFullScreen(), true);
    setNoBorder(noBorder());
    updateColorScheme();
    // FSP
    // AcceptFocus :
    if (workspace()->mostRecentlyActivatedClient() == this && !client_rules->checkAcceptFocus(true))
        workspace()->activateNextClient(this);
    // Closeable
    auto s = win::adjusted_size(this);
    if (s != size() && s.isValid())
        resizeWithChecks(s);
    // Autogrouping : Only checked on window manage
    // AutogroupInForeground : Only checked on window manage
    // AutogroupById : Only checked on window manage
    // StrictGeometry
    win::set_shortcut(this, rules()->checkShortcut(control()->shortcut().toString()));
    // see also X11Client::setActive()
    if (control()->active()) {
        setOpacity(rules()->checkOpacityActive(qRound(opacity() * 100.0)) / 100.0);
        workspace()->disableGlobalShortcutsForClient(rules()->checkDisableGlobalShortcuts(false));
    } else
        setOpacity(rules()->checkOpacityInactive(qRound(opacity() * 100.0)) / 100.0);
    setDesktopFileName(rules()->checkDesktopFile(desktopFileName()).toUtf8());
}

void X11Client::updateWindowRules(Rules::Types selection)
{
    if (!isManaged()) // not fully setup yet
        return;
    AbstractClient::updateWindowRules(selection);
}

void AbstractClient::updateWindowRules(Rules::Types selection)
{
    if (RuleBook::self()->areUpdatesDisabled())
        return;
    m_rules.update(this, selection);
}

void AbstractClient::finishWindowRules()
{
    updateWindowRules(Rules::All);
    m_rules = WindowRules();
}

#endif

}
