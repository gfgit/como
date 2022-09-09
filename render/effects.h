/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/screen_impl.h"
#include "effect/window_impl.h"
#include "effect_loader.h"
#include "scene.h"
#include "types.h"
#include "x11/effect.h"
#include "x11/property_notify_filter.h"

#include "desktop/screen_locker_watcher.h"
#include "win/activation.h"
#include "win/osd.h"
#include "win/screen_edges.h"
#include "win/session_manager.h"
#include "win/space_qobject.h"
#include "win/stacking_order.h"
#include "win/types.h"
#include "win/virtual_desktops.h"
#include "win/x11/stacking.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

#include "config-kwin.h"

#include <kwineffects/effect.h>
#include <kwineffects/effect_frame.h>
#include <kwineffects/effect_screen.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>

#include <Plasma/FrameSvg>
#include <QHash>
#include <QMouseEvent>
#include <memory>
#include <set>

namespace Wrapland::Server
{
class Display;
}

namespace KWin::render
{

/// Implements all QObject-specific functioanlity of EffectsHandler.
class KWIN_EXPORT effects_handler_wrap : public EffectsHandler
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Effects")
    Q_PROPERTY(QStringList activeEffects READ activeEffects)
    Q_PROPERTY(QStringList loadedEffects READ loadedEffects)
    Q_PROPERTY(QStringList listOfEffects READ listOfEffects)
public:
    effects_handler_wrap(CompositingType type);
    ~effects_handler_wrap() override;

    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void paintScreen(int mask, const QRegion& region, ScreenPaintData& data) override;
    /**
     * Special hook to perform a paintScreen but just with the windows on @p desktop.
     */
    void paintDesktop(int desktop, int mask, QRegion region, ScreenPaintData& data);
    void postPaintScreen() override;
    void prePaintWindow(EffectWindow* w,
                        WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void
    paintWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;
    void postPaintWindow(EffectWindow* w) override;

    Effect* provides(Effect::Feature ef);

    void
    drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;

    void buildQuads(EffectWindow* w, WindowQuadList& quadList) override;

    QString currentActivity() const override;
    int desktopGridWidth() const override;
    int desktopGridHeight() const override;
    int workspaceWidth() const override;
    int workspaceHeight() const override;

    bool optionRollOverDesktops() const override;

    bool grabKeyboard(Effect* effect) override;
    void ungrabKeyboard() override;
    // not performing XGrabPointer
    void startMouseInterception(Effect* effect, Qt::CursorShape shape) override;
    void stopMouseInterception(Effect* effect) override;
    bool isMouseInterception() const;
    void* getProxy(QString name) override;

    void setElevatedWindow(KWin::EffectWindow* w, bool set) override;

    void setActiveFullScreenEffect(Effect* e) override;
    Effect* activeFullScreenEffect() const override;
    bool hasActiveFullScreenEffect() const override;

    double animationTimeFactor() const override;
    WindowQuadType newWindowQuadType() override;

    bool checkInputWindowEvent(QMouseEvent* e);
    bool checkInputWindowEvent(QWheelEvent* e);
    void checkInputWindowStacking();

    void reconfigure() override;

    bool hasDecorationShadows() const override;
    bool decorationsHaveAlpha() const override;

    EffectFrame* effectFrame(EffectFrameStyle style,
                             bool staticSize,
                             const QPoint& position,
                             Qt::Alignment alignment) const override;

    bool isScreenLocked() const override;

    xcb_connection_t* xcbConnection() const override;
    xcb_window_t x11RootWindow() const override;

    // internal (used by kwin core or compositing code)
    void startPaint();
    void grabbedKeyboardEvent(QKeyEvent* e);
    bool hasKeyboardGrab() const;

    void reloadEffect(Effect* effect) override;
    QStringList loadedEffects() const;
    QStringList listOfEffects() const;
    void unloadAllEffects();

    QList<EffectWindow*> elevatedWindows() const;
    QStringList activeEffects() const;

    /**
     * @returns Whether we are currently in a desktop rendering process triggered by paintDesktop
     * hook
     */
    bool isDesktopRendering() const
    {
        return m_desktopRendering;
    }
    /**
     * @returns the desktop currently being rendered in the paintDesktop hook.
     */
    int currentRenderedDesktop() const
    {
        return m_currentRenderedDesktop;
    }

    Wrapland::Server::Display* waylandDisplay() const override;

    KSharedConfigPtr config() const override;
    KSharedConfigPtr inputConfig() const override;

    bool touchDown(qint32 id, const QPointF& pos, quint32 time);
    bool touchMotion(qint32 id, const QPointF& pos, quint32 time);
    bool touchUp(qint32 id, quint32 time);

    void highlightWindows(const QVector<EffectWindow*>& windows);

    /**
     * Finds an effect with the given name.
     *
     * @param name The name of the effect.
     * @returns The effect with the given name @p name, or nullptr if there
     *     is no such effect loaded.
     */
    Effect* findEffect(const QString& name) const;

    QImage blit_from_framebuffer(QRect const& geometry, double scale) const override;
    bool invert_screen();

    using PropertyEffectMap = QHash<QByteArray, QList<Effect*>>;
    PropertyEffectMap m_propertiesForEffects;
    QHash<QByteArray, qulonglong> m_managedProperties;
    QHash<long, int> registered_atoms;

public Q_SLOTS:
    // slots for D-Bus interface
    Q_SCRIPTABLE void reconfigureEffect(const QString& name);
    Q_SCRIPTABLE bool loadEffect(const QString& name);
    Q_SCRIPTABLE void toggleEffect(const QString& name);
    Q_SCRIPTABLE void unloadEffect(const QString& name);
    Q_SCRIPTABLE bool isEffectLoaded(const QString& name) const override;
    Q_SCRIPTABLE bool isEffectSupported(const QString& name);
    Q_SCRIPTABLE QList<bool> areEffectsSupported(const QStringList& names);
    Q_SCRIPTABLE QString supportInformation(const QString& name) const;
    Q_SCRIPTABLE QString debug(const QString& name, const QString& parameter = QString()) const;

protected:
    void slotCurrentTabAboutToChange(EffectWindow* from, EffectWindow* to);
    void slotTabAdded(EffectWindow* from, EffectWindow* to);
    void slotTabRemoved(EffectWindow* c, EffectWindow* newActiveWindow);

    void effectsChanged();

    virtual void final_paint_screen(paint_type mask, QRegion const& region, ScreenPaintData& data)
        = 0;

    virtual void final_paint_window(EffectWindow* window,
                                    paint_type mask,
                                    QRegion const& region,
                                    WindowPaintData& data)
        = 0;
    virtual void final_draw_window(EffectWindow* window,
                                   paint_type mask,
                                   QRegion const& region,
                                   WindowPaintData& data)
        = 0;

    /**
     * Default implementation does nothing and returns @c true.
     */
    virtual bool doGrabKeyboard();
    /**
     * Default implementation does nothing.
     */
    virtual void doUngrabKeyboard();

    virtual void doStartMouseInterception(Qt::CursorShape shape) = 0;
    virtual void doStopMouseInterception() = 0;

    /**
     * Default implementation does nothing
     */
    virtual void doCheckInputWindowStacking();

    virtual void handle_effect_destroy(Effect& effect) = 0;

    Effect* keyboard_grab_effect{nullptr};
    Effect* fullscreen_effect{nullptr};
    QList<EffectWindow*> elevated_windows;
    QMultiMap<int, EffectPair> effect_order;
    int next_window_quad_type{EFFECT_QUAD_TYPE_START};

private:
    void destroyEffect(Effect* effect);

    typedef QVector<Effect*> EffectsList;
    typedef EffectsList::const_iterator EffectsIterator;
    EffectsList m_activeEffects;
    EffectsIterator m_currentDrawWindowIterator;
    EffectsIterator m_currentPaintWindowIterator;
    EffectsIterator m_currentPaintScreenIterator;
    EffectsIterator m_currentBuildQuadsIterator;
    bool m_desktopRendering{false};
    int m_currentRenderedDesktop{0};
    QList<Effect*> m_grabbedMouseEffects;
    effect_loader* m_effectLoader;
};

template<typename Compositor>
class effects_handler_impl : public effects_handler_wrap
{
public:
    using platform_t = typename Compositor::platform_t;
    using base_t = typename platform_t::base_t;
    using space_t = typename platform_t::space_t;
    using scene_t = typename Compositor::scene_t;
    using effect_window_t = typename scene_t::effect_window_t;

