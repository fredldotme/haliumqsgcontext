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

#include <QAbstractEventDispatcher>
#include <QOpenGLFramebufferObject>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QMutexLocker>
#include <QThreadPool>

#include <exception>
#include <thread>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

extern "C" {
void hybris_ui_initialize();
}

static EglImageFunctions eglImageFunctions;
static QThreadPool* uploadThreadPool = new QThreadPool();

class TextureUploadTask : public QRunnable
{
public:
    TextureUploadTask(std::function<void()> func) : m_func(func) {}
    void run() override
    {
        m_func();
    }

private:
    std::function<void()> m_func;
};
EglImageFunctions::EglImageFunctions()
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
}

uint32_t GrallocTextureCreator::convertUsage(const QImage& image)
{
    return GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_NEVER | GRALLOC_USAGE_HW_TEXTURE;
}

uint32_t GrallocTextureCreator::convertLockUsage(const QImage& image)
{
    return GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_HW_TEXTURE;
}

int GrallocTextureCreator::convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader)
{
    qInfo() << "format:" << image;

    switch (image.format()) {
    case QImage::Format_Mono:
        break;
    case QImage::Format_MonoLSB:
        break;
    case QImage::Format_Indexed8:
        break;
    case QImage::Format_RGB32:
        conversionShader = ColorShader_None;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_BGRA_8888;
    case QImage::Format_ARGB32:
        conversionShader = ColorShader_None;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_BGRA_8888;
    case QImage::Format_ARGB32_Premultiplied:
        conversionShader = ColorShader_None;
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
        conversionShader = ColorShader_FlipColorChannels;
        numChannels = 3;
        return HAL_PIXEL_FORMAT_RGB_888;
    case QImage::Format_RGB444:
        break;
    case QImage::Format_ARGB4444_Premultiplied:
        break;
    case QImage::Format_RGBX8888:
        conversionShader = ColorShader_FlipColorChannels;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBX_8888;
    case QImage::Format_RGBA8888:
        conversionShader = ColorShader_FlipColorChannelsWithAlpha;
        numChannels = 4;
        return HAL_PIXEL_FORMAT_RGBA_8888;
    case QImage::Format_RGBA8888_Premultiplied:
        conversionShader = ColorShader_FlipColorChannelsWithAlpha;
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

GrallocTexture* GrallocTextureCreator::createTexture(const QImage& image, ShaderCache& cachedShaders, const int& maxTextureSize)
{
    int numChannels = 0;
    ColorShader conversionShader = ColorShader_None;

    const int format = convertFormat(image, numChannels, conversionShader);
    if (format < 0) {
        qDebug() << "Unknown color format" << image.format();
        return nullptr;
    }

    GrallocTexture* texture = nullptr;

    {
        ShaderBundle shaderBundle;
        if (cachedShaders.find(conversionShader) != cachedShaders.end())
            shaderBundle = cachedShaders[conversionShader];

        try {
            texture = new GrallocTexture(image.hasAlphaChannel(), shaderBundle, eglImageFunctions);
            if (texture) {
                QSize size = image.size();
                float scaleFactor = 1.0;
                if (size.width() > maxTextureSize)
                    scaleFactor = (float)maxTextureSize / (float)size.width();
                if (size.height() > maxTextureSize)
                    scaleFactor = (float)maxTextureSize / (float)size.height();

                size = QSize(size.width() * scaleFactor, size.height() * scaleFactor);
                texture->provideSizeInfo(size);

                auto uploadFunc = [=]() {
                    const QImage toUpload = (size != image.size()) ? image.transformed(QTransform::fromScale(scaleFactor, scaleFactor)) : image;
                    texture->provideSizeInfo(toUpload.size());

                    const uint32_t usage = convertUsage(image);
                    struct graphic_buffer* handle = graphic_buffer_new_sized(toUpload.width(), toUpload.height(), format, usage);
                    if (!handle) {
                        qWarning() << "No buffer allocated";
                        texture->createEglImage(handle, 0);
                        return;
                    }

                    const int stride = graphic_buffer_get_stride(handle);
                    const int lockUsage = convertLockUsage(toUpload);
                    const int bytesPerLine = toUpload.bytesPerLine();
                    const int grallocBytesPerLine = stride * numChannels;
                    const int copyBytesPerLine = qMin(bytesPerLine, grallocBytesPerLine);  
                    const int textureSize = (bytesPerLine != grallocBytesPerLine) ?
                        copyBytesPerLine * toUpload.height() :
                        toUpload.sizeInBytes();

                    const uchar* sourceAddr = toUpload.constBits();
                    void* vmemAddr = nullptr;
                    graphic_buffer_lock(handle, lockUsage, &vmemAddr);

                    if (bytesPerLine != grallocBytesPerLine) {
                        for (int i = 0; i < toUpload.height(); i++) {
                            void* dst = (vmemAddr + (grallocBytesPerLine * i));
                            const void* src = toUpload.constScanLine(i);
                            memcpy(dst, src, copyBytesPerLine);
                        }
                    } else {
                        memcpy(vmemAddr, (const void*)sourceAddr, toUpload.sizeInBytes());
                    }

                    graphic_buffer_unlock(handle);
                    texture->createEglImage(handle, textureSize);
                };
                uploadThreadPool->setMaxThreadCount(16);
                uploadThreadPool->start(new TextureUploadTask(uploadFunc));
            }
        } catch (const std::exception& ex) {
            texture = nullptr;
        }
    }

    return texture;
}

GrallocTexture::GrallocTexture(const bool& hasAlphaChannel,
                               ShaderBundle conversionShader, EglImageFunctions eglImageFunctions) :
    QSGTexture(), m_buffer(nullptr), m_image(EGL_NO_IMAGE_KHR), m_texture(0), m_textureSize(0),
    m_hasAlphaChannel(hasAlphaChannel), m_shaderCode(conversionShader), m_bound(false), m_valid(true),
    m_rendered(false), m_uploadInProgress(true), m_eglImageFunctions(eglImageFunctions)
{
}

GrallocTexture::GrallocTexture() : m_valid(false)
{
}

GrallocTexture::~GrallocTexture()
{
    awaitUpload();

    if (m_texture > 0) {
        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        gl->glDeleteTextures(1, &m_texture);
    }
    if (m_image != EGL_NO_IMAGE_KHR) {
        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        m_eglImageFunctions.eglDestroyImageKHR(dpy, m_image);
        m_image = EGL_NO_IMAGE_KHR;
    }
    if (m_buffer) {
        graphic_buffer_free(m_buffer);
        m_buffer = nullptr;
    }
}

int GrallocTexture::textureId() const
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    ensureEmptyTexture(gl);
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

void* GrallocTexture::buffer() const
{
    return m_buffer;
}

int GrallocTexture::textureByteCount() const
{
    return m_textureSize;
}

void GrallocTexture::provideSizeInfo(const QSize& size)
{
    {
        std::lock_guard<std::mutex> lk(m_infoMutex);
        m_size = size;
    }
    m_infoCondition.notify_all();
}

void GrallocTexture::createEglImage(struct graphic_buffer* handle, const int textureSize) const
{
    {
        std::lock_guard<std::mutex> lk(m_uploadMutex);

        m_buffer = handle;
        m_textureSize = textureSize;

        if (m_image == EGL_NO_IMAGE_KHR && m_buffer) {
            EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            EGLContext context = EGL_NO_CONTEXT;
            EGLint attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };

            void* native_buffer = graphic_buffer_get_native_buffer(m_buffer);
            m_image = m_eglImageFunctions.eglCreateImageKHR(dpy, context, EGL_NATIVE_BUFFER_ANDROID, native_buffer, attrs);
        }

        m_uploadInProgress = false;
    }
    m_uploadCondition.notify_all();
}

void GrallocTexture::ensureEmptyTexture(QOpenGLFunctions* gl) const
{
    if (m_texture == 0) {
        gl->glGenTextures(1, &m_texture);
        if (m_shaderCode.program != 0) {
            gl->glBindTexture(GL_TEXTURE_2D, m_texture);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_size.width(), m_size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl->glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
}

void GrallocTexture::renderShader(QOpenGLFunctions* gl) const
{
    const auto& size = m_size;
    const auto width = size.width();
    const auto height = size.height();
    const int textureUnit = 1;
    GLint prevProgram = 0;
    GLint prevFbo = 0;
    GLint prevTexture = 0;
    GLint prevActiveTexture = 0;
    GLint prevArrayBuf = 0;
    GLint viewport[4];
    GLuint tmpTexture = 0;

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

    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
    gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    gl->glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    gl->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    gl->glGetIntegerv(GL_VIEWPORT, viewport);

    gl->glBindTexture(GL_TEXTURE_2D, m_texture);

    GLuint fbo;
    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    if (m_hasAlphaChannel) {
        gl->glClearColor(0.0, 0.0, 0.0, 0.0);
        gl->glClear(GL_COLOR_BUFFER_BIT);
    }

    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vertexBuffer;
    QOpenGLBuffer textureBuffer;

    vao.create();
    vao.bind();
            
    vertexBuffer.create();
    vertexBuffer.bind();
    vertexBuffer.allocate(vertex_buffer_data, sizeof(vertex_buffer_data));
    gl->glEnableVertexAttribArray(m_shaderCode.vertexCoord);
    gl->glVertexAttribPointer(m_shaderCode.vertexCoord, 3, GL_FLOAT, GL_TRUE, 0, 0);
    vertexBuffer.release();
        
    textureBuffer.create();
    textureBuffer.bind();
    textureBuffer.allocate(texture_buffer_data, sizeof(texture_buffer_data));
    gl->glEnableVertexAttribArray(m_shaderCode.textureCoord);
    gl->glVertexAttribPointer(m_shaderCode.textureCoord, 2, GL_FLOAT, GL_TRUE, 0, 0);
    textureBuffer.release();

    QMutexLocker lock(m_shaderCode.mutex.get());
    gl->glLinkProgram(m_shaderCode.program);
    gl->glUseProgram(m_shaderCode.program);
    qDebug() << "Using shader:" << m_shaderCode.program << gl->glGetError();

    const auto newUnit = (prevActiveTexture - GL_TEXTURE0) + textureUnit;
    qDebug() << "newUnit:" << newUnit;
    gl->glUniform1i(m_shaderCode.texture, newUnit);

    // "Dump" the EGLImage onto the temporary texture and draw it through the shader
    gl->glGenTextures(1, &tmpTexture);
    gl->glActiveTexture(GL_TEXTURE0 + newUnit);
    gl->glBindTexture(GL_TEXTURE_2D, tmpTexture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_eglImageFunctions.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);
    gl->glViewport(0, 0, width, height);
    gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    gl->glDisableVertexAttribArray(m_shaderCode.textureCoord);
    gl->glDisableVertexAttribArray(m_shaderCode.vertexCoord);

    gl->glUseProgram(prevProgram);
    gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    gl->glActiveTexture(prevActiveTexture);
    gl->glBindTexture(GL_TEXTURE_2D, prevTexture);
    gl->glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuf);
    gl->glDeleteTextures(1, &tmpTexture);
    gl->glDeleteFramebuffers(1, &fbo);
}

void GrallocTexture::bindImageOnly(QOpenGLFunctions* gl) const
{
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_eglImageFunctions.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);
}

bool GrallocTexture::renderTexture() const
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

    if (m_rendered)
        return false;

    awaitUpload();

    // Special-case passthrough mode
    if (m_shaderCode.program == 0) {
        bindImageOnly(gl);
        m_rendered = true;
        return true;
    } else if (m_shaderCode.program != 0) {
        renderShader(gl);
        m_rendered = true;
        return true;
    }
    return false;
}

void GrallocTexture::bind()
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

    ensureEmptyTexture(gl);

    if (m_shaderCode.program != 0) {
        renderTexture();
        gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    } else {
        awaitUpload();
        bindImageOnly(gl);
    }
}

void GrallocTexture::awaitUpload() const
{
    std::unique_lock<std::mutex> lk(m_uploadMutex);
    m_uploadCondition.wait(lk, [=]{ return !m_uploadInProgress; });
}
