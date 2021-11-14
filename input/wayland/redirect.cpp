/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard_redirect.h"
#include "platform.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

#include "input/global_shortcuts_manager.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/switch.h"
#include "input/touch.h"

// TODO(romangg): should only be included when KWIN_BUILD_TABBOX is defined.
#include "input/filters/tabbox.h"

#include "input/filters/decoration_event.h"
#include "input/filters/drag_and_drop.h"
#include "input/filters/effects.h"
#include "input/filters/fake_tablet.h"
#include "input/filters/forward.h"
#include "input/filters/global_shortcut.h"
#include "input/filters/internal_window.h"
#include "input/filters/lock_screen.h"
#include "input/filters/move_resize.h"
#include "input/filters/popup.h"
#include "input/filters/screen_edge.h"
#include "input/filters/terminate_server.h"
#include "input/filters/virtual_terminal.h"
#include "input/filters/window_action.h"
#include "input/filters/window_selector.h"
#include "input/spies/touch_hide_cursor.h"

#include "main.h"
#include "seat/session.h"
#include "wayland_server.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <KGlobalAccel>

namespace KWin::input::wayland
{

static Wrapland::Server::Seat* find_seat()
{
    return waylandServer()->seat();
}

redirect::redirect(wayland::platform* platform)
    : input::redirect(new keyboard_redirect(this),
                      new pointer_redirect,
                      new tablet_redirect,
                      new touch_redirect)
    , platform{platform}
    , config_watcher{KConfigWatcher::create(kwinApp()->inputConfig())}
{
    QObject::connect(kwinApp(), &Application::startup_finished, this, &redirect::setup_workspace);

    reconfigure();
    setup_devices();

    platform->update_keyboard_leds(m_keyboard->xkb()->leds());
    waylandServer()->updateKeyState(m_keyboard->xkb()->leds());
    QObject::connect(m_keyboard.get(),
                     &keyboard_redirect::ledsChanged,
                     waylandServer(),
                     &WaylandServer::updateKeyState);
    QObject::connect(m_keyboard.get(),
                     &keyboard_redirect::ledsChanged,
                     platform,
                     &platform::update_keyboard_leds);

    QObject::connect(
        config_watcher.data(), &KConfigWatcher::configChanged, this, [this](auto const& group) {
            if (group.name() == QLatin1String("Keyboard")) {
                reconfigure();
            }
        });
}

void redirect::setup_devices()
{
    QObject::connect(platform, &platform::pointer_added, this, &redirect::handle_pointer_added);
    QObject::connect(platform, &platform::pointer_removed, this, [this]() {
        if (auto seat = find_seat(); seat && platform->pointers.empty()) {
            seat->setHasPointer(false);
        }
    });

    QObject::connect(platform, &platform::keyboard_added, this, &redirect::handle_keyboard_added);
    QObject::connect(platform, &platform::keyboard_removed, this, [this]() {
        if (auto seat = find_seat(); seat && platform->keyboards.empty()) {
            seat->setHasKeyboard(false);
        }
    });

    QObject::connect(platform, &platform::touch_added, this, &redirect::handle_touch_added);
    QObject::connect(platform, &platform::touch_removed, this, [this]() {
        if (auto seat = find_seat(); seat && platform->touchs.empty()) {
            seat->setHasTouch(false);
        }
    });

    QObject::connect(platform, &platform::switch_added, this, &redirect::handle_switch_added);
}

void redirect::install_shortcuts()
{
    m_shortcuts = std::make_unique<input::global_shortcuts_manager>();
    m_shortcuts->init();
    setup_touchpad_shortcuts();
}

redirect::~redirect() = default;

void redirect::setup_touchpad_shortcuts()
{
    auto toggle_action = new QAction(this);
    auto on_action = new QAction(this);
    auto off_action = new QAction(this);

    constexpr auto const component{"kcm_touchpad"};

    toggle_action->setObjectName(QStringLiteral("Toggle Touchpad"));
    toggle_action->setProperty("componentName", component);
    on_action->setObjectName(QStringLiteral("Enable Touchpad"));
    on_action->setProperty("componentName", component);
    off_action->setObjectName(QStringLiteral("Disable Touchpad"));
    off_action->setProperty("componentName", component);

    KGlobalAccel::self()->setDefaultShortcut(toggle_action,
                                             QList<QKeySequence>{Qt::Key_TouchpadToggle});
    KGlobalAccel::self()->setShortcut(toggle_action, QList<QKeySequence>{Qt::Key_TouchpadToggle});
    KGlobalAccel::self()->setDefaultShortcut(on_action, QList<QKeySequence>{Qt::Key_TouchpadOn});
    KGlobalAccel::self()->setShortcut(on_action, QList<QKeySequence>{Qt::Key_TouchpadOn});
    KGlobalAccel::self()->setDefaultShortcut(off_action, QList<QKeySequence>{Qt::Key_TouchpadOff});
    KGlobalAccel::self()->setShortcut(off_action, QList<QKeySequence>{Qt::Key_TouchpadOff});

    registerShortcut(Qt::Key_TouchpadToggle, toggle_action);
    registerShortcut(Qt::Key_TouchpadOn, on_action);
    registerShortcut(Qt::Key_TouchpadOff, off_action);

    QObject::connect(toggle_action, &QAction::triggered, platform, &platform::toggle_touchpads);
    QObject::connect(on_action, &QAction::triggered, platform, &platform::enable_touchpads);
    QObject::connect(off_action, &QAction::triggered, platform, &platform::disable_touchpads);
}

void redirect::setup_workspace()
{
    fake_input = waylandServer()->display()->createFakeInput();
    QObject::connect(fake_input.get(),
                     &Wrapland::Server::FakeInput::deviceCreated,
                     this,
                     &redirect::handle_fake_input_device_added);

    static_cast<keyboard_redirect*>(m_keyboard.get())->init();
    static_cast<pointer_redirect*>(m_pointer.get())->init();
    static_cast<touch_redirect*>(m_touch.get())->init();
    static_cast<tablet_redirect*>(m_tablet.get())->init();

    setup_filters();
}

void redirect::setup_filters()
{
    auto const has_global_shortcuts = waylandServer()->hasGlobalShortcutSupport();

    if (kwinApp()->session->hasSessionControl() && has_global_shortcuts) {
        m_filters.emplace_back(new virtual_terminal_filter);
    }

    installInputEventSpy(new touch_hide_cursor_spy);
    if (has_global_shortcuts) {
        m_filters.emplace_back(new terminate_server_filter);
    }
    m_filters.emplace_back(new drag_and_drop_filter);
    m_filters.emplace_back(new lock_screen_filter);
    m_filters.emplace_back(new popup_filter);

    window_selector = new window_selector_filter;
    m_filters.push_back(window_selector);

    if (has_global_shortcuts) {
        m_filters.emplace_back(new screen_edge_filter);
    }
    m_filters.emplace_back(new effects_filter);
    m_filters.emplace_back(new move_resize_filter);

#ifdef KWIN_BUILD_TABBOX
    m_filters.emplace_back(new tabbox_filter);
#endif

    if (has_global_shortcuts) {
        m_filters.emplace_back(new global_shortcut_filter);
    }

    m_filters.emplace_back(new decoration_event_filter);
    m_filters.emplace_back(new internal_window_filter);

    m_filters.emplace_back(new window_action_filter);
    m_filters_install_iterator = m_filters.insert(m_filters.cend(), new forward_filter);
    m_filters.emplace_back(new fake_tablet_filter);
}

void redirect::reconfigure()
{
    auto input_config = config_watcher->config();
    auto const group = input_config->group(QStringLiteral("Keyboard"));

    auto delay = group.readEntry("RepeatDelay", 660);
    auto rate = group.readEntry("RepeatRate", 25);
    auto const repeat = group.readEntry("KeyRepeat", "repeat");

    // When the clients will repeat the character or turn repeat key events into an accent character
    // selection, we want to tell the clients that we are indeed repeating keys.
    auto enabled = repeat == QLatin1String("accent") || repeat == QLatin1String("repeat");

    if (waylandServer()->seat()->hasKeyboard()) {
        waylandServer()->seat()->keyboards().set_repeat_info(enabled ? rate : 0, delay);
    }
}

bool redirect::has_tablet_mode_switch()
{
    if (platform) {
        return std::any_of(platform->switches.cbegin(), platform->switches.cend(), [](auto dev) {
            return dev->control->is_tablet_mode_switch();
        });
    }
    return false;
}

void redirect::startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                               QByteArray const& cursorName)
{
    if (window_selector->isActive()) {
        callback(nullptr);
        return;
    }
    window_selector->start(callback);
    m_pointer->setWindowSelectionCursor(cursorName);
}

