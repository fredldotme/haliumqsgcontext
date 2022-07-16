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

#include <exception>

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

int GrallocTextureCreator::convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader)
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
    ColorShader conversionShader;

    const int format = convertFormat(image, numChannels, conversionShader);
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
        int bytesPerPixel = image.bytesPerLine();
        const uchar* sourceAddr = image.constBits();

        for (int i = 0; i < height; i++) {
            void* dst = (vmemAddr + (stride * numChannels * i));
            const void* src = (sourceAddr + (bytesPerPixel * i));
            memcpy(dst, src, bytesPerPixel);
        }

        const QSize imageSize = image.size();
        const bool hasAlphaChannel = image.hasAlphaChannel();
        texture = new GrallocTexture(handle, imageSize, hasAlphaChannel, cachedShaders[conversionShader]);

        graphic_buffer_unlock(handle);
        vmemAddr = nullptr;
        handle = nullptr;
    }

    qDebug() << "Texture:" << texture;
    return texture;
}

GrallocTexture::GrallocTexture(struct graphic_buffer* handle, const QSize& size, const bool& hasAlphaChannel, ShaderBundle conversionShader) :
    QSGTexture(), m_handle(handle), m_image(EGL_NO_IMAGE_KHR), m_texture(0), m_size(size),
    m_hasAlphaChannel(hasAlphaChannel), m_shaderCode(conversionShader), m_usesShader(m_shaderCode.program != 0)
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

GrallocTexture::GrallocTexture() : m_usesShader(false)
{
}

GrallocTexture::~GrallocTexture()
{
    if (m_texture > 0) {
        glDeleteTextures(1, &m_texture);
    }

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglDestroyImageKHR(dpy, m_image);
    graphic_buffer_free(m_handle);
}

int GrallocTexture::textureId() const
{
    if (m_texture == 0) {
        glGenTextures(1, &m_texture);

        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

        GLint previousProgram;
        GLint previousTexture;

        preprocess(gl, &previousProgram, &previousTexture);
        postprocess(gl, previousProgram, previousTexture);
    }

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

void GrallocTexture::preprocess(QOpenGLFunctions* gl, GLint* tex, GLint* prog) const
{
    if (m_usesShader) {
        gl->glGetIntegerv(GL_CURRENT_PROGRAM, prog);
        gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, tex);

        gl->glUseProgram(m_shaderCode.program);

        GLint position = gl->glGetAttribLocation(m_shaderCode.program, "position");
        GLint tex = gl->glGetUniformLocation(m_shaderCode.program, "tex");

        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, m_texture);

        gl->glUniform1i(tex, 0);
        gl->glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, 0);
        gl->glEnableVertexAttribArray(position);
    }
}

void GrallocTexture::postprocess(QOpenGLFunctions* gl, GLint tex, GLint prog) const
{
    if (m_usesShader) {
        gl->glBindTexture(GL_TEXTURE_2D, tex);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glUseProgram(prog);
    }
}

void GrallocTexture::bind()
{
    static bool bound = (m_texture != 0);
    updateBindOptions(!bound);
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);
}