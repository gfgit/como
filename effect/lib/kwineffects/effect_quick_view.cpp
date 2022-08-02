/*
    SPDX-FileCopyrightText: 2019 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect_quick_view.h"

#include "effects_handler.h"
#include "logging_p.h"
#include "shared_qml_engine.h"

#include <kwingl/utils.h>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QTimer>
#include <QTouchEvent>
#include <QWindow>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QQuickOpenGLUtils>
#include <QQuickRenderTarget>
#include <private/qeventpoint_p.h> // for QMutableEventPoint
#endif

Q_LOGGING_CATEGORY(LIBKWINEFFECTS, "libkwineffects", QtWarningMsg)

namespace KWin
{

static std::unique_ptr<QOpenGLContext> s_shareContext;

class EffectQuickRenderControl : public QQuickRenderControl
{
    Q_OBJECT

public:
    explicit EffectQuickRenderControl(QWindow* renderWindow, QObject* parent = nullptr)
        : QQuickRenderControl(parent)
        , m_renderWindow(renderWindow)
    {
    }

    QWindow* renderWindow(QPoint* offset) override
    {
        if (offset) {
            *offset = QPoint(0, 0);
        }
        return m_renderWindow;
    }

private:
    QPointer<QWindow> m_renderWindow;
};

class Q_DECL_HIDDEN EffectQuickView::Private
{
public:
    QQuickWindow* m_view;
    QQuickRenderControl* m_renderControl;
    QScopedPointer<QOffscreenSurface> m_offscreenSurface;
    QScopedPointer<QOpenGLContext> m_glcontext;
    QScopedPointer<QOpenGLFramebufferObject> m_fbo;

    QTimer* m_repaintTimer;
    QImage m_image;
    QScopedPointer<GLTexture> m_textureExport;
    // if we should capture a QImage after rendering into our BO.
    // Used for either software QtQuick rendering and nonGL kwin rendering
    bool m_useBlit = false;
    bool m_visible = true;
    bool m_automaticRepaint = true;

    QList<QTouchEvent::TouchPoint> touchPoints;
    Qt::TouchPointStates touchState;
    QTouchDevice* touchDevice;

    void releaseResources();

    void updateTouchState(Qt::TouchPointState state, qint32 id, const QPointF& pos);
};

class Q_DECL_HIDDEN EffectQuickScene::Private
{
public:
    Private()
        : qmlEngine(SharedQmlEngine::engine())
    {
    }

    SharedQmlEngine::Ptr qmlEngine;
    QScopedPointer<QQmlComponent> qmlComponent;
    QScopedPointer<QQuickItem> quickItem;
};

EffectQuickView::EffectQuickView(QObject* parent)
    : EffectQuickView(parent, effects ? ExportMode::Texture : ExportMode::Image)
{
}

EffectQuickView::EffectQuickView(QObject* parent, ExportMode exportMode)
    : EffectQuickView(parent, nullptr, exportMode)
{
}

EffectQuickView::EffectQuickView(QObject* parent, QWindow* renderWindow)
    : EffectQuickView(parent, renderWindow, effects ? ExportMode::Texture : ExportMode::Image)
{
}

EffectQuickView::EffectQuickView(QObject* parent, QWindow* renderWindow, ExportMode exportMode)
    : QObject(parent)
    , d(new EffectQuickView::Private)
{
    d->m_renderControl = new EffectQuickRenderControl(renderWindow, this);

    d->m_view = new QQuickWindow(d->m_renderControl);
    d->m_view->setFlags(Qt::FramelessWindowHint);
    d->m_view->setColor(Qt::transparent);

    if (exportMode == ExportMode::Image) {
        d->m_useBlit = true;
    }

    const bool usingGl
        = d->m_view->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL;

    if (!usingGl) {
        qCDebug(LIBKWINEFFECTS) << "QtQuick Software rendering mode detected";
        d->m_useBlit = true;
        d->m_renderControl->initialize(nullptr);
    } else {
        QSurfaceFormat format;
        format.setOption(QSurfaceFormat::ResetNotification);
        format.setDepthBufferSize(16);
        format.setStencilBufferSize(8);

        auto share_context = s_shareContext.get();
        d->m_glcontext.reset(new QOpenGLContext);
        d->m_glcontext->setShareContext(share_context);
        d->m_glcontext->setFormat(format);
        d->m_glcontext->create();

        // and the offscreen surface
        d->m_offscreenSurface.reset(new QOffscreenSurface);
        d->m_offscreenSurface->setFormat(d->m_glcontext->format());
        d->m_offscreenSurface->create();

        d->m_glcontext->makeCurrent(d->m_offscreenSurface.data());
        d->m_renderControl->initialize(d->m_glcontext.data());
        d->m_glcontext->doneCurrent();

        // On Wayland, opengl contexts are implicitly shared.
        if (share_context && !d->m_glcontext->shareContext()) {
            qCDebug(LIBKWINEFFECTS)
                << "Failed to create a shared context, falling back to raster rendering";

            qCDebug(LIBKWINEFFECTS) << "Extra debug:";
            qCDebug(LIBKWINEFFECTS) << "our context:" << d->m_glcontext.data();
            qCDebug(LIBKWINEFFECTS) << "share context:" << share_context;

            // still render via GL, but blit for presentation
            d->m_useBlit = true;
        }
    }

    auto updateSize = [this]() { contentItem()->setSize(d->m_view->size()); };
    updateSize();
    connect(d->m_view, &QWindow::widthChanged, this, updateSize);
    connect(d->m_view, &QWindow::heightChanged, this, updateSize);

    d->m_repaintTimer = new QTimer(this);
    d->m_repaintTimer->setSingleShot(true);
    d->m_repaintTimer->setInterval(10);

    connect(d->m_repaintTimer, &QTimer::timeout, this, &EffectQuickView::update);
    connect(d->m_renderControl,
            &QQuickRenderControl::renderRequested,
            this,
            &EffectQuickView::handleRenderRequested);
    connect(d->m_renderControl,
            &QQuickRenderControl::sceneChanged,
            this,
            &EffectQuickView::handleSceneChanged);

    d->touchDevice = new QTouchDevice{};
    d->touchDevice->setCapabilities(QTouchDevice::Position);
    d->touchDevice->setType(QTouchDevice::TouchScreen);
    d->touchDevice->setMaximumTouchPoints(10);
}

EffectQuickView::~EffectQuickView()
{
    if (d->m_glcontext) {
        // close the view whilst we have an active GL context
        d->m_glcontext->makeCurrent(d->m_offscreenSurface.data());
    }

    delete d->m_renderControl; // Always delete render control first.
    d->m_renderControl = nullptr;

    delete d->m_view;
    d->m_view = nullptr;
}

bool EffectQuickView::automaticRepaint() const
{
    return d->m_automaticRepaint;
}

void EffectQuickView::setAutomaticRepaint(bool set)
{
    if (d->m_automaticRepaint != set) {
        d->m_automaticRepaint = set;

        // If there's an in-flight update, disable it.
        if (!d->m_automaticRepaint) {
            d->m_repaintTimer->stop();
        }
    }
}

void EffectQuickView::handleSceneChanged()
{
    if (d->m_automaticRepaint) {
        d->m_repaintTimer->start();
    }
    Q_EMIT sceneChanged();
}

void EffectQuickView::handleRenderRequested()
{
    if (d->m_automaticRepaint) {
        d->m_repaintTimer->start();
    }
    Q_EMIT renderRequested();
}

void EffectQuickView::update()
{
    if (!d->m_visible) {
        return;
    }
    if (d->m_view->size().isEmpty()) {
        return;
    }

    bool usingGl = d->m_glcontext;

    if (usingGl) {
        if (!d->m_glcontext->makeCurrent(d->m_offscreenSurface.data())) {
            // probably a context loss event, kwin is about to reset all the effects anyway
            return;
        }

        const QSize nativeSize = d->m_view->size() * d->m_view->effectiveDevicePixelRatio();
        if (d->m_fbo.isNull() || d->m_fbo->size() != nativeSize) {
            d->m_textureExport.reset(nullptr);
            d->m_fbo.reset(new QOpenGLFramebufferObject(
                nativeSize, QOpenGLFramebufferObject::CombinedDepthStencil));
            if (!d->m_fbo->isValid()) {
                d->m_fbo.reset();
                d->m_glcontext->doneCurrent();
                return;
            }
        }
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        d->m_view->setRenderTarget(d->m_fbo.data());
#else
        d->m_view->setRenderTarget(
            QQuickRenderTarget::fromOpenGLTexture(d->m_fbo->texture(), d->m_fbo->size()));
#endif
    }

    d->m_renderControl->polishItems();
    d->m_renderControl->sync();

    d->m_renderControl->render();
    if (usingGl) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        d->m_view->resetOpenGLState();
#else
        QQuickOpenGLUtils::resetOpenGLState();
#endif
    }

    if (d->m_useBlit) {
        d->m_image = d->m_renderControl->grab();
    }

    if (usingGl) {
        QOpenGLFramebufferObject::bindDefault();
        d->m_glcontext->doneCurrent();
    }
    Q_EMIT repaintNeeded();
}

void EffectQuickView::forwardMouseEvent(QEvent* e)
{
    if (!d->m_visible) {
        return;
    }
    switch (e->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick: {
        QMouseEvent* me = static_cast<QMouseEvent*>(e);
        const QPoint widgetPos = d->m_view->mapFromGlobal(me->pos());
        QMouseEvent cloneEvent(
            me->type(), widgetPos, me->pos(), me->button(), me->buttons(), me->modifiers());
        QCoreApplication::sendEvent(d->m_view, &cloneEvent);
        e->setAccepted(cloneEvent.isAccepted());
        return;
    }
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::HoverMove: {
        QHoverEvent* he = static_cast<QHoverEvent*>(e);
        const QPointF widgetPos = d->m_view->mapFromGlobal(he->pos());
        const QPointF oldWidgetPos = d->m_view->mapFromGlobal(he->oldPos());
        QHoverEvent cloneEvent(he->type(), widgetPos, oldWidgetPos, he->modifiers());
        QCoreApplication::sendEvent(d->m_view, &cloneEvent);
        e->setAccepted(cloneEvent.isAccepted());
        return;
    }
    case QEvent::Wheel: {
        QWheelEvent* we = static_cast<QWheelEvent*>(e);
        const QPointF widgetPos = d->m_view->mapFromGlobal(we->pos());
        QWheelEvent cloneEvent(widgetPos,
                               we->globalPosF(),
                               we->pixelDelta(),
                               we->angleDelta(),
                               we->buttons(),
                               we->modifiers(),
                               we->phase(),
                               we->inverted());
        QCoreApplication::sendEvent(d->m_view, &cloneEvent);
        e->setAccepted(cloneEvent.isAccepted());
        return;
    }
    default:
        return;
    }
}

void EffectQuickView::forwardKeyEvent(QKeyEvent* keyEvent)
{
    if (!d->m_visible) {
        return;
    }
    QCoreApplication::sendEvent(d->m_view, keyEvent);
}

void EffectQuickView::setShareContext(std::unique_ptr<QOpenGLContext> context)
{
    s_shareContext = std::move(context);
}

bool EffectQuickView::forwardTouchDown(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(time)

    d->updateTouchState(Qt::TouchPointPressed, id, pos);

    QTouchEvent event(
        QEvent::TouchBegin, d->touchDevice, Qt::NoModifier, d->touchState, d->touchPoints);
    QCoreApplication::sendEvent(d->m_view, &event);

    return event.isAccepted();
}

bool EffectQuickView::forwardTouchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(time)

    d->updateTouchState(Qt::TouchPointMoved, id, pos);

    QTouchEvent event(
        QEvent::TouchUpdate, d->touchDevice, Qt::NoModifier, d->touchState, d->touchPoints);
    QCoreApplication::sendEvent(d->m_view, &event);

    return event.isAccepted();
}

bool EffectQuickView::forwardTouchUp(qint32 id, quint32 time)
{
    Q_UNUSED(time)

    d->updateTouchState(Qt::TouchPointReleased, id, QPointF{});

    QTouchEvent event(
        QEvent::TouchEnd, d->touchDevice, Qt::NoModifier, d->touchState, d->touchPoints);
    QCoreApplication::sendEvent(d->m_view, &event);

    return event.isAccepted();
}

QRect EffectQuickView::geometry() const
{
    return d->m_view->geometry();
}

void EffectQuickView::setOpacity(qreal opacity)
{
    d->m_view->setOpacity(opacity);
}

qreal EffectQuickView::opacity() const
{
    return d->m_view->opacity();
}

QQuickItem* EffectQuickView::contentItem() const
{
    return d->m_view->contentItem();
}

void EffectQuickView::setVisible(bool visible)
{
    if (d->m_visible == visible) {
        return;
    }
    d->m_visible = visible;

    if (visible) {
        Q_EMIT d->m_renderControl->renderRequested();
    } else {
        // deferred to not change GL context
        QTimer::singleShot(0, this, [this]() { d->releaseResources(); });
    }
}

bool EffectQuickView::isVisible() const
{
    return d->m_visible;
}

void EffectQuickView::show()
{
    setVisible(true);
}

void EffectQuickView::hide()
{
    setVisible(false);
}

GLTexture* EffectQuickView::bufferAsTexture()
{
    if (d->m_useBlit) {
        if (d->m_image.isNull()) {
            return nullptr;
        }
        d->m_textureExport.reset(new GLTexture(d->m_image));
    } else {
        if (!d->m_fbo) {
            return nullptr;
        }
        if (!d->m_textureExport) {
            d->m_textureExport.reset(new GLTexture(
                d->m_fbo->texture(), d->m_fbo->format().internalTextureFormat(), d->m_fbo->size()));
        }
    }
    return d->m_textureExport.data();
}

QImage EffectQuickView::bufferAsImage() const
{
    return d->m_image;
}

QSize EffectQuickView::size() const
{
    return d->m_view->geometry().size();
}

void EffectQuickView::setGeometry(const QRect& rect)
{
    const QRect oldGeometry = d->m_view->geometry();
    d->m_view->setGeometry(rect);
    Q_EMIT geometryChanged(oldGeometry, rect);
}

void EffectQuickView::Private::releaseResources()
{
    if (m_glcontext) {
        m_glcontext->makeCurrent(m_offscreenSurface.data());
        m_view->releaseResources();
        m_glcontext->doneCurrent();
    } else {
        m_view->releaseResources();
    }
}

void EffectQuickView::Private::updateTouchState(Qt::TouchPointState state,
                                                qint32 id,
                                                const QPointF& pos)
{
    // Remove the points that were previously in a released state, since they
    // are no longer relevant. Additionally, reset the state of all remaining
    // points to Stationary so we only have one touch point with a different
    // state.
    touchPoints.erase(std::remove_if(touchPoints.begin(),
                                     touchPoints.end(),
                                     [](QTouchEvent::TouchPoint& point) {
                                         if (point.state() == Qt::TouchPointReleased) {
                                             return true;
                                         }
                                         point.setState(Qt::TouchPointStationary);
                                         return false;
                                     }),
                      touchPoints.end());

    // QtQuick Pointer Handlers incorrectly consider a touch point with ID 0
    // to be an invalid touch point. This has been fixed in Qt 6 but could not
    // be fixed for Qt 5. Instead, we offset kwin's internal IDs with this
    // offset to trick QtQuick into treating them as valid points.
    static const qint32 idOffset = 111;

    // Find the touch point that has changed. This is separate from the above
    // loop because removing the released touch points invalidates iterators.
    auto changed = std::find_if(
        touchPoints.begin(), touchPoints.end(), [id](const QTouchEvent::TouchPoint& point) {
            return point.id() == id + idOffset;
        });

    switch (state) {
    case Qt::TouchPointPressed: {
        if (changed != touchPoints.end()) {
            return;
        }

        QTouchEvent::TouchPoint point;
        point.setId(id + idOffset);
        point.setState(Qt::TouchPointPressed);
        point.setScreenPos(pos);
        point.setScenePos(m_view->mapFromGlobal(pos.toPoint()));
        point.setPos(m_view->mapFromGlobal(pos.toPoint()));

        touchPoints.append(point);
    } break;
    case Qt::TouchPointMoved: {
        if (changed == touchPoints.end()) {
            return;
        }

        auto& point = *changed;
        point.setLastPos(point.pos());
        point.setLastScenePos(point.scenePos());
        point.setLastScreenPos(point.screenPos());
        point.setScenePos(m_view->mapFromGlobal(pos.toPoint()));
        point.setPos(m_view->mapFromGlobal(pos.toPoint()));
        point.setScreenPos(pos);
        point.setState(Qt::TouchPointMoved);
    } break;
    case Qt::TouchPointReleased: {
        if (changed == touchPoints.end()) {
            return;
        }

        auto& point = *changed;
        point.setLastPos(point.pos());
        point.setLastScreenPos(point.screenPos());
        point.setState(Qt::TouchPointReleased);
    } break;
    default:
        break;
    }

    // The touch state value is used in QTouchEvent and includes all the states
    // that the current touch points are in.
    touchState = std::accumulate(touchPoints.begin(),
                                 touchPoints.end(),
                                 Qt::TouchPointStates{},
                                 [](auto init, const auto& point) { return init | point.state(); });
}

EffectQuickScene::EffectQuickScene(QObject* parent)
    : EffectQuickView(parent)
    , d(new EffectQuickScene::Private)
{
}

EffectQuickScene::EffectQuickScene(QObject* parent, QWindow* renderWindow)
    : EffectQuickView(parent, renderWindow)
    , d(new EffectQuickScene::Private)
{
}

EffectQuickScene::EffectQuickScene(QObject* parent, QWindow* renderWindow, ExportMode exportMode)
    : EffectQuickView(parent, renderWindow, exportMode)
    , d(new EffectQuickScene::Private)
{
}

EffectQuickScene::EffectQuickScene(QObject* parent, EffectQuickView::ExportMode exportMode)
    : EffectQuickView(parent, exportMode)
    , d(new EffectQuickScene::Private)
{
}

EffectQuickScene::~EffectQuickScene() = default;

void EffectQuickScene::setSource(const QUrl& source)
{
    setSource(source, QVariantMap());
}

void EffectQuickScene::setSource(const QUrl& source, const QVariantMap& initialProperties)
{
    if (!d->qmlComponent) {
        d->qmlComponent.reset(new QQmlComponent(d->qmlEngine.data()));
    }

    d->qmlComponent->loadUrl(source);
    if (d->qmlComponent->isError()) {
        qCWarning(LIBKWINEFFECTS).nospace()
            << "Failed to load effect quick view " << source << ": " << d->qmlComponent->errors();
        d->qmlComponent.reset();
        return;
    }

    d->quickItem.reset();

    QScopedPointer<QObject> qmlObject(
        d->qmlComponent->createWithInitialProperties(initialProperties));
    QQuickItem* item = qobject_cast<QQuickItem*>(qmlObject.data());
    if (!item) {
        qCWarning(LIBKWINEFFECTS) << "Root object of effect quick view" << source
                                  << "is not a QQuickItem";
        return;
    }

    qmlObject.take();
    d->quickItem.reset(item);

    item->setParentItem(contentItem());

    auto updateSize = [item, this]() { item->setSize(contentItem()->size()); };
    updateSize();
    connect(contentItem(), &QQuickItem::widthChanged, item, updateSize);
    connect(contentItem(), &QQuickItem::heightChanged, item, updateSize);
}

QQmlContext* EffectQuickScene::rootContext() const
{
    return d->qmlEngine->rootContext();
}

QQuickItem* EffectQuickScene::rootItem() const
{
    return d->quickItem.data();
}

}

#include "effect_quick_view.moc"