void redirect::startInteractivePositionSelection(std::function<void(QPoint const&)> callback)
{
    if (window_selector->isActive()) {
        callback(QPoint(-1, -1));
        return;
    }
    window_selector->start(callback);
    m_pointer->setWindowSelectionCursor(QByteArray());
}

bool redirect::isSelectingWindow() const
{
    // TODO(romangg): This function is called before setup_filters is run (from setup_workspace).
    //                Can we ensure it's only called afterwards and remove the nullptr check?
    return window_selector && window_selector->isActive();
}

void redirect::handle_pointer_added(input::pointer* pointer)
{
    auto pointer_red = m_pointer.get();

    QObject::connect(
        pointer, &pointer::button_changed, pointer_red, &input::pointer_redirect::process_button);

    QObject::connect(
        pointer, &pointer::motion, pointer_red, &input::pointer_redirect::process_motion);
    QObject::connect(pointer,
                     &pointer::motion_absolute,
                     pointer_red,
                     &input::pointer_redirect::process_motion_absolute);

    QObject::connect(
        pointer, &pointer::axis_changed, pointer_red, &input::pointer_redirect::process_axis);

    QObject::connect(
        pointer, &pointer::pinch_begin, pointer_red, &input::pointer_redirect::process_pinch_begin);
    QObject::connect(pointer,
                     &pointer::pinch_update,
                     pointer_red,
                     &input::pointer_redirect::process_pinch_update);
    QObject::connect(
        pointer, &pointer::pinch_end, pointer_red, &input::pointer_redirect::process_pinch_end);

    QObject::connect(
        pointer, &pointer::swipe_begin, pointer_red, &input::pointer_redirect::process_swipe_begin);
    QObject::connect(pointer,
                     &pointer::swipe_update,
                     pointer_red,
                     &input::pointer_redirect::process_swipe_update);
    QObject::connect(
        pointer, &pointer::swipe_end, pointer_red, &input::pointer_redirect::process_swipe_end);

    QObject::connect(
        pointer, &pointer::frame, pointer_red, &input::pointer_redirect::process_frame);

    if (auto seat = find_seat()) {
        seat->setHasPointer(true);
    }
}

