/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_SETUP_H
#define KWIN_WIN_SETUP_H

#include "appmenu.h"
#include "deco.h"
#include "meta.h"
#include "placement.h"
#include "screen.h"

#include "decorations/decorationbridge.h"
#include "rules/rule_book.h"
#include "wayland_server.h"

#include <KDecoration2/Decoration>
#include <Wrapland/Server/plasma_window.h>

namespace KWin::win
{

template<typename Win>
void setup_rules(Win* win, bool ignore_temporary)
{
    // TODO(romangg): This disconnects all connections of captionChanged to the window itself.
    //                There is only one so this works fine but it's not robustly specified.
    //                Either reshuffle later or use explicit connection object.
    QObject::disconnect(win, &Win::captionChanged, win, nullptr);
    win->control->set_rules(RuleBook::self()->find(win, ignore_temporary));
    // check only after getting the rules, because there may be a rule forcing window type
    // TODO(romangg): what does this mean?
}

template<typename Win>
void evaluate_rules(Win* win)
{
    setup_rules(win, true);
    win->applyWindowRules();
}

template<typename Win>
void setup_connections(Win* win)
{
    QObject::connect(win, &Win::clientStartUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(win, &Win::clientFinishUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(
        win, &Win::clientStartUserMovedResized, win, &Win::removeCheckScreenConnection);
    QObject::connect(
        win, &Win::clientFinishUserMovedResized, win, &Win::setupCheckScreenConnection);

    QObject::connect(win, &Win::paletteChanged, win, [win] { trigger_decoration_repaint(win); });

    QObject::connect(Decoration::DecorationBridge::self(), &QObject::destroyed, win, [win] {
        win->control->destroy_decoration();
    });

    // Replace on-screen-display on size changes.
    QObject::connect(win,
                     &Win::frame_geometry_changed,
                     win,
                     [win]([[maybe_unused]] Toplevel* toplevel, QRect const& old) {
                         if (!is_on_screen_display(win)) {
                             // Not an on-screen-display.
                             return;
                         }
                         if (win->frameGeometry().isEmpty()) {
                             // No current geometry to set.
                             return;
                         }
                         if (old.size() == win->frameGeometry().size()) {
                             // No change.
                             return;
                         }
                         if (win->isInitialPositionSet()) {
                             // Position (geometry?) already set.
                             return;
                         }
                         geometry_updates_blocker blocker(win);

                         auto const area = workspace()->clientArea(
                             PlacementArea, Screens::self()->current(), win->desktop());

                         win::place(win, area);
                     });

    QObject::connect(
        ApplicationMenu::self(), &ApplicationMenu::applicationMenuEnabledChanged, win, [win] {
            Q_EMIT win->hasApplicationMenuChanged(win->control->has_application_menu());
        });
}

template<typename Win>
void setup_wayland_plasma_management(Win* win)
{
    if (win->control->wayland_management()) {
        // Already setup.
        return;
    }
    if (!waylandServer() || !win->surface()) {
        return;
    }
    if (!waylandServer()->window_management()) {
        return;
    }
    auto plasma_win
        = waylandServer()->window_management()->createWindow(waylandServer()->window_management());
    plasma_win->setTitle(caption(win));
    plasma_win->setActive(win->control->active());
    plasma_win->setFullscreen(win->control->fullscreen());
    plasma_win->setKeepAbove(win->control->keep_above());
    plasma_win->setKeepBelow(win->control->keep_below());
    plasma_win->setMaximized(win->maximizeMode() == win::maximize_mode::full);
    plasma_win->setMinimized(win->control->minimized());
    plasma_win->setOnAllDesktops(win->isOnAllDesktops());
    plasma_win->setDemandsAttention(win->control->demands_attention());
    plasma_win->setCloseable(win->isCloseable());
    plasma_win->setMaximizeable(win->isMaximizable());
    plasma_win->setMinimizeable(win->isMinimizable());
    plasma_win->setFullscreenable(win->control->can_fullscreen());
    plasma_win->setIcon(win->control->icon());
    auto updateAppId = [win, plasma_win] {
        auto const name = win->control->desktop_file_name();
        plasma_win->setAppId(QString::fromUtf8(name.isEmpty() ? win->resourceClass() : name));
    };
    updateAppId();
    plasma_win->setSkipTaskbar(win->control->skip_taskbar());
    plasma_win->setSkipSwitcher(win->control->skip_switcher());
    plasma_win->setPid(win->pid());
    plasma_win->setResizable(win->isResizable());
    plasma_win->setMovable(win->isMovable());
    auto const& [name, path] = win->control->application_menu();
    plasma_win->setApplicationMenuPaths(name, path);

    // FIXME Matches X11Client::actionSupported(), but both should be implemented.
    plasma_win->setVirtualDesktopChangeable(true);

    auto transient_lead = win->transient()->lead();
    plasma_win->setParentWindow(transient_lead ? transient_lead->control->wayland_management()
                                               : nullptr);
    plasma_win->setGeometry(win->frameGeometry());
    QObject::connect(win, &Win::skipTaskbarChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipTaskbar(win->control->skip_taskbar());
    });
    QObject::connect(win, &Win::skipSwitcherChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipSwitcher(win->control->skip_switcher());
    });
    QObject::connect(win, &Win::captionChanged, plasma_win, [plasma_win, win] {
        plasma_win->setTitle(caption(win));
    });

    QObject::connect(win, &Win::activeChanged, plasma_win, [plasma_win, win] {
        plasma_win->setActive(win->control->active());
    });
    QObject::connect(win, &Win::fullScreenChanged, plasma_win, [plasma_win, win] {
        plasma_win->setFullscreen(win->control->fullscreen());
    });
    QObject::connect(
        win, &Win::keepAboveChanged, plasma_win, &Wrapland::Server::PlasmaWindow::setKeepAbove);
    QObject::connect(
        win, &Win::keepBelowChanged, plasma_win, &Wrapland::Server::PlasmaWindow::setKeepBelow);
    QObject::connect(win, &Win::minimizedChanged, plasma_win, [plasma_win, win] {
        plasma_win->setMinimized(win->control->minimized());
    });
    QObject::connect(win,
                     static_cast<void (Win::*)(Toplevel*, win::maximize_mode)>(
                         &Win::clientMaximizedStateChanged),
                     plasma_win,
                     [plasma_win]([[maybe_unused]] Toplevel* c, win::maximize_mode mode) {
                         plasma_win->setMaximized(mode == win::maximize_mode::full);
                     });
    QObject::connect(win, &Win::demandsAttentionChanged, plasma_win, [plasma_win, win] {
        plasma_win->setDemandsAttention(win->control->demands_attention());
    });
    QObject::connect(win, &Win::iconChanged, plasma_win, [plasma_win, win] {
        plasma_win->setIcon(win->control->icon());
    });
    QObject::connect(win, &Win::windowClassChanged, plasma_win, updateAppId);
    QObject::connect(win, &Win::desktopFileNameChanged, plasma_win, updateAppId);
    QObject::connect(win, &Win::transientChanged, plasma_win, [plasma_win, win] {
        auto lead = win->transient()->lead();
        if (lead && !lead->control) {
            // When lead becomes remnant.
            lead = nullptr;
        }
        plasma_win->setParentWindow(lead ? lead->control->wayland_management() : nullptr);
    });
    QObject::connect(win, &Win::applicationMenuChanged, plasma_win, [plasma_win, win] {
        auto const& [name, path] = win->control->application_menu();
        plasma_win->setApplicationMenuPaths(name, path);
    });
    QObject::connect(win, &Win::frame_geometry_changed, plasma_win, [plasma_win, win] {
        plasma_win->setGeometry(win->frameGeometry());
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::closeRequested, win, [win] {
        win->closeWindow();
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::moveRequested, win, [win] {
        auto cursor = input::get_cursor();
        cursor->set_pos(win->frameGeometry().center());
        win->performMouseCommand(Options::MouseMove, cursor->pos());
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::resizeRequested, win, [win] {
        auto cursor = input::get_cursor();
        cursor->set_pos(win->frameGeometry().bottomRight());
        win->performMouseCommand(Options::MouseResize, cursor->pos());
    });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::fullscreenRequested,
                     win,
                     [win](bool set) { win->setFullScreen(set, false); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::minimizedRequested, win, [win](bool set) {
            if (set) {
                set_minimized(win, true);
            } else {
                set_minimized(win, false);
            }
        });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::maximizedRequested, win, [win](bool set) {
            win::maximize(win, set ? win::maximize_mode::full : win::maximize_mode::restore);
        });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepAboveRequested,
                     win,
                     [win](bool set) { win::set_keep_above(win, set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepBelowRequested,
                     win,
                     [win](bool set) { win::set_keep_below(win, set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::demandsAttentionRequested,
                     win,
                     [win](bool set) { win::set_demands_attention(win, set); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::activeRequested, win, [win](bool set) {
            if (set) {
                workspace()->activateClient(win, true);
            }
        });

    for (auto const vd : win->desktops()) {
        plasma_win->addPlasmaVirtualDesktop(vd->id());
    }

    // Only for the legacy mechanism.
    QObject::connect(win, &Win::desktopChanged, plasma_win, [plasma_win, win] {
        if (win->isOnAllDesktops()) {
            plasma_win->setOnAllDesktops(true);
            return;
        }
        plasma_win->setOnAllDesktops(false);
    });

    // Plasma Virtual desktop management
    // show/hide when the window enters/exits from desktop
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::enterPlasmaVirtualDesktopRequested,
                     win,
                     [win](const QString& desktopId) {
                         VirtualDesktop* vd
                             = VirtualDesktopManager::self()->desktopForId(desktopId.toUtf8());
                         if (vd) {
                             enter_desktop(win, vd);
                         }
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::enterNewPlasmaVirtualDesktopRequested,
                     win,
                     [win]() {
                         VirtualDesktopManager::self()->setCount(
                             VirtualDesktopManager::self()->count() + 1);
                         enter_desktop(win, VirtualDesktopManager::self()->desktops().last());
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::leavePlasmaVirtualDesktopRequested,
                     win,
                     [win](const QString& desktopId) {
                         VirtualDesktop* vd
                             = VirtualDesktopManager::self()->desktopForId(desktopId.toUtf8());
                         if (vd) {
                             leave_desktop(win, vd);
                         }
                     });

    win->control->set_wayland_management(plasma_win);
}

}

#endif
