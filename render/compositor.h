/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwinglobals.h>

#include <QBasicTimer>
#include <QObject>
#include <QRegion>
#include <QTimer>

#include <deque>
#include <map>
#include <memory>

namespace KWin
{
class Toplevel;

namespace render
{

class platform;

namespace x11
{
class compositor_selection_owner;
}

class cursor;
class scene;

class KWIN_EXPORT compositor : public QObject
{
    Q_OBJECT
public:
    enum class State {
        On = 0,
        Off,
        Starting,
        Stopping,
    };

    // TODO(romangg): Only relevant for Wayland. Put in child class.
    std::unique_ptr<cursor> software_cursor;
    render::platform& platform;

    ~compositor() override;
    static compositor* self();

    // when adding repaints caused by a window, you probably want to use
    // either Toplevel::addRepaint() or Toplevel::addWorkspaceRepaint()
    void addRepaint(QRect const& rect);
    void addRepaint(int x, int y, int w, int h);
    virtual void addRepaint(QRegion const& region);
    void addRepaintFull();

    /**
     * Schedules a new repaint if no repaint is currently scheduled. Tries to optimize by only
     * repainting outputs that the visible bounds of @arg window intersect with.
     */
    virtual void schedule_repaint(Toplevel* window);
    virtual void schedule_frame_callback(Toplevel* window);

    /**
     * Notifies the compositor that SwapBuffers() is about to be called.
     * Rendering of the next frame will be deferred until bufferSwapComplete()
     * is called.
     */
    void aboutToSwapBuffers();

    /**
     * Notifies the compositor that a pending buffer swap has completed.
     */
    virtual void bufferSwapComplete(bool present = true);

    /**
     * Toggles compositing, that is if the Compositor is suspended it will be resumed
     * and if the Compositor is active it will be suspended.
     * Invoked by keybinding (shortcut default: Shift + Alt + F12).
     */
    virtual void toggleCompositing() = 0;

    /**
     * Re-initializes the Compositor completely.
     * Connected to the D-Bus signal org.kde.KWin /KWin reinitCompositing
     */
    virtual void reinitialize();

    /**
     * Whether the Compositor is active. That is a Scene is present and the Compositor is
     * not shutting down itself.
     */
    bool isActive();

    render::scene* scene() const;

    static bool compositing();

    // for delayed supportproperty management of effects
    void keepSupportProperty(xcb_atom_t atom);
    void removeSupportProperty(xcb_atom_t atom);

Q_SIGNALS:
    void compositingToggled(bool active);
    void aboutToDestroy();
    void aboutToToggleCompositing();
    void sceneCreated();

protected:
    compositor(render::platform& platform);
    void timerEvent(QTimerEvent* te) override;

    virtual void start() = 0;
    void stop();

    /**
     * @brief Prepares start.
     * @return bool @c true if start should be continued and @c if not.
     */
    bool setupStart();
    /**
     * Continues the startup after Scene And Workspace are created
     */
    void startupWithWorkspace();
    virtual render::scene* create_scene(QVector<CompositingType> const& support) = 0;

    virtual std::deque<Toplevel*> performCompositing() = 0;
    void update_paint_periods(int64_t duration);
    void retard_next_composition();
    void setCompositeTimer();

    virtual void configChanged();

    void destroyCompositorSelection();

    State m_state;
    x11::compositor_selection_owner* m_selectionOwner;
    QRegion repaints_region;
    QBasicTimer compositeTimer;
    qint64 m_delay;
    bool m_bufferSwapPending;

private:
    void claimCompositorSelection();
    int refreshRate() const;

    void setupX11Support();

    void deleteUnusedSupportProperties();

    /**
     * The current refresh cycle length. In the future this should be per output on Wayland.
     *
     * @return refresh cycle length in nanoseconds.
     */
    qint64 refreshLength() const;

    QList<xcb_atom_t> m_unusedSupportProperties;
    QTimer m_unusedSupportPropertyTimer;

    // Compositing delay (in ns).
    qint64 m_lastPaintDurations[2]{0};
    int m_paintPeriods{0};

    std::unique_ptr<render::scene> m_scene;
};

}
}
