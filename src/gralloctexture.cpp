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
        numChannels = 4;
        return HAL_PIXEL_FORMAT_BGRA_8888;
    case QImage::Format_ARGB32:
        numChannels = 4;
        return HAL_PIXEL_FORMAT_BGRA_8888;
    case QImage::Format_ARGB32_Premultiplied:
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
    ColorShader conversionShader = ColorShader_None;

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

    GrallocTexture* texture = nullptr;

    {
        const QSize imageSize = image.size();
        const bool hasAlphaChannel = image.hasAlphaChannel();
        const int stride = graphic_buffer_get_stride(handle);
        const int lockUsage = convertLockUsage(image);
        const int bytesPerLine = image.bytesPerLine();
        const int grallocBytesPerLine = stride * numChannels;
        const int copyBytesPerLine = qMin(bytesPerLine, grallocBytesPerLine);
        const int textureSize = (bytesPerLine != grallocBytesPerLine) ?
            copyBytesPerLine * height :
            image.sizeInBytes();
    
        ShaderBundle shaderBundle;
        if (cachedShaders.find(conversionShader) != cachedShaders.end())
            shaderBundle = cachedShaders[conversionShader];

        try {
            texture = new GrallocTexture(handle, imageSize, hasAlphaChannel, textureSize,
                                         shaderBundle, eglImageFunctions);
            if (texture) {
                auto uploadFunc = [=]() {
                    const uchar* sourceAddr = image.constBits();
                    void* vmemAddr = nullptr;
                    graphic_buffer_lock(handle, lockUsage, &vmemAddr);

                    if (bytesPerLine != grallocBytesPerLine) {
                        for (int i = 0; i < height; i++) {
                            void* dst = (vmemAddr + (grallocBytesPerLine * i));
                            const void* src = image.constScanLine(i);
                            memcpy(dst, src, copyBytesPerLine);
                        }
                    } else {
                        memcpy(vmemAddr, (const void*)sourceAddr, image.sizeInBytes());
                    }

                    graphic_buffer_unlock(handle);
                    texture->createEglImage();
                };
                uploadThreadPool->setMaxThreadCount(12);
                uploadThreadPool->start(new TextureUploadTask(uploadFunc));
            }
        } catch (const std::exception& ex) {
            texture = nullptr;
        }
    }

    // The graphic_buffer handle will be freed right after turning it into an EGLImage
    handle = nullptr;
    return texture;
}

GrallocTexture::GrallocTexture(struct graphic_buffer* handle, const QSize& size,
                               const bool& hasAlphaChannel, int textureSize,
                               ShaderBundle conversionShader, EglImageFunctions eglImageFunctions) :
    QSGTexture(), m_buffer(handle), m_image(EGL_NO_IMAGE_KHR), m_size(size), m_texture(0),
    m_hasAlphaChannel(hasAlphaChannel), m_shaderCode(conversionShader), m_bound(false), m_valid(true),
    m_textureSize(textureSize), m_eglImageFunctions(eglImageFunctions)
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
    if (m_texture == 0)
        renderTexture();
    return m_texture;
}

QSize GrallocTexture::textureSize() const
{
    const int width = graphic_buffer_get_width(m_buffer);
    const int height = graphic_buffer_get_height(m_buffer);
    return QSize(width, height);
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

void GrallocTexture::createEglImage() const
{
    {
        std::lock_guard<std::mutex> lk(m_uploadMutex);

        if (m_image == EGL_NO_IMAGE_KHR && m_buffer) {
            EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            EGLContext context = EGL_NO_CONTEXT;
            EGLint attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };

            void* native_buffer = graphic_buffer_get_native_buffer(m_buffer);
            m_image = m_eglImageFunctions.eglCreateImageKHR(dpy, context, EGL_NATIVE_BUFFER_ANDROID, native_buffer, attrs);
        }
    }
    m_uploadCondition.notify_all();
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

    gl->glClearColor(0.0, 0.0, 0.0, 0.0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl->glViewport(0, 0, width, height);

    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vertexBuffer;
    QOpenGLBuffer textureBuffer;

    QMutexLocker lock(m_shaderCode.mutex.get());
    const bool bound = m_shaderCode.program->bind();

    gl->glActiveTexture(GL_TEXTURE0 + textureUnit);
    gl->glGenTextures(1, &tmpTexture);
    gl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, tmpTexture);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // "Dump" the EGLImage onto the texture
    m_eglImageFunctions.glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, m_image);

    vao.create();
    vao.bind();

    vertexBuffer.create();
    vertexBuffer.bind();
    vertexBuffer.allocate(vertex_buffer_data, sizeof(vertex_buffer_data));
    m_shaderCode.program->enableAttributeArray(m_shaderCode.vertexCoord);
    m_shaderCode.program->setAttributeBuffer(m_shaderCode.vertexCoord, GL_FLOAT, 0, 3, 0);

    textureBuffer.create();
    textureBuffer.bind();
    textureBuffer.allocate(texture_buffer_data, sizeof(texture_buffer_data));
    m_shaderCode.program->enableAttributeArray(m_shaderCode.textureCoord);
    m_shaderCode.program->setAttributeBuffer(m_shaderCode.textureCoord, GL_FLOAT, 0, 2, 0);

    m_shaderCode.program->setUniformValue(m_shaderCode.texture, textureUnit);

    gl->glDrawArrays(GL_TRIANGLES, 0, 6);

    m_shaderCode.program->disableAttributeArray(m_shaderCode.textureCoord);
    m_shaderCode.program->disableAttributeArray(m_shaderCode.vertexCoord);
    vertexBuffer.release();
    vertexBuffer.destroy();
    textureBuffer.release();
    textureBuffer.destroy();
    vao.release();
    m_shaderCode.program->release();

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glDeleteTextures(1, &tmpTexture);

    if (m_texture != 0)
        gl->glDeleteTextures(1, &m_texture);
    m_texture = fbo.takeTexture();
}

void GrallocTexture::bindImageOnly(QOpenGLFunctions* gl) const
{
    gl->glGenTextures(1, &m_texture);
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    m_eglImageFunctions.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_image);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
}

bool GrallocTexture::renderTexture() const
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

    awaitUpload();

    // Special-case passthrough mode
    if (!m_shaderCode.program.get() && m_texture == 0) {
        bindImageOnly(gl);
        return true;
    } else if (m_shaderCode.program.get() && m_texture == 0) {
        renderShader(gl);
        return true;
    }
    return false;
}

void GrallocTexture::bind()
{
    if (m_texture == 0)
        renderTexture();

    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    gl->glBindTexture(GL_TEXTURE_2D, m_texture);
}

void GrallocTexture::awaitUpload() const
{
    std::unique_lock<std::mutex> lk(m_uploadMutex);
    m_uploadCondition.wait(lk, [=]{ return m_image != EGL_NO_IMAGE_KHR; });
}
