/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "toplevel.h"

#ifdef KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif
#include "abstract_output.h"
#include "atoms.h"
#include "composite.h"
#include "effects.h"
#include "platform.h"
#include "screens.h"
#include "shadow.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xcbutils.h"

#include "win/input.h"
#include "win/remnant.h"
#include "win/scene.h"
#include "win/space.h"
#include "win/transient.h"

#include "win/x11/client_machine.h"
#include "win/x11/netinfo.h"
#include "win/x11/xcb.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/wl_output.h>
#include <Wrapland/Server/surface.h>

#include <QDebug>

namespace KWin
{

Toplevel::Toplevel()
    : Toplevel(new win::transient(this))
{
}

Toplevel::Toplevel(win::transient* transient)
    : m_isDamaged(false)
    , m_internalId(QUuid::createUuid())
    , m_client()
    , damage_handle(XCB_NONE)
    , is_shape(false)
    , effect_window(nullptr)
    , m_clientMachine(new ClientMachine(this))
    , m_wmClientLeader(XCB_WINDOW_NONE)
    , m_damageReplyPending(false)
    , m_screen(0)
    , m_skipCloseAnimation(false)
{
    m_transient.reset(transient);

    connect(this, &Toplevel::frame_geometry_changed, this, [this](auto win, auto const& old_geo) {
        if (win::render_geometry(win).size() == win::frame_to_render_rect(win, old_geo).size()) {
            // Size unchanged. No need to update.
            return;
        }
        discard_shape();
    });

    connect(this, &Toplevel::damaged, this, &Toplevel::needsRepaint);
    connect(screens(), &Screens::changed, this, &Toplevel::checkScreen);
    connect(screens(), &Screens::countChanged, this, &Toplevel::checkScreen);

    setupCheckScreenConnection();
}

Toplevel::~Toplevel()
{
    Q_ASSERT(damage_handle == XCB_NONE);
    delete info;
    delete m_remnant;
}

QDebug& operator<<(QDebug& stream, const Toplevel* cl)
{
    if (cl == nullptr)
        return stream << "\'NULL\'";
    cl->debug(stream);
    return stream;
}

NET::WindowType Toplevel::windowType([[maybe_unused]] bool direct,int supported_types) const
{
    if (m_remnant) {
        return m_remnant->window_type;
    }
    if (supported_types == 0) {
        supported_types = supported_default_types;
    }

    auto wt = info->windowType(NET::WindowTypes(supported_types));
    if (direct || !control) {
        return wt;
    }

    auto wt2 = control->rules().checkType(wt);
    if (wt != wt2) {
        wt = wt2;
        // force hint change
        info->setWindowType(wt);
    }

    // hacks here
    if (wt == NET::Unknown) {
        // this is more or less suggested in NETWM spec
        wt = isTransient() ? NET::Dialog : NET::Normal;
    }
    return wt;
}

void Toplevel::detectShape(xcb_window_t id)
{
    const bool wasShape = is_shape;
    is_shape = Xcb::Extensions::self()->hasShape(id);
    if (wasShape != is_shape) {
        emit shapedChanged();
    }
}

Toplevel* Toplevel::create_remnant(Toplevel* source)
{
    auto win = new Toplevel();
    win->copyToDeleted(source);
    win->m_remnant = new win::remnant(win, source);
    workspace()->addDeleted(win, source);
    return win;
}

// used only by Deleted::copy()
void Toplevel::copyToDeleted(Toplevel* c)
{
    m_internalId = c->internalId();
    m_frameGeometry = c->m_frameGeometry;
    m_visual = c->m_visual;
    bit_depth = c->bit_depth;

    info = c->info;
    if (auto win_info = dynamic_cast<WinInfo*>(info)) {
        win_info->disable();
    }

    m_client.reset(c->m_client, false);
    ready_for_painting = c->ready_for_painting;
    damage_handle = XCB_NONE;
    damage_region = c->damage_region;
    repaints_region = c->repaints_region;
    layer_repaints_region = c->layer_repaints_region;
    is_shape = c->is_shape;
    effect_window = c->effect_window;
    if (effect_window != nullptr)
        effect_window->setWindow(this);
    resource_name = c->resourceName();
    resource_class = c->resourceClass();
    m_clientMachine = c->m_clientMachine;
    m_clientMachine->setParent(this);
    m_wmClientLeader = c->wmClientLeader();
    opaque_region = c->opaqueRegion();
    m_screen = c->m_screen;
    m_skipCloseAnimation = c->m_skipCloseAnimation;
    m_internalFBO = c->m_internalFBO;
    m_internalImage = c->m_internalImage;
    m_desktops = c->desktops();
    m_layer = c->layer();
    has_in_content_deco = c->has_in_content_deco;
    client_frame_extents = c->client_frame_extents;
}

// before being deleted, remove references to everything that's now
// owner by Deleted
void Toplevel::disownDataPassedToDeleted()
{
    info = nullptr;
}

Xcb::Property Toplevel::fetchWmClientLeader() const
{
    return Xcb::Property(false, xcb_window(), atoms->wm_client_leader, XCB_ATOM_WINDOW, 0, 10000);
}

void Toplevel::readWmClientLeader(Xcb::Property &prop)
{
    m_wmClientLeader = prop.value<xcb_window_t>(xcb_window());
}

void Toplevel::getWmClientLeader()
{
    auto prop = fetchWmClientLeader();
    readWmClientLeader(prop);
}

/**
 * Returns sessionId for this client,
 * taken either from its window or from the leader window.
 */
QByteArray Toplevel::sessionId() const
{
    QByteArray result = Xcb::StringProperty(xcb_window(), atoms->sm_client_id);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != xcb_window()) {
        result = Xcb::StringProperty(m_wmClientLeader, atoms->sm_client_id);
    }
    return result;
}