    effects_handler_impl(Compositor& compositor)
        : effects_handler_wrap(compositor.scene->compositingType())
        , compositor{compositor}
    {
        singleton_interface::register_thumbnail = [this](auto& eff_win, auto& thumbnail) {
            auto& impl_win = static_cast<effect_window_t&>(eff_win);
            impl_win.registerThumbnail(&thumbnail);
        };

        QObject::connect(
            this, &effects_handler_impl::hasActiveFullScreenEffectChanged, this, [this] {
                Q_EMIT this->compositor.space->edges->qobject->checkBlocking();
            });

        auto ws = this->compositor.space;
        auto& vds = ws->virtual_desktop_manager;

        connect(ws->qobject.get(),
                &win::space_qobject::showingDesktopChanged,
                this,
                &effects_handler_wrap::showingDesktopChanged);
        connect(ws->qobject.get(),
                &win::space_qobject::currentDesktopChanged,
                this,
                [this, space = ws](int old) {
                    auto c = space->move_resize_window;
                    int const newDesktop
                        = this->compositor.space->virtual_desktop_manager->current();
                    if (old != 0 && newDesktop != old) {
                        assert(!c || c->render);
                        assert(!c || c->render->effect);
                        auto eff_win = c ? c->render->effect.get() : nullptr;
                        Q_EMIT desktopChanged(old, newDesktop, eff_win);
                        // TODO: remove in 4.10
                        Q_EMIT desktopChanged(old, newDesktop);
                    }
                });
        connect(ws->qobject.get(),
                &win::space_qobject::desktopPresenceChanged,
                this,
                [this, space = ws](auto win_id, int old) {
                    auto c = space->windows_map.at(win_id);
                    assert(c);
                    assert(c->render);
                    assert(c->render->effect);
                    Q_EMIT desktopPresenceChanged(c->render->effect.get(), old, c->desktop());
                });
        connect(ws->qobject.get(),
                &win::space_qobject::clientAdded,
                this,
                [this, space = ws](auto win_id) {
                    auto c = space->windows_map.at(win_id);
                    if (c->ready_for_painting) {
                        slotClientShown(c);
                    } else {
                        QObject::connect(c->qobject.get(),
                                         &win::window_qobject::windowShown,
                                         this,
                                         [this, c] { slotClientShown(c); });
                    }
                });
        connect(ws->qobject.get(),
                &win::space_qobject::unmanagedAdded,
                this,
                [this, space = ws](auto win_id) {
                    // it's never initially ready but has synthetic 50ms delay
                    auto u = space->windows_map.at(win_id);
                    connect(u->qobject.get(), &win::window_qobject::windowShown, this, [this, u] {
                        slotUnmanagedShown(u);
                    });
                });
        connect(ws->qobject.get(),
                &win::space_qobject::internalClientAdded,
                this,
                [this, space = ws](auto win_id) {
                    auto client = space->windows_map.at(win_id);
                    assert(client->render);
                    assert(client->render->effect);
                    setupAbstractClientConnections(client);
                    Q_EMIT windowAdded(client->render->effect.get());
                });
        connect(ws->qobject.get(), &win::space_qobject::clientActivated, this, [this, space = ws] {
            auto window = space->active_client;
            assert(!window || window->render);
            assert(!window || window->render->effect);
            auto eff_win = window ? window->render->effect.get() : nullptr;
            Q_EMIT windowActivated(eff_win);
        });

        QObject::connect(ws->qobject.get(),
                         &win::space_qobject::remnant_created,
                         this,
                         [this, space = ws](auto win_id) {
                             auto win = space->windows_map.at(win_id);
                             add_remnant(win);
                         });

        connect(ws->qobject.get(),
                &win::space_qobject::window_deleted,
                this,
                [this, space = ws](auto win_id) {
                    auto d = space->windows_map.at(win_id);
                    assert(d->render);
                    assert(d->render->effect);
                    Q_EMIT windowDeleted(d->render->effect.get());
                    elevated_windows.removeAll(d->render->effect.get());
                });
        connect(ws->session_manager.get(),
                &win::session_manager::stateChanged,
                this,
                &KWin::EffectsHandler::sessionStateChanged);
        connect(vds->qobject.get(),
                &win::virtual_desktop_manager_qobject::countChanged,
                this,
                &EffectsHandler::numberDesktopsChanged);
        QObject::connect(ws->input->platform.cursor.get(),
                         &input::cursor::mouse_changed,
                         this,
                         &EffectsHandler::mouseChanged);

        auto& base = compositor.platform.base;
        connect(&base, &base_t::output_added, this, &EffectsHandler::numberScreensChanged);
        connect(&base, &base_t::output_removed, this, &EffectsHandler::numberScreensChanged);

        QObject::connect(
            &base, &base_t::topology_changed, this, [this](auto old_topo, auto new_topo) {
                if (old_topo.size != new_topo.size) {
                    Q_EMIT virtualScreenSizeChanged();
                    Q_EMIT virtualScreenGeometryChanged();
                }
            });

        connect(ws->stacking_order.qobject.get(),
                &win::stacking_order_qobject::changed,
                this,
                &EffectsHandler::stackingOrderChanged);

#if KWIN_BUILD_TABBOX
        auto qt_tabbox = ws->tabbox->qobject.get();
        connect(qt_tabbox, &win::tabbox_qobject::tabbox_added, this, &EffectsHandler::tabBoxAdded);
        connect(
            qt_tabbox, &win::tabbox_qobject::tabbox_updated, this, &EffectsHandler::tabBoxUpdated);
        connect(
            qt_tabbox, &win::tabbox_qobject::tabbox_closed, this, &EffectsHandler::tabBoxClosed);
        connect(qt_tabbox,
                &win::tabbox_qobject::tabbox_key_event,
                this,
                &EffectsHandler::tabBoxKeyEvent);
#endif

        connect(ws->edges->qobject.get(),
                &win::screen_edger_qobject::approaching,
                this,
                &EffectsHandler::screenEdgeApproaching);
        connect(kwinApp()->screen_locker_watcher.get(),
                &desktop::screen_locker_watcher::locked,
                this,
                &EffectsHandler::screenLockingChanged);
        connect(kwinApp()->screen_locker_watcher.get(),
                &desktop::screen_locker_watcher::about_to_lock,
                this,
                &EffectsHandler::screenAboutToLock);

        auto make_property_filter = [this] {
            using filter = x11::property_notify_filter<effects_handler_wrap, space_t>;
            x11_property_notify = std::make_unique<filter>(
                *this, *this->compositor.space, kwinApp()->x11RootWindow());
        };

        connect(kwinApp(), &Application::x11ConnectionChanged, this, [this, make_property_filter] {
            registered_atoms.clear();
            for (auto it = m_propertiesForEffects.keyBegin(); it != m_propertiesForEffects.keyEnd();
                 it++) {
                x11::add_support_property(*this, *it);
            }
            if (kwinApp()->x11Connection()) {
                make_property_filter();
            } else {
                x11_property_notify.reset();
            }
            Q_EMIT xcbConnectionChanged();
        });

        if (kwinApp()->x11Connection()) {
            make_property_filter();
        }

        // connect all clients
        for (auto& window : ws->windows) {
            // TODO: Can we merge this with the one for Wayland XdgShellClients below?
            if (!window->control) {
                continue;
            }
            auto x11_client = dynamic_cast<typename space_t::x11_window*>(window);
            if (!x11_client) {
                continue;
            }
            setupClientConnections(x11_client);
        }
        for (auto unmanaged : win::x11::get_unmanageds(*ws)) {
            setupUnmanagedConnections(unmanaged);
        }
        for (auto window : ws->windows) {
            if (auto internal = dynamic_cast<typename space_t::internal_window_t*>(window)) {
                setupAbstractClientConnections(internal);
            }
        }

        connect(&compositor.platform.base,
                &base_t::output_added,
                this,
                &effects_handler_impl::slotOutputEnabled);
        connect(&compositor.platform.base,
                &base_t::output_removed,
                this,
                &effects_handler_impl::slotOutputDisabled);

        auto const outputs = compositor.platform.base.outputs;
        for (base::output* output : outputs) {
            slotOutputEnabled(output);
        }
    }

