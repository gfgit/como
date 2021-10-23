/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "helpers.h"

#include "main.h"
#include "options.h"
#include "win/input.h"
#include "workspace.h"
#include <input/event.h>
#include <input/keyboard_redirect.h>
#include <input/pointer_redirect.h>
#include <input/qt_event.h>

namespace KWin::input
{

bool get_modifier_command(uint32_t key, Options::MouseCommand& command)
{
    if (kwinApp()->input->redirect->keyboard()->modifiersRelevantForGlobalShortcuts()
        != options->commandAllModifier()) {
        return false;
    }
    if (kwinApp()->input->redirect->pointer()->isConstrained()) {
        return false;
    }
    if (workspace()->globalShortcutsDisabled()) {
        return false;
    }
    auto qt_key = button_to_qt_mouse_button(key);
    switch (qt_key) {
    case Qt::LeftButton:
        command = options->commandAll1();
        break;
    case Qt::MiddleButton:
        command = options->commandAll2();
        break;
    case Qt::RightButton:
        command = options->commandAll3();
        break;
    default:
        // nothing
        break;
    }
    return true;
}

std::pair<bool, bool> do_perform_mouse_action(Options::MouseCommand command, Toplevel* window)
{
    return std::make_pair(true,
                          !window->performMouseCommand(
                              command, kwinApp()->input->redirect->pointer()->pos().toPoint()));
}

std::pair<bool, bool> KWIN_EXPORT perform_mouse_modifier_action(button_event const& event,
                                                                Toplevel* window)
{
    auto command = Options::MouseNothing;
    auto was_action = get_modifier_command(event.key, command);

    return was_action ? do_perform_mouse_action(command, window) : std::make_pair(false, false);
}

std::pair<bool, bool> KWIN_EXPORT
perform_mouse_modifier_and_window_action(button_event const& event, Toplevel* window)
{
    auto command = Options::MouseNothing;
    auto was_action = get_modifier_command(event.key, command);

    if (!was_action) {
        command = win::get_mouse_command(window, button_to_qt_mouse_button(event.key), &was_action);
    }

    return was_action ? do_perform_mouse_action(command, window) : std::make_pair(false, false);
}

bool get_wheel_modifier_command(axis_orientation orientation,
                                double delta,
                                Options::MouseCommand& command)
{
    if (kwinApp()->input->redirect->keyboard()->modifiersRelevantForGlobalShortcuts()
        != options->commandAllModifier()) {
        return false;
    }
    if (kwinApp()->input->redirect->pointer()->isConstrained()) {
        return false;
    }
    if (workspace()->globalShortcutsDisabled()) {
        return false;
    }

    auto veritcal_delta = (orientation == axis_orientation::vertical) ? -1 * delta : 0;
    command = options->operationWindowMouseWheel(veritcal_delta);

    return true;
}

std::pair<bool, bool> KWIN_EXPORT perform_wheel_action(axis_event const& event, Toplevel* window)
{
    auto command = Options::MouseNothing;
    auto was_action = get_wheel_modifier_command(event.orientation, event.delta, command);

    return was_action ? do_perform_mouse_action(command, window) : std::make_pair(false, false);
}

std::pair<bool, bool> KWIN_EXPORT perform_wheel_and_window_action(axis_event const& event,
                                                                  Toplevel* window)
{
    auto command = Options::MouseNothing;
    auto was_action = get_wheel_modifier_command(event.orientation, event.delta, command);

    if (!was_action) {
        command = win::get_wheel_command(window, Qt::Vertical, &was_action);
    }

    return was_action ? do_perform_mouse_action(command, window) : std::make_pair(false, false);
}

}