/**
 * Returns command property for this client,
 * taken either from its window or from the leader window.
 */
QByteArray Toplevel::wmCommand()
{
    QByteArray result = Xcb::StringProperty(xcb_window(), XCB_ATOM_WM_COMMAND);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != xcb_window()) {
        result = Xcb::StringProperty(m_wmClientLeader, XCB_ATOM_WM_COMMAND);
    }
    result.replace(0, ' ');
    return result;
}

void Toplevel::getWmClientMachine()
{
    m_clientMachine->resolve(xcb_window(), wmClientLeader());
}

/**
 * Returns client machine for this client,
 * taken either from its window or from the leader window.
 */
QByteArray Toplevel::wmClientMachine(bool use_localhost) const
{
    if (!m_clientMachine) {
        // this should never happen
        return QByteArray();
    }
    if (use_localhost && m_clientMachine->isLocal()) {
        // special name for the local machine (localhost)
        return ClientMachine::localhost();
    }
    return m_clientMachine->hostName();
}

/**
 * Returns client leader window for this client.
 * Returns the client window itself if no leader window is defined.
 */
xcb_window_t Toplevel::wmClientLeader() const
{
    if (m_wmClientLeader != XCB_WINDOW_NONE) {
        return m_wmClientLeader;
    }
    return xcb_window();
}

void Toplevel::getResourceClass()
{
    setResourceClass(QByteArray(info->windowClassName()).toLower(), QByteArray(info->windowClassClass()).toLower());
}

void Toplevel::setResourceClass(const QByteArray &name, const QByteArray &className)
{
    resource_name  = name;
    resource_class = className;
    emit windowClassChanged();
}

bool Toplevel::resourceMatch(const Toplevel *c1, const Toplevel *c2)
{
    return c1->resourceClass() == c2->resourceClass();
}

double Toplevel::opacity() const
{
    if (m_remnant) {
        return m_remnant->opacity;
    }
    if (info->opacity() == 0xffffffff)
        return 1.0;
    return info->opacity() * 1.0 / 0xffffffff;
}

void Toplevel::setOpacity(double new_opacity)
{
    double old_opacity = opacity();
    new_opacity = qBound(0.0, new_opacity, 1.0);
    if (old_opacity == new_opacity)
        return;
    info->setOpacity(static_cast< unsigned long >(new_opacity * 0xffffffff));
    if (win::compositing()) {
        addRepaintFull();
        emit opacityChanged(this, old_opacity);
    }
}

bool Toplevel::isOutline() const
{
    if (m_remnant) {
        return m_remnant->was_outline;
    }
    return is_outline;
}