    ~effects_handler_impl() override
    {
        singleton_interface::register_thumbnail = {};
    }

    scene_t* scene() const
    {
        return compositor.scene.get();
    }

    unsigned long xrenderBufferPicture() const override
    {
        return compositor.scene->xrenderBufferPicture();
    }

    QPainter* scenePainter() override
    {
        return compositor.scene->scenePainter();
    }

    bool animationsSupported() const override
    {
        static const QByteArray forceEnvVar = qgetenv("KWIN_EFFECTS_FORCE_ANIMATIONS");
        if (!forceEnvVar.isEmpty()) {
            static const int forceValue = forceEnvVar.toInt();
            return forceValue == 1;
        }
        return compositor.scene->animationsSupported();
    }

    bool makeOpenGLContextCurrent() override
    {
        return compositor.scene->makeOpenGLContextCurrent();
    }

    void doneOpenGLContextCurrent() override
    {
        compositor.scene->doneOpenGLContextCurrent();
    }

    void addRepaintFull() override
    {
        compositor.addRepaintFull();
    }

    void addRepaint(const QRect& r) override
    {
        compositor.addRepaint(r);
    }

    void addRepaint(const QRegion& r) override
    {
        compositor.addRepaint(r);
    }

    void addRepaint(int x, int y, int w, int h) override
    {
        compositor.addRepaint(QRegion(x, y, w, h));
    }

