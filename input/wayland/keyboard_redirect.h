/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/keyboard_redirect.h"
#include "input/spies/keyboard_repeat.h"
#include "input/spies/modifier_only_shortcuts.h"
#include "input/xkb/layout_manager.h"
#include "input/xkb/manager.h"

#include <KScreenLocker/KsldApp>
#include <memory>

namespace KWin::input::wayland
{

template<typename Redirect>
class key_state_changed_spy : public event_spy<Redirect>
{
public:
    key_state_changed_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
    {
    }

    void key(key_event const& event) override
    {
        Q_EMIT this->redirect.qobject->keyStateChanged(event.keycode, event.state);
    }
};

template<typename Redirect>
class modifiers_changed_spy : public event_spy<Redirect>
{
public:
    modifiers_changed_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
        , m_modifiers()
    {
    }

    void key(key_event const& event) override
    {
        if (auto& xkb = event.base.dev->xkb) {
            updateModifiers(xkb->qt_modifiers);
        }
    }

    void updateModifiers(Qt::KeyboardModifiers mods)
    {
        if (mods == m_modifiers) {
            return;
        }
        Q_EMIT this->redirect.qobject->keyboardModifiersChanged(mods, m_modifiers);
        m_modifiers = mods;
    }

private:
    Qt::KeyboardModifiers m_modifiers;
};

template<typename Redirect>
class keyboard_redirect
{
public:
    using platform_t = typename Redirect::platform_t;
    using space_t = typename platform_t::base_t::space_t;
    using window_t = typename space_t::window_t;
    using layout_manager_t = xkb::layout_manager<xkb::manager<platform_t>>;

    explicit keyboard_redirect(Redirect* redirect)
        : qobject{std::make_unique<keyboard_redirect_qobject>()}
        , redirect{redirect}
    {
    }

    void init()
    {
        auto& xkb = redirect->platform.xkb;
        auto const config = kwinApp()->kxkbConfig();
        xkb.numlock_config = kwinApp()->inputConfig();
        xkb.setConfig(config);

        redirect->m_spies.push_back(new key_state_changed_spy(*redirect));
        modifiers_spy = new modifiers_changed_spy(*redirect);
        redirect->m_spies.push_back(modifiers_spy);

        layout_manager = std::make_unique<layout_manager_t>(redirect->platform.xkb, config);

        if (waylandServer()->has_global_shortcut_support()) {
            redirect->m_spies.push_back(new modifier_only_shortcuts_spy(*redirect));
        }

        auto keyRepeatSpy = new keyboard_repeat_spy(*redirect);
        QObject::connect(keyRepeatSpy->qobject.get(),
                         &keyboard_repeat_spy_qobject::key_repeated,
                         qobject.get(),
                         [this](auto const& event) { process_key_repeat(event); });
        redirect->m_spies.push_back(keyRepeatSpy);

        QObject::connect(redirect->platform.base.space->qobject.get(),
                         &win::space::qobject_t::clientActivated,
                         qobject.get(),
                         [this] {
                             QObject::disconnect(m_activeClientSurfaceChangedConnection);
                             if (auto c = redirect->platform.base.space->active_client) {
                                 m_activeClientSurfaceChangedConnection
                                     = QObject::connect(c->qobject.get(),
                                                        &win::window_qobject::surfaceChanged,
                                                        qobject.get(),
                                                        [this] { update(); });
                             } else {
                                 m_activeClientSurfaceChangedConnection = QMetaObject::Connection();
                             }
                             update();
                         });
        if (waylandServer()->has_screen_locker_integration()) {
            QObject::connect(ScreenLocker::KSldApp::self(),
                             &ScreenLocker::KSldApp::lockStateChanged,
                             qobject.get(),
                             [this] { update(); });
        }
    }

    void update()
    {
        auto seat = waylandServer()->seat();
        if (!seat->hasKeyboard()) {
            return;
        }

        // TODO: this needs better integration
        window_t* found = nullptr;
        auto const& stacking = redirect->platform.base.space->stacking_order.stack;
        if (!stacking.empty()) {
            auto it = stacking.end();
            do {
                --it;
                auto t = (*it);
                if (t->remnant) {
                    // a deleted window doesn't get mouse events
                    continue;
                }
                if (!t->ready_for_painting) {
                    continue;
                }
                auto wlwin = dynamic_cast<typename space_t::wayland_window*>(t);
                if (!wlwin) {
                    continue;
                }
                if (!wlwin->layer_surface || !wlwin->has_exclusive_keyboard_interactivity()) {
                    continue;
                }
                found = t;
                break;
            } while (it != stacking.begin());
        }

        if (!found && !redirect->isSelectingWindow()) {
            found = redirect->platform.base.space->active_client;
        }
        if (found && found->surface) {
            if (found->surface != seat->keyboards().get_focus().surface) {
                seat->setFocusedKeyboardSurface(found->surface);
            }
        } else {
            seat->setFocusedKeyboardSurface(nullptr);
        }
    }

    void process_key(key_event const& event)
    {
        auto& xkb = event.base.dev->xkb;

        keyboard_redirect_prepare_key<Redirect>(*this, event);

        process_filters(redirect->m_filters,
                        std::bind(&event_filter<Redirect>::key, std::placeholders::_1, event));
        xkb->forward_modifiers();
    }

    void process_key_repeat(key_event const& event)
    {
        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::key_repeat, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::key_repeat, std::placeholders::_1, event));
    }

    void process_modifiers(modifiers_event const& event)
    {
        auto const& xkb = event.base.dev->xkb.get();

        // TODO: send to proper Client and also send when active Client changes
        xkb->update_modifiers(event.depressed, event.latched, event.locked, event.group);

        modifiers_spy->updateModifiers(xkb->qt_modifiers);
    }

    std::unique_ptr<keyboard_redirect_qobject> qobject;
    Redirect* redirect;

private:
    QMetaObject::Connection m_activeClientSurfaceChangedConnection;
    modifiers_changed_spy<Redirect>* modifiers_spy{nullptr};

    std::unique_ptr<layout_manager_t> layout_manager;
};

}