bool Toplevel::setupCompositing(bool add_full_damage)
{
    assert(!remnant());

    if (!win::compositing())
        return false;

    if (damage_handle != XCB_NONE)
        return false;

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        assert(!surface());
        damage_handle = xcb_generate_id(connection());
        xcb_damage_create(connection(), damage_handle, frameId(), XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
    }

    discard_shape();
    damage_region = QRegion(QRect(QPoint(), size()));
    effect_window = new EffectWindowImpl(this);

    Compositor::self()->scene()->addToplevel(this);

    if (add_full_damage) {
        // With unmanaged windows there is a race condition between the client painting the window
        // and us setting up damage tracking.  If the client wins we won't get a damage event even
        // though the window has been painted.  To avoid this we mark the whole window as damaged
        // and schedule a repaint immediately after creating the damage object.
        // TODO: move this out of the class.
        addDamageFull();
    }

    return true;
}

void Toplevel::finishCompositing(ReleaseReason releaseReason)
{
    assert(!remnant());

    if (kwinApp()->operationMode() == Application::OperationModeX11 && damage_handle == XCB_NONE)
        return;

    if (effect_window->window() == this) { // otherwise it's already passed to Deleted, don't free data
        discardWindowPixmap();
        delete effect_window;
    }

    if (damage_handle != XCB_NONE &&
            releaseReason != ReleaseReason::Destroyed) {
        xcb_damage_destroy(connection(), damage_handle);
    }

    damage_handle = XCB_NONE;
    damage_region = QRegion();
    repaints_region = QRegion();
    effect_window = nullptr;
}

void Toplevel::discardWindowPixmap()
{
    addDamageFull();
    if (auto scene_window = win::scene_window(this)) {
        scene_window->discardPixmap();
    }
}

void Toplevel::damageNotifyEvent()
{
    m_isDamaged = true;

    // Note: The region is supposed to specify the damage extents,
    //       but we don't know it at this point. No one who connects
    //       to this signal uses the rect however.
    Q_EMIT damaged(this, {});
}

bool Toplevel::resetAndFetchDamage()
{
    if (!m_isDamaged)
        return false;

    if (damage_handle == XCB_NONE) {
        m_isDamaged = false;
        return true;
    }

    xcb_connection_t *conn = connection();

    // Create a new region and copy the damage region to it,
    // resetting the damaged state.
    xcb_xfixes_region_t region = xcb_generate_id(conn);
    xcb_xfixes_create_region(conn, region, 0, nullptr);
    xcb_damage_subtract(conn, damage_handle, 0, region);

    // Send a fetch-region request and destroy the region
    m_regionCookie = xcb_xfixes_fetch_region_unchecked(conn, region);
    xcb_xfixes_destroy_region(conn, region);

    m_isDamaged = false;
    m_damageReplyPending = true;

    return m_damageReplyPending;
}