    void final_paint_screen(paint_type mask, QRegion const& region, ScreenPaintData& data) override
    {
        compositor.scene->finalPaintScreen(mask, region, data);
    }

    void final_paint_window(EffectWindow* window,
                            paint_type mask,
                            QRegion const& region,
                            WindowPaintData& data) override
    {
        compositor.scene->finalPaintWindow(
            static_cast<effect_window_t*>(window), mask, region, data);
    }

    void final_draw_window(EffectWindow* window,
                           paint_type mask,
                           QRegion const& region,
                           WindowPaintData& data) override
    {
        compositor.scene->finalDrawWindow(
            static_cast<effect_window_t*>(window), mask, region, data);
    }

    void activateWindow(EffectWindow* c) override
    {
        auto window = static_cast<effect_window_t*>(c)->window.ref_win;
        if (window && window->control) {
            win::force_activate_window(*compositor.space, window);
        }
    }

    EffectWindow* activeWindow() const override
    {
        auto ac = compositor.space->active_client;
        return ac ? ac->render->effect.get() : nullptr;
    }

    void desktopResized(const QSize& size)
    {
        compositor.scene->handle_screen_geometry_change(size);
        Q_EMIT screenGeometryChanged(size);
    }

    void registerGlobalShortcut(const QKeySequence& shortcut, QAction* action) override
    {
        compositor.space->input->platform.registerShortcut(shortcut, action);
    }

    void registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButton pointerButtons,
                                 QAction* action) override
    {
        compositor.space->input->platform.registerPointerShortcut(
            modifiers, pointerButtons, action);
    }

