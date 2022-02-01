/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwingltexture.h"

#include <QRegion>
#include <deque>
#include <epoxy/egl.h>
#include <memory>

namespace KWin::render::backend::wlroots
{

class egl_backend;
class output;

class egl_output
{
public:
    egl_output(output& out, egl_backend* egl_back);
    egl_output(egl_output const&) = delete;
    egl_output& operator=(egl_output const&) = delete;
    egl_output(egl_output&& other) noexcept;
    egl_output& operator=(egl_output&& other) noexcept;
    ~egl_output();

    bool reset();
    bool reset_framebuffer();
    void cleanup_framebuffer();

    void make_current() const;
    bool present();

    output* out;
    int bufferAge{0};
    egl_backend* egl_back;

    /** Damage history for the past 10 frames. */
    std::deque<QRegion> damageHistory;

    struct {
        GLuint framebuffer = 0;
        GLuint texture = 0;
        std::shared_ptr<GLVertexBuffer> vbo;
    } render;
};

}