void Toplevel::getDamageRegionReply()
{
    if (!m_damageReplyPending) {
        return;
    }

    m_damageReplyPending = false;

    // Get the fetch-region reply
    auto reply = xcb_xfixes_fetch_region_reply(connection(), m_regionCookie, nullptr);
    if (!reply) {
        return;
    }

    // Convert the reply to a QRegion. The region is relative to the content geometry.
    auto count = xcb_xfixes_fetch_region_rectangles_length(reply);
    QRegion region;

    if (count > 1 && count < 16) {
        auto rects = xcb_xfixes_fetch_region_rectangles(reply);

        QVector<QRect> qrects;
        qrects.reserve(count);

        for (int i = 0; i < count; i++) {
            qrects << QRect(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        }
        region.setRects(qrects.constData(), count);
    } else {
        region += QRect(reply->extents.x, reply->extents.y,
                        reply->extents.width, reply->extents.height);
    }

    region.translate(-QPoint(client_frame_extents.left(), client_frame_extents.top()));
    repaints_region |= region;

    if (has_in_content_deco) {
        region.translate(-QPoint(win::left_border(this), win::top_border(this)));
    }
    damage_region |= region;

    free(reply);
}

void Toplevel::addDamageFull()
{
    if (!win::compositing()) {
        return;
    }

    auto const render_geo = win::frame_to_render_rect(this, frameGeometry());

    auto const damage = QRect(QPoint(), render_geo.size());
    damage_region = damage;

    auto repaint = damage;
    if (has_in_content_deco) {
        repaint.translate(-QPoint(win::left_border(this), win::top_border(this)));
    }
    repaints_region |= repaint;
    add_repaint_outputs(render_geo);

    Q_EMIT damaged(this, damage_region);
}

void Toplevel::resetDamage()
{
    damage_region = QRegion();
}

void Toplevel::addRepaint(int x, int y, int w, int h)
{
    addRepaint(QRegion(x, y, w, h));
}

void Toplevel::addRepaint(QRect const& rect)
{
    addRepaint(QRegion(rect));
}

void Toplevel::addRepaint(QRegion const& region)
{
    if (!win::compositing()) {
        return;
    }
    repaints_region += region;
    add_repaint_outputs(region.translated(pos()));
    Q_EMIT needsRepaint();
}

void Toplevel::addLayerRepaint(int x, int y, int w, int h)
{
    addLayerRepaint(QRegion(x, y, w, h));
}

void Toplevel::addLayerRepaint(QRect const& rect)
{
    addLayerRepaint(QRegion(rect));
}

void Toplevel::addLayerRepaint(QRegion const& region)
{
    if (!win::compositing()) {
        return;
    }
    layer_repaints_region += region;
    add_repaint_outputs(region);
    Q_EMIT needsRepaint();
}

void Toplevel::addRepaintFull()
{
    auto const region = win::visible_rect(this);
    repaints_region = region.translated(-pos());
    for (auto child : transient()->children) {
        if (child->transient()->annexed) {
            child->addRepaintFull();
        }
    }
    add_repaint_outputs(region);
    Q_EMIT needsRepaint();
}

bool Toplevel::has_pending_repaints() const
{
    return !repaints().isEmpty();
}

QRegion Toplevel::repaints() const
{
    return repaints_region.translated(pos()) | layer_repaints_region;
}

void Toplevel::resetRepaints(AbstractOutput* output)
{
    auto reset_all = [this] {
        repaints_region = QRegion();
        layer_repaints_region = QRegion();
    };

    if (!output) {
        assert(!repaint_outputs.size());
        reset_all();
        return;
    }

    remove_all(repaint_outputs, output);

    if (!repaint_outputs.size()) {
        reset_all();
        return;
    }

    auto reset_region = QRegion(output->geometry());

    for (auto out : repaint_outputs) {
        reset_region = reset_region.subtracted(out->geometry());
    }

    repaints_region.translate(pos());
    repaints_region = repaints_region.subtracted(reset_region);
    repaints_region.translate(-pos());

    layer_repaints_region = layer_repaints_region.subtracted(reset_region);
}

void Toplevel::add_repaint_outputs(QRegion const& region)
{
    if (!waylandServer()) {
        // On X11 we do not paint per output.
        return;
    }
    for (auto& out : kwinApp()->platform()->enabledOutputs()) {
        if (contains(repaint_outputs, out)) {
            continue;
        }
        if (region.intersected(out->geometry()).isEmpty()) {
            continue;
        }
        repaint_outputs.push_back(out);
    }
}

void Toplevel::addWorkspaceRepaint(int x, int y, int w, int h)
{
    addWorkspaceRepaint(QRect(x, y, w, h));
}

void Toplevel::addWorkspaceRepaint(QRect const& rect)
{
    if (!win::compositing()) {
        return;
    }
    Compositor::self()->addRepaint(rect);
}

void Toplevel::setReadyForPainting()
{
    if (!ready_for_painting) {
        ready_for_painting = true;
        if (win::compositing()) {
            addRepaintFull();
            emit windowShown(this);
        }
    }
}

void Toplevel::deleteEffectWindow()
{
    delete effect_window;
    effect_window = nullptr;
}

void Toplevel::checkScreen()
{
    if (screens()->count() == 1) {
        if (m_screen != 0) {
            m_screen = 0;
            emit screenChanged();
        }
    } else {
        const int s = screens()->number(frameGeometry().center());
        if (s != m_screen) {
            m_screen = s;
            emit screenChanged();
        }
    }
    qreal newScale = screens()->scale(m_screen);
    if (newScale != m_screenScale) {
        m_screenScale = newScale;
        emit screenScaleChanged();
    }
}

void Toplevel::setupCheckScreenConnection()
{
    connect(this, &Toplevel::frame_geometry_changed, this, &Toplevel::checkScreen);
    checkScreen();
}

void Toplevel::removeCheckScreenConnection()
{
    disconnect(this, &Toplevel::frame_geometry_changed, this, &Toplevel::checkScreen);
}

int Toplevel::screen() const
{
    return m_screen;
}

qreal Toplevel::screenScale() const
{
    return m_screenScale;
}

qreal Toplevel::bufferScale() const
{
    if (m_remnant) {
        return m_remnant->buffer_scale;
    }
    return surface() ? surface()->scale() : 1;
}

bool Toplevel::wantsShadowToBeRendered() const
{
    return true;
}

void Toplevel::getWmOpaqueRegion()
{
    const auto rects = info->opaqueRegion();
    QRegion new_opaque_region;
    for (const auto &r : rects) {
        new_opaque_region += QRect(r.pos.x, r.pos.y, r.size.width, r.size.height);
    }

    opaque_region = new_opaque_region;
}

bool Toplevel::isClient() const
{
    return false;
}

bool Toplevel::isDeleted() const
{
    return remnant() != nullptr;
}

bool Toplevel::isOnCurrentActivity() const
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return true;
    }
    return isOnActivity(Activities::self()->current());