    void registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis,
                              QAction* action) override
    {
        compositor.space->input->platform.registerAxisShortcut(modifiers, axis, action);
    }

    void registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action) override
    {
        compositor.space->input->platform.registerTouchpadSwipeShortcut(direction, action);
    }

    void startMousePolling() override
    {
        if (auto& cursor = compositor.space->input->platform.cursor) {
            cursor->start_mouse_polling();
        }
    }

    void stopMousePolling() override
    {
        if (auto& cursor = compositor.space->input->platform.cursor) {
            cursor->stop_mouse_polling();
        }
    }

    QPoint cursorPos() const override
    {
        return compositor.space->input->platform.cursor->pos();
    }

    void defineCursor(Qt::CursorShape shape) override
    {
        compositor.space->input->pointer->setEffectsOverrideCursor(shape);
    }

    void connectNotify(const QMetaMethod& signal) override
    {
        if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
            if (!m_trackingCursorChanges) {
                QObject::connect(compositor.space->input->platform.cursor.get(),
                                 &input::cursor::image_changed,
                                 this,
                                 &EffectsHandler::cursorShapeChanged);
                compositor.space->input->platform.cursor->start_image_tracking();
            }
            ++m_trackingCursorChanges;
        }
        EffectsHandler::connectNotify(signal);
    }

    void disconnectNotify(const QMetaMethod& signal) override
    {
        if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
            Q_ASSERT(m_trackingCursorChanges > 0);
            if (!--m_trackingCursorChanges) {
                compositor.space->input->platform.cursor->stop_image_tracking();
                QObject::disconnect(compositor.space->input->platform.cursor.get(),
                                    &input::cursor::image_changed,
                                    this,
                                    &EffectsHandler::cursorShapeChanged);
            }
        }
        EffectsHandler::disconnectNotify(signal);
    }

    PlatformCursorImage cursorImage() const override
    {
        return compositor.space->input->platform.cursor->platform_image();
    }

    bool isCursorHidden() const override
    {
        return compositor.space->input->platform.cursor->is_hidden();
    }

    void hideCursor() override
    {
        compositor.space->input->platform.cursor->hide();
    }

    void showCursor() override
    {
        compositor.space->input->platform.cursor->show();
    }

    void startInteractiveWindowSelection(std::function<void(KWin::EffectWindow*)> callback) override
    {
        compositor.space->input->platform.start_interactive_window_selection([callback](auto t) {
            if (t) {
                assert(t->render);
                assert(t->render->effect);
                callback(t->render->effect.get());
            } else {
                callback(nullptr);
            }
        });
    }

    void startInteractivePositionSelection(std::function<void(const QPoint&)> callback) override
    {
        compositor.space->input->platform.start_interactive_position_selection(callback);
    }

    void showOnScreenMessage(const QString& message, const QString& iconName = QString()) override
    {
        win::osd_show(*compositor.space, message, iconName);
    }

    void hideOnScreenMessage(OnScreenMessageHideFlags flags = OnScreenMessageHideFlags()) override
    {
        win::osd_hide_flags internal_flags{};
        if (flags.testFlag(OnScreenMessageHideFlag::SkipsCloseAnimation)) {
            internal_flags |= win::osd_hide_flags::skip_close_animation;
        }
        win::osd_hide(*compositor.space, internal_flags);
    }

    QRect renderTargetRect() const override
    {
        return compositor.scene->m_renderTargetRect;
    }

    qreal renderTargetScale() const override
    {
        return compositor.scene->m_renderTargetScale;
    }

    void renderEffectQuickView(EffectQuickView* effectQuickView) const override
    {
        if (!effectQuickView->isVisible()) {
            return;
        }
        compositor.scene->paintEffectQuickView(effectQuickView);
    }

    void moveWindow(EffectWindow* w,
                    const QPoint& pos,
                    bool snap = false,
                    double snapAdjust = 1.0) override
    {
        auto window = static_cast<effect_window_t*>(w)->window.ref_win;
        if (!window || !window->isMovable()) {
            return;
        }

        if (snap) {
            win::move(
                window,
                win::adjust_window_position(*compositor.space, *window, pos, true, snapAdjust));
        } else {
            win::move(window, pos);
        }
    }

    void windowToDesktop(EffectWindow* w, int desktop) override
    {
        auto window = static_cast<effect_window_t*>(w)->window.ref_win;
        if (window && window->control && !win::is_desktop(window) && !win::is_dock(window)) {
            win::send_window_to_desktop(*compositor.space, window, desktop, true);
        }
    }

    void windowToDesktops(EffectWindow* w, const QVector<uint>& desktopIds) override
    {
        auto window = static_cast<effect_window_t*>(w)->window.ref_win;
        if (!window || !window->control || win::is_desktop(window) || win::is_dock(window)) {
            return;
        }
        QVector<win::virtual_desktop*> desktops;
        desktops.reserve(desktopIds.count());
        for (uint x11Id : desktopIds) {
            if (x11Id > compositor.space->virtual_desktop_manager->count()) {
                continue;
            }
            auto d = compositor.space->virtual_desktop_manager->desktopForX11Id(x11Id);
            Q_ASSERT(d);
            if (desktops.contains(d)) {
                continue;
            }
            desktops << d;
        }
        win::set_desktops(window, desktops);
    }

    void windowToScreen(EffectWindow* w, int screen) override
    {
        auto output = base::get_output(compositor.platform.base.outputs, screen);
        auto window = static_cast<effect_window_t*>(w)->window.ref_win;

        if (output && window && window->control && !win::is_desktop(window)
            && !win::is_dock(window)) {
            win::send_to_screen(*compositor.space, window, *output);
        }
    }

    void setShowingDesktop(bool showing) override
    {
        win::set_showing_desktop(*compositor.space, showing);
    }

    int currentDesktop() const override
    {
        return compositor.space->virtual_desktop_manager->current();
    }

    int numberOfDesktops() const override
    {
        return compositor.space->virtual_desktop_manager->count();
    }

    void setCurrentDesktop(int desktop) override
    {
        compositor.space->virtual_desktop_manager->setCurrent(desktop);
    }

    void setNumberOfDesktops(int desktops) override
    {
        compositor.space->virtual_desktop_manager->setCount(desktops);
    }

    QSize desktopGridSize() const override
    {
        return compositor.space->virtual_desktop_manager->grid().size();
    }

    int desktopAtCoords(QPoint coords) const override
    {
        if (auto vd = compositor.space->virtual_desktop_manager->grid().at(coords)) {
            return vd->x11DesktopNumber();
        }
        return 0;
    }

    QPoint desktopGridCoords(int id) const override
    {
        return compositor.space->virtual_desktop_manager->grid().gridCoords(id);
    }

    QPoint desktopCoords(int id) const override
    {
        auto coords = compositor.space->virtual_desktop_manager->grid().gridCoords(id);
        if (coords.x() == -1) {
            return QPoint(-1, -1);
        }
        auto const& space_size = compositor.platform.base.topology.size;
        return QPoint(coords.x() * space_size.width(), coords.y() * space_size.height());
    }

    int desktopAbove(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_above>(
            *compositor.space->virtual_desktop_manager, desktop, wrap);
    }

    int desktopToRight(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_right>(
            *compositor.space->virtual_desktop_manager, desktop, wrap);
    }

    int desktopBelow(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_below>(
            *compositor.space->virtual_desktop_manager, desktop, wrap);
    }

    int desktopToLeft(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_left>(
            *compositor.space->virtual_desktop_manager, desktop, wrap);
    }

    QString desktopName(int desktop) const override
    {
        return compositor.space->virtual_desktop_manager->name(desktop);
    }

    EffectWindow* find_window_by_wid(WId id) const override
    {
        if (auto w = win::x11::find_controlled_window<typename space_t::x11_window>(
                *compositor.space, win::x11::predicate_match::window, id)) {
            return w->render->effect.get();
        }
        if (auto unmanaged
            = win::x11::find_unmanaged<typename space_t::x11_window>(*compositor.space, id)) {
            return unmanaged->render->effect.get();
        }
        return nullptr;
    }

    EffectWindow* find_window_by_surface(Wrapland::Server::Surface* /*surface*/) const override
    {
        return nullptr;
    }

    EffectWindow* find_window_by_qwindow(QWindow* w) const override
    {
        if (auto toplevel = compositor.space->findInternal(w)) {
            return toplevel->render->effect.get();
        }
        return nullptr;
    }

    EffectWindow* find_window_by_uuid(const QUuid& id) const override
    {
        for (auto win : compositor.space->windows) {
            if (!win->remnant && win->internal_id == id) {
                return win->render->effect.get();
            }
        }
        return nullptr;
    }

    EffectWindowList stackingOrder() const override
    {
        auto list = win::render_stack(compositor.space->stacking_order);
        EffectWindowList ret;
        for (auto t : list) {
            if (auto eff_win = t->render->effect.get()) {
                ret.append(eff_win);
            }
        }
        return ret;
    }

    void setTabBoxWindow([[maybe_unused]] EffectWindow* w) override
    {
#if KWIN_BUILD_TABBOX
        auto window = static_cast<effect_window_t*>(w)->window.ref_win;
        if (window->control) {
            compositor.space->tabbox->set_current_client(window);
        }
#endif
    }

    void setTabBoxDesktop([[maybe_unused]] int desktop) override
    {
#if KWIN_BUILD_TABBOX
        compositor.space->tabbox->set_current_desktop(desktop);
#endif
    }

    EffectWindowList currentTabBoxWindowList() const override
    {
#if KWIN_BUILD_TABBOX
        const auto clients = compositor.space->tabbox->current_client_list();
        EffectWindowList ret;
        ret.reserve(clients.size());
        std::transform(std::cbegin(clients),
                       std::cend(clients),
                       std::back_inserter(ret),
                       [](auto client) { return client->render->effect.get(); });
        return ret;
#else
        return EffectWindowList();
#endif
    }

    void refTabBox() override
    {
#if KWIN_BUILD_TABBOX
        compositor.space->tabbox->reference();
#endif
    }

    void unrefTabBox() override
    {
#if KWIN_BUILD_TABBOX
        compositor.space->tabbox->unreference();
#endif
    }

    void closeTabBox() override
    {
#if KWIN_BUILD_TABBOX
        compositor.space->tabbox->close();
#endif
    }

    QList<int> currentTabBoxDesktopList() const override
    {
#if KWIN_BUILD_TABBOX
        return compositor.space->tabbox->current_desktop_list();
#else
        return QList<int>();
#endif
    }

    int currentTabBoxDesktop() const override
    {
#if KWIN_BUILD_TABBOX
        return compositor.space->tabbox->current_desktop();
#else
        return -1;
#endif
    }

    EffectWindow* currentTabBoxWindow() const override
    {
#if KWIN_BUILD_TABBOX
        if (auto c = compositor.space->tabbox->current_client())
            return c->render->effect.get();
#endif
        return nullptr;
    }

    int activeScreen() const override
    {
        auto output = win::get_current_output(*compositor.space);
        if (!output) {
            return 0;
        }
        return base::get_output_index(compositor.platform.base.outputs, *output);
    }

    int numScreens() const override
    {
        return compositor.platform.base.outputs.size();
    }

    int screenNumber(const QPoint& pos) const override
    {
        auto const& outputs = compositor.platform.base.outputs;
        auto output = base::get_nearest_output(outputs, pos);
        if (!output) {
            return 0;
        }
        return base::get_output_index(outputs, *output);
    }

    QList<EffectScreen*> screens() const override
    {
        return m_effectScreens;
    }

    EffectScreen* screenAt(const QPoint& point) const override
    {
        return m_effectScreens.value(screenNumber(point));
    }

    EffectScreen* findScreen(const QString& name) const override
    {
        for (EffectScreen* screen : qAsConst(m_effectScreens)) {
            if (screen->name() == name) {
                return screen;
            }
        }
        return nullptr;
    }

    EffectScreen* findScreen(int screenId) const override
    {
        return m_effectScreens.value(screenId);
    }

    QRect clientArea(clientAreaOption opt, int screen, int desktop) const override
    {
        auto output = base::get_output(compositor.platform.base.outputs, screen);
        return win::space_window_area(*compositor.space, opt, output, desktop);
    }

    QRect clientArea(clientAreaOption opt, const EffectWindow* c) const override
    {
        auto window = static_cast<effect_window_t const*>(c)->window.ref_win;
        auto space = compositor.space;

        if (window->control) {
            return win::space_window_area(*space, opt, window);
        } else {
            return win::space_window_area(*space,
                                          opt,
                                          window->frameGeometry().center(),
                                          space->virtual_desktop_manager->current());
        }
    }

    QRect clientArea(clientAreaOption opt, const QPoint& p, int desktop) const override
    {
        return win::space_window_area(*compositor.space, opt, p, desktop);
    }

    QSize virtualScreenSize() const override
    {
        return compositor.platform.base.topology.size;
    }

    QRect virtualScreenGeometry() const override
    {
        return QRect({}, compositor.platform.base.topology.size);
    }

    void reserveElectricBorder(ElectricBorder border, Effect* effect) override
    {
        auto id = compositor.space->edges->reserve(
            border, [effect](auto eb) { return effect->borderActivated(eb); });

        auto it = reserved_borders.find(effect);
        if (it == reserved_borders.end()) {
            it = reserved_borders.insert({effect, {}}).first;
        }

        auto insert_border = [](auto& map, ElectricBorder border, uint32_t id) {
            auto it = map.find(border);
            if (it == map.end()) {
                map.insert({border, id});
                return;
            }

            it->second = id;
        };

        insert_border(it->second, border, id);
    }

    void unreserveElectricBorder(ElectricBorder border, Effect* effect) override
    {
        auto it = reserved_borders.find(effect);
        if (it == reserved_borders.end()) {
            return;
        }

        auto it2 = it->second.find(border);
        if (it2 == it->second.end()) {
            return;
        }

        compositor.space->edges->unreserve(border, it2->second);
    }

    void registerTouchBorder(ElectricBorder border, QAction* action) override
    {
        compositor.space->edges->reserveTouch(border, action);
    }

    void unregisterTouchBorder(ElectricBorder border, QAction* action) override
    {
        compositor.space->edges->unreserveTouch(border, action);
    }

    void unreserve_borders(Effect& effect)
    {
        auto it = reserved_borders.find(&effect);
        if (it == reserved_borders.end()) {
            return;
        }

        // Might be at shutdown with edges object already gone.
        if (compositor.space->edges) {
            for (auto& [key, id] : it->second) {
                compositor.space->edges->unreserve(key, id);
            }
        }

        reserved_borders.erase(it);
    }

    QVariant kwinOption(KWinOption kwopt) override
    {
        switch (kwopt) {
        case CloseButtonCorner: {
            // TODO: this could become per window and be derived from the actual position in the
            // deco
            auto deco_settings = compositor.space->deco->settings();
            auto close_enum = KDecoration2::DecorationButtonType::Close;
            return deco_settings && deco_settings->decorationButtonsLeft().contains(close_enum)
                ? Qt::TopLeftCorner
                : Qt::TopRightCorner;
        }
        case SwitchDesktopOnScreenEdge:
            return compositor.space->edges->desktop_switching.always;
        case SwitchDesktopOnScreenEdgeMovingWindows:
            return compositor.space->edges->desktop_switching.when_moving_client;
        default:
            return QVariant(); // an invalid one
        }
    }

    SessionState sessionState() const override
    {
        return compositor.space->session_manager->state();
    }

    QByteArray readRootProperty(long atom, long type, int format) const override
    {
        if (!kwinApp()->x11Connection()) {
            return QByteArray();
        }
        return render::x11::read_window_property(kwinApp()->x11RootWindow(), atom, type, format);
    }

    xcb_atom_t announceSupportProperty(const QByteArray& propertyName, Effect* effect) override
    {
        return x11::announce_support_property(*this, effect, propertyName);
    }

    void removeSupportProperty(const QByteArray& propertyName, Effect* effect) override
    {
        x11::remove_support_property(*this, effect, propertyName);
    }

    Compositor& compositor;

