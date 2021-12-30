/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_texture.h"

#include "egl_backend.h"

#include "render/gl/egl.h"
#include "render/gl/egl_texture.h"

namespace KWin::render::backend::wlroots
{

egl_texture::egl_texture(gl::texture* texture, egl_backend* backend)
    : gl::texture_private()
    , q(texture)
    , m_backend(backend)
{
    m_target = GL_TEXTURE_2D;
    m_hasSubImageUnpack = hasGLExtension(QByteArrayLiteral("GL_EXT_unpack_subimage"));
}

egl_texture::~egl_texture()
{
    if (m_image != EGL_NO_IMAGE_KHR) {
        eglDestroyImageKHR(m_backend->data.base.display, m_image);
    }
}

gl::backend* egl_texture::backend()
{
    return m_backend;
}

bool egl_texture::loadTexture(window_pixmap* pixmap)
{
    return gl::load_texture_from_pixmap(*this, pixmap);
}

void egl_texture::updateTexture(window_pixmap* pixmap)
{
    gl::update_texture_from_pixmap(*this, pixmap);
}

}