#else
    return true;
#endif
}

pid_t Toplevel::pid() const
{
    return info->pid();
}

xcb_window_t Toplevel::frameId() const
{
    if (m_remnant) {
        return m_remnant->frame;
    }
    return m_client;
}

void Toplevel::getSkipCloseAnimation()
{
    setSkipCloseAnimation(win::x11::fetch_skip_close_animation(xcb_window()).toBool());
}

void Toplevel::debug(QDebug& stream) const
{
    if (remnant()) {
        stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
    } else {
        stream << "\'ID:" << reinterpret_cast<void const*>(this) << xcb_window() << "\'";
    }
}

bool Toplevel::skipsCloseAnimation() const
{
    return m_skipCloseAnimation;
}

void Toplevel::setSkipCloseAnimation(bool set)
{
    if (set == m_skipCloseAnimation) {
        return;
    }
    m_skipCloseAnimation = set;
    emit skipCloseAnimationChanged();
}

void Toplevel::setSurface(Wrapland::Server::Surface *surface)
{
    using namespace Wrapland::Server;
    Q_ASSERT(surface);

    if (m_surface) {
        // This can happen with XWayland clients since receiving the surface destroy signal through
        // the Wayland connection is independent of when the corresponding X11 unmap/map events
        // are received.
        disconnect(m_surface, nullptr, this, nullptr);

        disconnect(this, &Toplevel::frame_geometry_changed, this, &Toplevel::updateClientOutputs);
        disconnect(screens(), &Screens::changed, this, &Toplevel::updateClientOutputs);
    } else {
        // Need to setup this connections since setSurface was never called before or
        // the surface had been destroyed before what disconnected them.
        connect(this, &Toplevel::frame_geometry_changed, this, &Toplevel::updateClientOutputs);
        connect(screens(), &Screens::changed, this, &Toplevel::updateClientOutputs);
    }

    m_surface = surface;

    connect(m_surface, &Surface::damaged, this, &Toplevel::addDamage);
    connect(m_surface, &Surface::sizeChanged, this, [this]{
        discardWindowPixmap();
        if (m_surface->client() == waylandServer()->xWaylandConnection()) {
            // Quads for Xwayland clients need for size emulation.
            // Also apparently needed for unmanaged Xwayland clients (compare Kate's open-file
            // dialog when type-forward list is changing size).
            // TODO(romangg): can this be put in a less hot path?
            discard_quads();
        }
    });

    connect(m_surface, &Surface::subsurfaceTreeChanged, this,
        [this] {
            // TODO improve to only update actual visual area
            if (ready_for_painting) {
                addDamageFull();
                m_isDamaged = true;
            }
        }
    );
    connect(m_surface, &Surface::destroyed, this,
        [this] {
            m_surface = nullptr;
            m_surfaceId = 0;
            disconnect(this, &Toplevel::frame_geometry_changed, this, &Toplevel::updateClientOutputs);
            disconnect(screens(), &Screens::changed, this, &Toplevel::updateClientOutputs);
        }
    );
    m_surfaceId = surface->id();
    updateClientOutputs();
    emit surfaceChanged();
}