protected:
    void add_remnant(typename space_t::window_t* remnant)
    {
        assert(remnant);
        assert(remnant->render);
        Q_EMIT windowClosed(remnant->render->effect.get());
    }

    void setupAbstractClientConnections(typename space_t::window_t* window)
    {
        auto qtwin = window->qobject.get();

        QObject::connect(qtwin,
                         &win::window_qobject::maximize_mode_changed,
                         this,
                         [this, window](auto mode) { slotClientMaximized(window, mode); });
        QObject::connect(
            qtwin, &win::window_qobject::clientStartUserMovedResized, this, [this, window] {
                Q_EMIT windowStartUserMovedResized(window->render->effect.get());
            });
        QObject::connect(qtwin,
                         &win::window_qobject::clientStepUserMovedResized,
                         this,
                         [this, window](QRect const& geometry) {
                             Q_EMIT windowStepUserMovedResized(window->render->effect.get(),
                                                               geometry);
                         });
        QObject::connect(
            qtwin, &win::window_qobject::clientFinishUserMovedResized, this, [this, window] {
                Q_EMIT windowFinishUserMovedResized(window->render->effect.get());
            });
        QObject::connect(qtwin,
                         &win::window_qobject::opacityChanged,
                         this,
                         [this, window](auto old) { slotOpacityChanged(window, old); });
        QObject::connect(
            qtwin, &win::window_qobject::clientMinimized, this, [this, window](auto animate) {
                // TODO: notify effects even if it should not animate?
                if (animate) {
                    Q_EMIT windowMinimized(window->render->effect.get());
                }
            });
        QObject::connect(
            qtwin, &win::window_qobject::clientUnminimized, this, [this, window](auto animate) {
                // TODO: notify effects even if it should not animate?
                if (animate) {
                    Q_EMIT windowUnminimized(window->render->effect.get());
                }
            });
        QObject::connect(qtwin, &win::window_qobject::modalChanged, this, [this, window] {
            slotClientModalityChanged(window);
        });
        QObject::connect(
            qtwin,
            &win::window_qobject::frame_geometry_changed,
            this,
            [this, window](auto const& rect) { slotGeometryShapeChanged(window, rect); });
        QObject::connect(
            qtwin,
            &win::window_qobject::frame_geometry_changed,
            this,
            [this, window](auto const& rect) { slotFrameGeometryChanged(window, rect); });
        QObject::connect(qtwin,
                         &win::window_qobject::damaged,
                         this,
                         [this, window](auto const& rect) { slotWindowDamaged(window, rect); });
        QObject::connect(qtwin,
                         &win::window_qobject::unresponsiveChanged,
                         this,
                         [this, window](bool unresponsive) {
                             Q_EMIT windowUnresponsiveChanged(window->render->effect.get(),
                                                              unresponsive);
                         });
        QObject::connect(qtwin, &win::window_qobject::windowShown, this, [this, window] {
            Q_EMIT windowShown(window->render->effect.get());
        });
        QObject::connect(qtwin, &win::window_qobject::windowHidden, this, [this, window] {
            Q_EMIT windowHidden(window->render->effect.get());
        });
        QObject::connect(
            qtwin, &win::window_qobject::keepAboveChanged, this, [this, window](bool above) {
                Q_UNUSED(above)
                Q_EMIT windowKeepAboveChanged(window->render->effect.get());
            });
        QObject::connect(
            qtwin, &win::window_qobject::keepBelowChanged, this, [this, window](bool below) {
                Q_UNUSED(below)
                Q_EMIT windowKeepBelowChanged(window->render->effect.get());
            });
        QObject::connect(qtwin, &win::window_qobject::fullScreenChanged, this, [this, window]() {
            Q_EMIT windowFullScreenChanged(window->render->effect.get());
        });
        QObject::connect(
            qtwin, &win::window_qobject::visible_geometry_changed, this, [this, window]() {
                Q_EMIT windowExpandedGeometryChanged(window->render->effect.get());
            });
    }

    // For X11 windows
    void setupClientConnections(typename space_t::window_t* c)
    {
        setupAbstractClientConnections(c);
        connect(c->qobject.get(),
                &win::window_qobject::paddingChanged,
                this,
                [this, c](auto const& old) { slotPaddingChanged(c, old); });
    }

    void setupUnmanagedConnections(typename space_t::window_t* u)
    {
        connect(u->qobject.get(), &win::window_qobject::opacityChanged, this, [this, u](auto old) {
            slotOpacityChanged(u, old);
        });
        connect(u->qobject.get(),
                &win::window_qobject::frame_geometry_changed,
                this,
                [this, u](auto const& old) { slotGeometryShapeChanged(u, old); });
        connect(u->qobject.get(),
                &win::window_qobject::frame_geometry_changed,
                this,
                [this, u](auto const& old) { slotFrameGeometryChanged(u, old); });
        connect(u->qobject.get(),
                &win::window_qobject::paddingChanged,
                this,
                [this, u](auto const& old) { slotPaddingChanged(u, old); });
        connect(u->qobject.get(),
                &win::window_qobject::damaged,
                this,
                [this, u](auto const& region) { slotWindowDamaged(u, region); });
        connect(u->qobject.get(),
                &win::window_qobject::visible_geometry_changed,
                this,
                [this, u]() { Q_EMIT windowExpandedGeometryChanged(u->render->effect.get()); });
    }

    void slotClientShown(typename space_t::window_t* t)
    {
        assert(dynamic_cast<typename space_t::x11_window*>(t));
        disconnect(t->qobject.get(), &win::window_qobject::windowShown, this, nullptr);
        setupClientConnections(t);
        Q_EMIT windowAdded(t->render->effect.get());
    }

    void slotXdgShellClientShown(typename space_t::window_t* t)
    {
        setupAbstractClientConnections(t);
        Q_EMIT windowAdded(t->render->effect.get());
    }

    void slotUnmanagedShown(typename space_t::window_t* t)
    { // regardless, unmanaged windows are -yet?- not synced anyway
        assert(!t->control);
        setupUnmanagedConnections(t);
        Q_EMIT windowAdded(t->render->effect.get());
    }

    void slotClientMaximized(typename space_t::window_t* window, win::maximize_mode maxMode)
    {
        bool horizontal = false;
        bool vertical = false;
        switch (maxMode) {
        case win::maximize_mode::horizontal:
            horizontal = true;
            break;
        case win::maximize_mode::vertical:
            vertical = true;
            break;
        case win::maximize_mode::full:
            horizontal = true;
            vertical = true;
            break;
        case win::maximize_mode::restore: // fall through
        default:
            // default - nothing to do
            break;
        }

        auto ew = window->render->effect.get();
        assert(ew);
        Q_EMIT windowMaximizedStateChanged(ew, horizontal, vertical);
    }

    void slotOpacityChanged(typename space_t::window_t* t, qreal oldOpacity)
    {
        assert(t->render->effect);

        if (t->opacity() == oldOpacity) {
            return;
        }

        Q_EMIT windowOpacityChanged(
            t->render->effect.get(), oldOpacity, static_cast<qreal>(t->opacity()));
    }

    void slotClientModalityChanged(typename space_t::window_t* window)
    {
        Q_EMIT windowModalityChanged(window->render->effect.get());
    }

    void slotGeometryShapeChanged(typename space_t::window_t* t, const QRect& old)
    {
        assert(t);
        assert(t->render);
        assert(t->render->effect);

        if (t->control && (win::is_move(t) || win::is_resize(t))) {
            // For that we have windowStepUserMovedResized.
            return;
        }

        Q_EMIT windowGeometryShapeChanged(t->render->effect.get(), old);
    }

    void slotFrameGeometryChanged(typename space_t::window_t* toplevel, const QRect& oldGeometry)
    {
        assert(toplevel->render);
        assert(toplevel->render->effect);
        Q_EMIT windowFrameGeometryChanged(toplevel->render->effect.get(), oldGeometry);
    }

    void slotPaddingChanged(typename space_t::window_t* t, const QRect& old)
    {
        assert(t);
        assert(t->render);
        assert(t->render->effect);
        Q_EMIT windowPaddingChanged(t->render->effect.get(), old);
    }

    void slotWindowDamaged(typename space_t::window_t* t, const QRegion& r)
    {
        assert(t->render);
        assert(t->render->effect);
        Q_EMIT windowDamaged(t->render->effect.get(), r);
    }

    void slotOutputEnabled(base::output* output)
    {
        auto screen = new effect_screen_impl<base::output>(output, this);
        m_effectScreens.append(screen);
        Q_EMIT screenAdded(screen);
    }

    void slotOutputDisabled(base::output* output)
    {
        auto it = std::find_if(
            m_effectScreens.begin(), m_effectScreens.end(), [&output](EffectScreen* screen) {
                return static_cast<effect_screen_impl<base::output>*>(screen)->platformOutput()
                    == output;
            });
        if (it != m_effectScreens.end()) {
            EffectScreen* screen = *it;
            m_effectScreens.erase(it);
            Q_EMIT screenRemoved(screen);
            delete screen;
        }
    }

    QList<EffectScreen*> m_effectScreens;
    int m_trackingCursorChanges{0};
    std::unique_ptr<x11::property_notify_filter<effects_handler_wrap, space_t>> x11_property_notify;
    std::unordered_map<Effect*, std::unordered_map<ElectricBorder, uint32_t>> reserved_borders;
};

}