void redirect::handle_keyboard_added(input::keyboard* keyboard)
{
    auto keyboard_red = m_keyboard.get();

    QObject::connect(
        keyboard, &keyboard::key_changed, keyboard_red, &input::keyboard_redirect::process_key);
    QObject::connect(keyboard,
                     &keyboard::modifiers_changed,
                     keyboard_red,
                     &input::keyboard_redirect::process_modifiers);

    if (auto seat = find_seat(); seat && !seat->hasKeyboard()) {
        seat->setHasKeyboard(true);
        reconfigure();
    }
}

void redirect::handle_touch_added(input::touch* touch)
{
    auto touch_red = m_touch.get();

    QObject::connect(touch, &touch::down, touch_red, &input::touch_redirect::process_down);
    QObject::connect(touch, &touch::up, touch_red, &input::touch_redirect::process_up);
    QObject::connect(touch, &touch::motion, touch_red, &input::touch_redirect::process_motion);
    QObject::connect(touch, &touch::cancel, touch_red, &input::touch_redirect::cancel);
#if HAVE_WLR_TOUCH_FRAME
    QObject::connect(touch, &touch::frame, touch_red, &input::touch_redirect::frame);
#endif

    if (auto seat = find_seat()) {
        seat->setHasTouch(true);
    }
}

void redirect::handle_switch_added(input::switch_device* switch_device)
{
    QObject::connect(switch_device, &switch_device::toggle, this, [this](auto const& event) {
        if (event.type == switch_type::tablet_mode) {
            Q_EMIT has_tablet_mode_switch_changed(event.state == switch_state::on);
        }
    });
}

void redirect::handle_fake_input_device_added(Wrapland::Server::FakeInputDevice* device)
{
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::authenticationRequested,
                     this,
                     [this, device](auto const& /*application*/, auto const& /*reason*/) {
                         // TODO: make secure
                         device->setAuthentication(true);
                     });

    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerMotionRequested,
                     this,
                     [this](auto const& delta) {
                         // TODO: Fix time
                         m_pointer->processMotion(
                             globalPointer() + QPointF(delta.width(), delta.height()), 0);
                         waylandServer()->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerMotionAbsoluteRequested,
                     this,
                     [this](auto const& pos) {
                         // TODO: Fix time
                         m_pointer->processMotion(pos, 0);
                         waylandServer()->simulateUserActivity();
                     });

    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerButtonPressRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         m_pointer->process_button({button, button_state::pressed, {nullptr, 0}});
                         waylandServer()->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerButtonReleaseRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         m_pointer->process_button({button, button_state::released, {nullptr, 0}});
                         waylandServer()->simulateUserActivity();
                     });
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::pointerAxisRequested,
        this,
        [this](auto orientation, auto delta) {
            // TODO: Fix time
            auto axis = (orientation == Qt::Horizontal) ? axis_orientation::horizontal
                                                        : axis_orientation::vertical;
            // TODO: Fix time
            m_pointer->process_axis({axis_source::unknown, axis, delta, 0, nullptr, 0});
            waylandServer()->simulateUserActivity();
        });

    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchDownRequested,
                     this,
                     [this](auto id, auto const& pos) {
                         // TODO: Fix time
                         m_touch->process_down({static_cast<int32_t>(id), pos, nullptr, 0});
                         waylandServer()->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchMotionRequested,
                     this,
                     [this](auto id, auto const& pos) {
                         // TODO: Fix time
                         m_touch->process_motion({static_cast<int32_t>(id), pos, nullptr, 0});
                         waylandServer()->simulateUserActivity();
                     });
    QObject::connect(
        device, &Wrapland::Server::FakeInputDevice::touchUpRequested, this, [this](auto id) {
            // TODO: Fix time
            m_touch->process_up({static_cast<int32_t>(id), nullptr, 0});
            waylandServer()->simulateUserActivity();
        });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchCancelRequested,
                     this,
                     [this]() { m_touch->cancel(); });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchFrameRequested,
                     this,
                     [this]() { m_touch->frame(); });

    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::keyboardKeyPressRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         m_keyboard->process_key({button, key_state::pressed, false, nullptr, 0});
                         waylandServer()->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::keyboardKeyReleaseRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         m_keyboard->process_key({button, key_state::released, false, nullptr, 0});
                         waylandServer()->simulateUserActivity();
                     });
}

}