void Toplevel::updateClientOutputs()
{
    std::vector<Wrapland::Server::Output*> clientOutputs;
    const auto outputs = waylandServer()->display()->outputs();
    for (auto output : outputs) {
        if (frameGeometry().intersects(output->output()->geometry().toRect())) {
            clientOutputs.push_back(output->output());
        }
    }
    surface()->setOutputs(clientOutputs);
}

// TODO(romangg): This function is only called on Wayland and the damage translation is not the
//                usual way. Unify that.
void Toplevel::addDamage(const QRegion &damage)
{
    auto const render_region = win::render_geometry(this);
    repaints_region += damage.translated(render_region.topLeft() - pos());
    add_repaint_outputs(render_region);

    m_isDamaged = true;
    damage_region += damage;
    emit damaged(this, damage);
}

QByteArray Toplevel::windowRole() const
{
    if (m_remnant) {
        return m_remnant->window_role;
    }
    return QByteArray(info->windowRole());
}

void Toplevel::setDepth(int depth)
{
    if (bit_depth == depth) {
        return;
    }
    const bool oldAlpha = hasAlpha();
    bit_depth = depth;
    if (oldAlpha != hasAlpha()) {
        emit hasAlphaChanged();
    }
}

QMatrix4x4 Toplevel::input_transform() const
{
    QMatrix4x4 transform;

    auto const render_pos = win::frame_to_render_pos(this, pos());
    transform.translate(-render_pos.x(), -render_pos.y());

    return transform;
}

quint32 Toplevel::windowId() const
{
    return xcb_window();
}

void Toplevel::set_frame_geometry(QRect const& rect)
{
    m_frameGeometry = rect;
}

void Toplevel::discard_shape()
{
    m_render_shape_valid = false;
    discard_quads();
}

void Toplevel::discard_quads()
{
    if (auto scene_window = win::scene_window(this)) {
        scene_window->invalidateQuadsCache();
        addRepaintFull();
    }
    if (transient()->annexed) {
        for (auto lead : transient()->leads()) {
            lead->discard_quads();
        }
    }
}

