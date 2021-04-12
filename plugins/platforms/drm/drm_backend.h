/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_DRM_BACKEND_H
#define KWIN_DRM_BACKEND_H
#include "platform.h"
#include "input.h"

#include "drm_buffer.h"
#if HAVE_GBM
#include "drm_buffer_gbm.h"
#endif
#include "drm_pointer.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QSize>
#include <QVector>
#include <xf86drmMode.h>

#include <memory>

struct gbm_bo;
struct gbm_device;
struct gbm_surface;

namespace KWin
{

class Udev;
class UdevMonitor;

class DrmOutput;
class DrmPlane;
class DrmCrtc;
class DrmConnector;
class GbmSurface;


class KWIN_EXPORT DrmBackend : public Platform
{
    Q_OBJECT
    Q_INTERFACES(KWin::Platform)
    Q_PLUGIN_METADATA(IID "org.kde.kwin.Platform" FILE "drm.json")
public:
    explicit DrmBackend(QObject *parent = nullptr);
    ~DrmBackend() override;

    QPainterBackend *createQPainterBackend() override;
    OpenGLBackend* createOpenGLBackend() override;

    void init() override;
    void prepareShutdown() override;

    DrmDumbBuffer *createBuffer(const QSize &size);
#if HAVE_GBM
    DrmSurfaceBuffer *createBuffer(const std::shared_ptr<GbmSurface> &surface);
#endif
    bool present(DrmBuffer *buffer, DrmOutput *output);

    int fd() const {
        return m_fd;
    }
    Outputs outputs() const override;
    Outputs enabledOutputs() const override;
    QVector<DrmOutput*> drmOutputs() const {
        return m_outputs;
    }
    QVector<DrmOutput*> drmEnabledOutputs() const {
        return m_enabledOutputs;
    }

    void enableOutput(DrmOutput *output, bool enable);

    QVector<DrmPlane*> planes() const {
        return m_planes;
    }
    QVector<DrmPlane*> overlayPlanes() const {
        return m_overlayPlanes;
    }

    // QPainter reuses buffers
    bool deleteBufferAfterPageFlip() const {
        return m_deleteBufferAfterPageFlip;
    }
    // returns use of AMS, default is not/legacy
    bool atomicModeSetting() const {
        return m_atomicModeSetting;
    }

    void setGbmDevice(gbm_device *device) {
        m_gbmDevice = device;
    }
    gbm_device *gbmDevice() const {
        return m_gbmDevice;
    }

    QByteArray devNode() const {
        return m_devNode;
    }

#if HAVE_EGL_STREAMS
    bool useEglStreams() const {
        return m_useEglStreams;
    }
#endif

    QVector<CompositingType> supportedCompositors() const override;

    QString supportInformation() const override;

protected:
    void doHideCursor() override;
    void doShowCursor() override;

    bool supportsClockId() const override;
    clockid_t clockId() const override;

private:
    static void atomicFlipHandler(int fd, unsigned int frame, unsigned int sec, unsigned int usec,
                                  unsigned int crtc_id, void *data);
    static void legacyFlipHandler(int fd, unsigned int frame, unsigned int sec, unsigned int usec,
                                    void *data);

    void openDrm();
    void activate(bool active);
    void reactivate();
    void deactivate();
    void updateOutputs();
    void setCursor();
    void updateCursor();
    void moveCursor();
    void initCursor();
    DrmOutput *findOutput(quint32 connector);
    QScopedPointer<Udev> m_udev;
    QScopedPointer<UdevMonitor> m_udevMonitor;
    int m_fd = -1;
    int m_drmId = 0;
    // all crtcs
    QVector<DrmCrtc*> m_crtcs;
    // all connectors
    QVector<DrmConnector*> m_connectors;
    // active output pipelines (planes + crtc + encoder + connector)
    QVector<DrmOutput*> m_outputs;
    // active and enabled pipelines (above + wl_output)
    QVector<DrmOutput*> m_enabledOutputs;

    bool m_deleteBufferAfterPageFlip;
    bool m_atomicModeSetting = false;
    bool m_cursorEnabled = false;

    bool m_supportsClockId;
    clockid_t m_clockId;

    QSize m_cursorSize;
    int m_pageFlipsPending = 0;
    bool m_active = false;
    QByteArray m_devNode;
#if HAVE_EGL_STREAMS
    bool m_useEglStreams = false;
#endif
    // all available planes: primarys, cursors and overlays
    QVector<DrmPlane*> m_planes;
    QVector<DrmPlane*> m_overlayPlanes;
    gbm_device *m_gbmDevice = nullptr;
};


}

#endif

