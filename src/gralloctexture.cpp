/*
 * Copyright (C) 2022 UBports Foundation
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gralloctexture.h"

#include <QOpenGLFramebufferObject>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>

#include <exception>

#undef None
#include <private/qdrawhelper_p.h>

extern "C" {
void hybris_ui_initialize();
}

uint32_t GrallocTextureCreator::convertUsage(const QImage& image)
{
    return GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_HW_RENDER |
           GRALLOC_USAGE_HW_TEXTURE;
}

uint32_t GrallocTextureCreator::convertLockUsage(const QImage& image)
{
    return GRALLOC_USAGE_SW_WRITE_RARELY;
}

int GrallocTextureCreator::convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader, AlphaBehavior& alphaBehavior)
{
    qInfo() << "format:" << image.format();

    switch (image.format()) {
    case QImage::Format_Mono:
        break;
    case QImage::Format_MonoLSB:
        break;
    case QImage::Format_Indexed8:
        break;
#if 0
    case QImage::Format_RGB32:
        conversionShader = ColorShader_FixupRgb32;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;
#endif
    case QImage::Format_ARGB32:
        break;
#if 1
    case QImage::Format_ARGB32_Premultiplied:
        conversionShader = ColorShader_ArgbToRgba;
        //alphaBehavior = AlphaBehavior_None;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;
#endif
    case QImage::Format_RGB16:
        break;
    case QImage::Format_ARGB8565_Premultiplied:
        break;
    case QImage::Format_RGB666:
        break;
    case QImage::Format_ARGB6666_Premultiplied:
        break;
    case QImage::Format_RGB555:
        break;
    case QImage::Format_ARGB8555_Premultiplied:
        break;
    case QImage::Format_RGB888:
        numChannels = 3;
        return HAL_PIXEL_FORMAT_RGB_888;
    case QImage::Format_RGB444:
        break;
    case QImage::Format_ARGB4444_Premultiplied:
        break;
    case QImage::Format_RGBX8888:
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBX_8888;
    case QImage::Format_RGBA8888:
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;
    case QImage::Format_RGBA8888_Premultiplied:
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;
    case QImage::Format_BGR30:
        break;
    case QImage::Format_A2BGR30_Premultiplied:
        break;
    case QImage::Format_RGB30:
        break;
    case QImage::Format_A2RGB30_Premultiplied:
        break;
    case QImage::Format_Alpha8:
        break;
    case QImage::Format_Grayscale8:
        break;
    case QImage::Format_RGBX64:
        break;
    case QImage::Format_RGBA64:
        break;
    case QImage::Format_RGBA64_Premultiplied:
        break;
    default:
        break;
    }
    return -1;
}

QSGTexture* GrallocTextureCreator::createTexture(const QImage& image, ShaderCache& cachedShaders)
{
    int numChannels = 0;
    ColorShader conversionShader = ColorShader_None;
    AlphaBehavior alphaBehavior = AlphaBehavior_None;

    const int format = convertFormat(image, numChannels, conversionShader, alphaBehavior);
    if (format < 0) {
        qDebug() << "Unknown color format" << image.format();
        return nullptr;
    }
    const uint32_t usage = convertUsage(image);
    const int width = image.width();
    const int height = image.height();

    struct graphic_buffer* handle = nullptr;

    handle = graphic_buffer_new_sized(width, height, format, usage);
    if (!handle) {
        qDebug() << "No buffer allocated";
        return nullptr;
    }

    const int stride = graphic_buffer_get_stride(handle);
    const int lockUsage = convertLockUsage(image);
    void* vmemAddr = nullptr;
    graphic_buffer_lock(handle, lockUsage, &vmemAddr);

    GrallocTexture* texture = nullptr;

    if (vmemAddr) {
        int bytesPerLine = image.bytesPerLine();
        const uchar* sourceAddr = image.constBits();
        const int dbpl = stride * 4;
        char* data = (char*)vmemAddr;

        for (int i = 0; i < height; i++) {
            void* dst = (vmemAddr + (stride * numChannels * i));
            const void* src = (sourceAddr + (bytesPerLine * i));
            memcpy(dst, src, bytesPerLine);
        }
        graphic_buffer_unlock(handle);

        const QSize imageSize = image.size();
        const bool hasAlphaChannel = image.hasAlphaChannel();
        texture = new GrallocTexture(handle, imageSize, hasAlphaChannel, cachedShaders[conversionShader]);

        vmemAddr = nullptr;
        handle = nullptr;
    } else {
        graphic_buffer_unlock(handle);
        vmemAddr = nullptr;
        handle = nullptr;
    }

    qDebug() << "Texture:" << texture;
    return texture;
}

GrallocTexture::GrallocTexture(struct graphic_buffer* handle, const QSize& size, const bool& hasAlphaChannel, ShaderBundle conversionShader) :
    QSGTexture(), m_handle(handle), m_image(EGL_NO_IMAGE_KHR), m_size(size),
    m_hasAlphaChannel(hasAlphaChannel), m_shaderCode(conversionShader), m_usesShader(true), m_drawn(false)
{
    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    if (!eglCreateImageKHR)
        throw std::runtime_error("eglCreateImageKHR");

    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (!eglDestroyImageKHR)
        throw std::runtime_error("eglDestroyImageKHR");

    glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!glEGLImageTargetTexture2DOES)
        throw std::runtime_error("glEGLImageTargetTexture2DOES");

    if (m_image == EGL_NO_IMAGE_KHR) {
        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLContext context = EGL_NO_CONTEXT;
        EGLint attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };

        void* native_buffer = graphic_buffer_get_native_buffer(m_handle);
        m_image = eglCreateImageKHR(dpy, context, EGL_NATIVE_BUFFER_ANDROID, native_buffer, attrs);
    }
}

GrallocTexture::GrallocTexture() : m_usesShader(false), m_drawn(false)
{
}

GrallocTexture::~GrallocTexture()
{
    if (m_texture > 0) {
        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        gl->glDeleteTextures(1, &m_texture);
    }

    EGLDisplay dpy = eglGetCurrentDisplay();
    eglDestroyImageKHR(dpy, m_image);
    graphic_buffer_free(m_handle);
}

int GrallocTexture::textureId() const
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    return m_texture;
}

QSize GrallocTexture::textureSize() const
{
    return m_size;
}

bool GrallocTexture::hasAlphaChannel() const
{
    return m_hasAlphaChannel;
}

bool GrallocTexture::hasMipmaps() const
{
    return false;
}

void GrallocTexture::renderShader(QOpenGLFunctions* gl) const
{
    const auto width = graphic_buffer_get_width(m_handle);
    const auto height = graphic_buffer_get_height(m_handle);
    const int textureUnit = 0;
    int samLocation = -1;
    int texLocation = -1;
    int posLocation = -1;

    static const GLfloat vertices[] = {
        -1,-1, 0,
        -1, 1, 0,
        1,-1, 0,
        -1, 1, 0,
        1,-1, 0,
        1, 1, 0
    };

    static const GLfloat texcoords[] = {
        0, 0,
        0, 1,
        1, 0,
        0, 1,
        1, 0,
        1, 1
    };

    QOpenGLFramebufferObject fbo(width, height);
    if (!fbo.isValid()) {
        qWarning() << "Failed to set up FBO";
        return;
    }

    fbo.bind();
    m_shaderCode.program->bind();

    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vertexBuffer;
    QOpenGLBuffer textureBuffer;

    vao.create();
    vertexBuffer.create();
    vertexBuffer.bind();
    vertexBuffer.allocate(vertices, sizeof(vertices));
    textureBuffer.create();
    textureBuffer.bind();
    textureBuffer.allocate(texcoords, sizeof(texcoords));

    samLocation = m_shaderCode.program->uniformLocation("tex");
    m_shaderCode.program->setUniformValue(samLocation, 0);

    posLocation = m_shaderCode.program->attributeLocation("vertexCoord");
    m_shaderCode.program->setAttributeBuffer(posLocation, GL_FLOAT, 0, 3, 0);
    m_shaderCode.program->enableAttributeArray(posLocation);

    texLocation = m_shaderCode.program->attributeLocation("textureCoord");
    m_shaderCode.program->setAttributeBuffer(texLocation, GL_FLOAT, 0, 2, 0);
    m_shaderCode.program->enableAttributeArray(texLocation);

    // "Dump" the EGLImage onto the fbo
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);

    gl->glDrawArrays(GL_TRIANGLES, 0, 6);

    vertexBuffer.destroy();
    textureBuffer.destroy();
    vao.destroy();

    vertexBuffer.release();
    textureBuffer.release();
    vao.release();
    m_shaderCode.program->release();

    m_texture = fbo.takeTexture();
}

void GrallocTexture::bind()
{
    m_bound = true;

    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    if (m_texture == 0) {
        if (m_usesShader) {
            renderShader(gl);
        } else {
            gl->glGenTextures(1, &m_texture);
        }
    }

    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    if (!m_usesShader) {
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);
    }

    m_drawn = true;
}