QRegion Toplevel::render_region() const
{
    if (m_remnant) {
        return m_remnant->render_region;
    }

    auto const render_geo = win::render_geometry(this);

    if (is_shape) {
        if (m_render_shape_valid) {
            return m_render_shape;
        }
        m_render_shape_valid = true;
        m_render_shape = QRegion();

        auto cookie
            = xcb_shape_get_rectangles_unchecked(connection(), frameId(), XCB_SHAPE_SK_BOUNDING);
        ScopedCPointer<xcb_shape_get_rectangles_reply_t> reply(
            xcb_shape_get_rectangles_reply(connection(), cookie, nullptr));
        if (reply.isNull()) {
            return QRegion();
        }

        auto const rects = xcb_shape_get_rectangles_rectangles(reply.data());
        auto const rect_count = xcb_shape_get_rectangles_rectangles_length(reply.data());
        for (int i = 0; i < rect_count; ++i) {
            m_render_shape += QRegion(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        }

        // make sure the shape is sane (X is async, maybe even XShape is broken)
        m_render_shape &= QRegion(0, 0, render_geo.width(), render_geo.height());
        return m_render_shape;
    }

    return QRegion(0, 0, render_geo.width(), render_geo.height());
}

bool Toplevel::isLocalhost() const
{
    if (!m_clientMachine) {
        return true;
    }
    return m_clientMachine->isLocal();
}

bool Toplevel::is_popup_end() const
{
    if (m_remnant) {
        return m_remnant->was_popup_window;
    }
    return false;
}

int Toplevel::desktop() const
{
    // TODO: for remnant special case?
    return m_desktops.isEmpty() ? (int)NET::OnAllDesktops : m_desktops.last()->x11DesktopNumber();
}

QVector<VirtualDesktop *> Toplevel::desktops() const
{
    return m_desktops;
}

void Toplevel::set_desktops(QVector<VirtualDesktop*> const& desktops)
{
    m_desktops = desktops;
}

bool Toplevel::isOnAllActivities() const
{
    return win::on_all_activities(this);
}

bool Toplevel::isOnActivity(const QString &activity) const
{
    return win::on_activity(this, activity);
}

bool Toplevel::isOnAllDesktops() const
{
    return win::on_all_desktops(this);
}

bool Toplevel::isOnDesktop(int d) const
{
    return win::on_desktop(this, d);
}

bool Toplevel::isOnCurrentDesktop() const
{
    return win::on_current_desktop(this);
}

QStringList Toplevel::activities() const
{
    if (m_remnant) {
        return m_remnant->activities;
    }
    return QStringList();
}

win::layer Toplevel::layer() const
{
    if (transient()->lead() && transient()->annexed) {
        return transient()->lead()->layer();
    }
    if (m_layer == win::layer::unknown) {
        const_cast<Toplevel*>(this)->m_layer = win::belong_to_layer(this);
    }
    return m_layer;
}

void Toplevel::set_layer(win::layer layer)
{
    m_layer = layer;;
}

win::layer Toplevel::layer_for_dock() const
{
    assert(control);

    // Slight hack for the 'allow window to cover panel' Kicker setting.
    // Don't move keepbelow docks below normal window, but only to the same
    // layer, so that both may be raised to cover the other.
    if (control->keep_below()) {
        return win::layer::normal;
    }
    if (control->keep_above()) {
        // slight hack for the autohiding panels
        return win::layer::above;
    }
    return win::layer::dock;
}

bool Toplevel::isInternal() const
{
    return false;
}

bool Toplevel::belongsToDesktop() const
{
    return false;
}

void Toplevel::checkTransient([[maybe_unused]] Toplevel* window)
{
}

win::remnant* Toplevel::remnant() const
{
    return m_remnant;
}

win::transient* Toplevel::transient() const
{
    return m_transient.get();
}

bool Toplevel::isCloseable() const
{
    return false;
}

bool Toplevel::isShown() const
{
    return false;
}

bool Toplevel::isHiddenInternal() const
{
    return false;
}

void Toplevel::hideClient([[maybe_unused]] bool hide)
{
}

void Toplevel::setFullScreen([[maybe_unused]] bool set, [[maybe_unused]] bool user)
{
}

win::maximize_mode Toplevel::maximizeMode() const
{
    return win::maximize_mode::restore;
}

bool Toplevel::noBorder() const
{
    if (m_remnant) {
        return m_remnant->no_border;
    }
    return true;
}

void Toplevel::setNoBorder([[maybe_unused]] bool set)
{
}

void Toplevel::blockActivityUpdates([[maybe_unused]] bool b)
{
}

bool Toplevel::isResizable() const
{
    return false;
}

bool Toplevel::isMovable() const
{
    return false;
}

bool Toplevel::isMovableAcrossScreens() const
{
    return false;
}

void Toplevel::takeFocus()
{
}

bool Toplevel::wantsInput() const
{
    return false;
}

bool Toplevel::dockWantsInput() const
{
    return false;
}

bool Toplevel::isMaximizable() const
{
    return false;
}

bool Toplevel::isMinimizable() const
{
    return false;
}

bool Toplevel::userCanSetFullScreen() const
{
    return false;
}

bool Toplevel::userCanSetNoBorder() const
{
    return false;
}

void Toplevel::checkNoBorder()
{
    setNoBorder(false);
}

bool Toplevel::isTransient() const
{
    return transient()->lead();
}

void Toplevel::setOnActivities([[maybe_unused]] QStringList newActivitiesList)
{
}

void Toplevel::setOnAllActivities([[maybe_unused]] bool set)
{
}

xcb_timestamp_t Toplevel::userTime() const
{
    return XCB_TIME_CURRENT_TIME;
}

QSize Toplevel::maxSize() const
{
    return control->rules().checkMaxSize(QSize(INT_MAX, INT_MAX));
}

QSize Toplevel::minSize() const
{
    return control->rules().checkMinSize(QSize(0, 0));
}

void Toplevel::setFrameGeometry([[maybe_unused]] QRect const& rect)
{
}

bool Toplevel::hasStrut() const
{
    return false;
}

void Toplevel::updateDecoration([[maybe_unused]] bool check_workspace_pos,
                                [[maybe_unused]] bool force)
{
}

void Toplevel::layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const
{
    if (m_remnant) {
        return m_remnant->layout_decoration_rects(left, top, right, bottom);
    }
    win::layout_decoration_rects(this, left, top, right, bottom);
}

bool Toplevel::providesContextHelp() const
{
    return false;
}

void Toplevel::showContextHelp()
{
}

void Toplevel::showOnScreenEdge()
{
}

void Toplevel::killWindow()
{
}

bool Toplevel::isInitialPositionSet() const
{
    return false;
}

bool Toplevel::groupTransient() const
{
    return false;
}

win::x11::Group const* Toplevel::group() const
{
    return nullptr;
}

win::x11::Group* Toplevel::group()
{
    return nullptr;
}

bool Toplevel::supportsWindowRules() const
{
    return control != nullptr;
}

QSize Toplevel::basicUnit() const
{
    return QSize(1, 1);
}

void Toplevel::setBlockingCompositing([[maybe_unused]] bool block)
{
}

bool Toplevel::isBlockingCompositing()
{
    return false;
}

bool Toplevel::doStartMoveResize()
{
    return true;
}

void Toplevel::doPerformMoveResize()
{
}

void Toplevel::leaveMoveResize()
{
    workspace()->setMoveResizeClient(nullptr);
    control->move_resize().enabled = false;
    if (ScreenEdges::self()->isDesktopSwitchingMovingClients()) {
        ScreenEdges::self()->reserveDesktopSwitching(false, Qt::Vertical|Qt::Horizontal);
    }
    if (control->electric_maximizing()) {
        outline()->hide();
        win::elevate(this, false);
    }
}

void Toplevel::doResizeSync()
{
}

void Toplevel::doSetActive()
{
}

void Toplevel::doSetKeepAbove()
{
}

void Toplevel::doSetKeepBelow()
{
}

void Toplevel::doMinimize()
{
}

void Toplevel::doSetDesktop([[maybe_unused]] int desktop, [[maybe_unused]] int was_desk)
{
}

bool Toplevel::isWaitingForMoveResizeSync() const
{
    return false;
}

QSize Toplevel::resizeIncrements() const
{
    return QSize(1, 1);
}

void Toplevel::updateColorScheme()
{
}

void Toplevel::updateCaption()
{
}

bool Toplevel::acceptsFocus() const
{
    return false;
}

void Toplevel::update_maximized([[maybe_unused]] win::maximize_mode mode)
{
}

void Toplevel::closeWindow()
{
}

bool Toplevel::performMouseCommand(Options::MouseCommand cmd, const QPoint &globalPos)
{
    return win::perform_mouse_command(this, cmd, globalPos);
}

Toplevel* Toplevel::findModal()
{
    return nullptr;
}

bool Toplevel::belongsToSameApplication([[maybe_unused]] Toplevel const* other,
                                        [[maybe_unused]] win::same_client_check checks) const
{
    return false;
}

QRect Toplevel::iconGeometry() const
{
    auto management = control->wayland_management();
    if (!management || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRect();
    }

    int minDistance = INT_MAX;
    Toplevel* candidatePanel = nullptr;
    QRect candidateGeom;

    for (auto i = management->minimizedGeometries().constBegin(),
         end = management->minimizedGeometries().constEnd(); i != end; ++i) {
        auto client = waylandServer()->findToplevel(i.key());
        if (!client) {
            continue;
        }
        const int distance = QPoint(client->pos() - pos()).manhattanLength();
        if (distance < minDistance) {
            minDistance = distance;
            candidatePanel = client;
            candidateGeom = i.value();
        }
    }
    if (!candidatePanel) {
        return QRect();
    }
    return candidateGeom.translated(candidatePanel->pos());
}

void Toplevel::setWindowHandles(xcb_window_t w)
{
    Q_ASSERT(!m_client.isValid() && w != XCB_WINDOW_NONE);
    m_client.reset(w, false);
}

} // namespace

