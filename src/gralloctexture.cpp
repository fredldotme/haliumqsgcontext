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
#include <QOpenGLTexture>
#include <QMutexLocker>

#include <exception>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

extern "C" {
void hybris_ui_initialize();
}

uint32_t GrallocTextureCreator::convertUsage(const QImage& image)
{
    return GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_READ_RARELY | GRALLOC_USAGE_HW_TEXTURE;
}

uint32_t GrallocTextureCreator::convertLockUsage(const QImage& image)
{
    return GRALLOC_USAGE_SW_WRITE_RARELY;
}

int GrallocTextureCreator::convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader, AlphaBehavior& alphaBehavior)
{
    //qInfo() << "format:" << image;

    switch (image.format()) {
    case QImage::Format_Mono:
        break;
    case QImage::Format_MonoLSB:
        break;
    case QImage::Format_Indexed8:
        break;
    case QImage::Format_RGB32:
        numChannels = 4;
        return HAL_PIXEL_FORMAT_BGRA_8888;
    case QImage::Format_ARGB32:
        //conversionShader = ColorShader_ArgbToRgba;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_BGRA_8888;
    case QImage::Format_ARGB32_Premultiplied:
        //conversionShader = ColorShader_ArgbToRgba;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_BGRA_8888;
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

GrallocTexture* GrallocTextureCreator::createTexture(const QImage& image, ShaderCache& cachedShaders)
{
    int numChannels = 0;
    ColorShader conversionShader = ColorShader_Passthrough;
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
        const int grallocBytesPerLine = stride * numChannels;
        const int copyBytesPerLine = qMin(bytesPerLine, grallocBytesPerLine);
        const uchar* sourceAddr = image.constBits();
        char* data = (char*)vmemAddr;

        if (bytesPerLine != grallocBytesPerLine) {
            qDebug() << "1" << bytesPerLine << grallocBytesPerLine << "(" << stride << numChannels << ")";
            for (int i = 0; i < height; i++) {
                void* dst = (vmemAddr + (grallocBytesPerLine * i));
                const void* src = image.constScanLine(i);
                memcpy(dst, src, copyBytesPerLine);
            }
        } else {
            qDebug() << "2";
            memcpy(vmemAddr, (const void*)sourceAddr, image.sizeInBytes());
        }
        graphic_buffer_unlock(handle);

        const QSize imageSize = image.size();
        const bool hasAlphaChannel = image.hasAlphaChannel();
        try {
            texture = new GrallocTexture(handle, imageSize, hasAlphaChannel, cachedShaders[conversionShader]);
        } catch (const std::exception& ex) {
            texture = nullptr;
        }
    } else {
        graphic_buffer_unlock(handle);
    }

    graphic_buffer_free(handle);
    vmemAddr = nullptr;
    handle = nullptr;
    return texture;
}

GrallocTexture::GrallocTexture(struct graphic_buffer* handle, const QSize& size, const bool& hasAlphaChannel, ShaderBundle conversionShader) :
    QSGTexture(), m_image(EGL_NO_IMAGE_KHR), m_size(size), m_texture(0),
    m_hasAlphaChannel(hasAlphaChannel), m_shaderCode(conversionShader), m_drawn(false), m_bound(false)
{
    initializeEgl(handle);
}

void GrallocTexture::initializeEgl(struct graphic_buffer* handle)
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

        void* native_buffer = graphic_buffer_get_native_buffer(handle);
        m_image = eglCreateImageKHR(dpy, context, EGL_NATIVE_BUFFER_ANDROID, native_buffer, attrs);
    }
}

GrallocTexture::GrallocTexture() : m_drawn(false)
{
}

GrallocTexture::~GrallocTexture()
{
    if (m_texture > 0) {
        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        gl->glDeleteTextures(1, &m_texture);
    }
    if (m_image != EGL_NO_IMAGE_KHR) {
        EGLDisplay dpy = eglGetCurrentDisplay();
        eglDestroyImageKHR(dpy, m_image);
        m_image = EGL_NO_IMAGE_KHR;
    }
}

int GrallocTexture::textureId() const
{
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
    const auto width = m_size.width();
    const auto height = m_size.height();
    const int textureUnit = 1;
    GLuint tmpTexture;

    static const GLfloat vertex_buffer_data[] = {
        -1,-1, 0,
        -1, 1, 0,
         1,-1, 0,
        -1, 1, 0,
         1,-1, 0,
         1, 1, 0
    };

    static const GLfloat texture_buffer_data[] = {
        0, 0,
        0, 1,
        1, 0,
        0, 1,
        1, 0,
        1, 1
    };

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    format.setSamples(0);
    format.setMipmap(false);
    QOpenGLFramebufferObject fbo(width, height, format);
    if (!fbo.isValid()) {
        qWarning() << "Failed to set up FBO";
        return;
    }

    fbo.bind();

    gl->glClearColor(0, 0, 0, 0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl->glViewport(0, 0, width, height);

    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vertexBuffer;
    QOpenGLBuffer textureBuffer;

    QMutexLocker lock(m_shaderCode.mutex.get());
    m_shaderCode.program->bind();

    gl->glActiveTexture(GL_TEXTURE0 + textureUnit);
    gl->glGenTextures(1, &tmpTexture);
    gl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, tmpTexture);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl->glViewport(0, 0, width, height);

    // "Dump" the EGLImage onto the texture
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, m_image);

    vao.create();
    vao.bind();

    vertexBuffer.create();
    vertexBuffer.bind();
    vertexBuffer.allocate(vertex_buffer_data, sizeof(vertex_buffer_data));
    m_shaderCode.program->setAttributeBuffer("vertexCoord", GL_FLOAT, 0, 3, 0);
    m_shaderCode.program->enableAttributeArray("vertexCoord");
    vertexBuffer.release();

    textureBuffer.create();
    textureBuffer.bind();
    textureBuffer.allocate(texture_buffer_data, sizeof(texture_buffer_data));
    m_shaderCode.program->setAttributeBuffer("textureCoord", GL_FLOAT, 0, 2, 0);
    m_shaderCode.program->enableAttributeArray("textureCoord");
    textureBuffer.release();

    m_shaderCode.program->setUniformValue("tex", textureUnit);

    gl->glDrawArrays(GL_TRIANGLES, 0, 6);

    m_shaderCode.program->disableAttributeArray("textureCoord");
    m_shaderCode.program->disableAttributeArray("vertexCoord");
    vertexBuffer.destroy();
    textureBuffer.destroy();

    vao.release();

    m_shaderCode.program->release();

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glDeleteTextures(1, &tmpTexture);

    m_texture = fbo.takeTexture();
}

bool GrallocTexture::updateTexture() const
{
    if (m_texture == 0) {
        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        renderShader(gl);
        return true;
    }
    return false;
}

void GrallocTexture::bind()
{
    m_bound = true;

    // In case the texture was not already created, due to being created in a non-current thread
    // then catch up and do so now right before the texture is being asked for.
    updateTexture();

    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
}
