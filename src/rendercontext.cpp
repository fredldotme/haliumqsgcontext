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

#include "rendercontext.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>
#include <QtMath>
#include <QMutex>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

#include <dlfcn.h>
#include <hybris/common/dlfcn.h>

RenderContext::RenderContext(QSGContext* context) : QSGDefaultRenderContext(context),
    m_logging(false), m_quirks(RenderContext::NoQuirk), m_libuiFound(false)
{

}

void RenderContext::messageReceived(const QOpenGLDebugMessage &debugMessage)
{
    qWarning() << "OpenGL log:" << debugMessage.message();
}

ShaderCache RenderContext::colorCorrectionShaders()
{
    return m_cachedShaders;
}

bool RenderContext::init() const
{
    if (qEnvironmentVariableIsSet("HALIUMQSG_OPENGL_LOG")) {
        m_logging = true;
        connect(&m_glLogger, &QOpenGLDebugLogger::messageLogged, this, &RenderContext::messageReceived);

        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        m_glLogger.initialize();
        m_glLogger.startLogging(QOpenGLDebugLogger::SynchronousLogging);
    }

#if 0
    // Attempt to get FIFO scheduling from mechanicd
    {
        const QString connName = QStringLiteral("haliumqsgcontext");

        QDBusConnection shortConnection = QDBusConnection::connectToBus(QDBusConnection::SystemBus, connName);
        QDBusInterface interface(QStringLiteral("org.halium.mechanicd"),
                                 QStringLiteral("/org/halium/mechanicd/Scheduling"),
                                 QStringLiteral("org.halium.mechanicd.Scheduling"),
                                 shortConnection);

        QDBusReply<void> reply = interface.call(QStringLiteral("requestSchedulingChange"));
        if (!reply.isValid()) {
            qDebug() << "Failed to acquire realtime scheduling on render thread" << reply.error().message();
        }
        QDBusConnection::disconnectFromBus(connName);
    }
#endif

    // Check for worrysome GPU vendors
    if (!qEnvironmentVariableIsSet("HALIUMQSG_NO_QUIRKS")) {
        static const std::map<std::string, RenderContext::Quirks> gpuQuirks = {
            {"Imagination Technologies", RenderContext::DisableConversionShaders}
        };

        const char* graphicsVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        if (graphicsVendor && gpuQuirks.find(std::string(graphicsVendor)) != gpuQuirks.end()) {
            m_quirks = gpuQuirks.find(std::string(graphicsVendor))->second;
            if (m_logging)
                qWarning() << "Worrysome GPU vendor detected, quirks:" << m_quirks;
        }
    }

    // Check whether the prerequisite library can be dlopened
    {
#ifdef __LP64__
        const char* ldpath = "/system/lib64/libui_compat_layer.so";
#else
        const char* ldpath = "/system/lib/libui_compat_layer.so";
#endif
        void* handle = hybris_dlopen(ldpath, RTLD_LAZY);
        if (!handle)
            return false;
        else
            m_libuiFound = true;

        hybris_dlclose(handle);
    }

    // When conversion shaders are disabled the application might still use EGLImage or the default
    if (m_quirks & RenderContext::DisableConversionShaders)
        return false;

    return compileColorShaders();
}

QSGTexture* RenderContext::createTexture(const QImage &image, uint flags) const
{
    QSGTexture* texture = nullptr;

    static const bool colorShadersBuilt = init();
    static const bool eglImageOnly = (m_quirks & RenderContext::DisableConversionShaders);

    // We don't support texture atlases, so defer to Qt's internal implementation
    if (flags & QSGRenderContext::CreateTexture_Atlas)
        goto default_method;

    // Same for mipmaps, use Qt's implementation
    if (flags & QSGRenderContext::CreateTexture_Mipmap)
        goto default_method;

    // We would have to downscale the image before upload, wasting CPU cycles.
    // In this case the Qt-internal implementation should be sufficient.
    if (image.width() > m_maxTextureSize || image.height() > m_maxTextureSize)
        goto default_method;

    if (!m_libuiFound)
        goto default_method;

    if (eglImageOnly)
        goto default_method;

    if (!colorShadersBuilt)
        goto default_method;

gralloc_method:
    texture = GrallocTextureCreator::createTexture(image, m_cachedShaders);
    if (texture)
        return texture;

default_method:
    return QSGDefaultRenderContext::createTexture(image, flags);
}

bool RenderContext::compileColorShaders() const
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

    // Store the texture geometry limit to decide later on whether to use Gralloc or not
    gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);

    if (m_logging)
        qDebug() << "Max texture size:" << m_maxTextureSize;

    ShaderBundle emptyBundle{nullptr, nullptr};
    m_cachedShaders[ColorShader_None] = emptyBundle;

    for (int i = (int)ColorShader::ColorShader_First; i < ColorShader::ColorShader_Count; i++) {
        auto program = std::make_shared<QOpenGLShaderProgram>();
        auto mutex = std::make_shared<QMutex>();
        bool success = false;

        if (m_logging)
            qDebug() << "Compiling shader" << i;

        success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, COLOR_CONVERSION_VERTEX);

        if (!success) {
            qWarning() << "Failed to compile vertex shader hence using defaults. Reason:";
            qWarning() << program->log();
            return false;
        }

        switch (i) {
        case ColorShader_Passthrough:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, PASSTHROUGH_SHADER);
            break;
        case ColorShader_FlipColorChannels:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, FLIP_COLOR_CHANNELS_SHADER);
            break;
        case ColorShader_FlipColorChannelsWithAlpha:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, FLIP_COLOR_CHANNELS_WITH_ALPHA_SHADER);
            break;
        case ColorShader_RGB32ToRGBX8888:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, RGB32_TO_RGBA8888_SHADER);
            break;
        case ColorShader_RedAndBlueSwap:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, RED_AND_BLUE_SWAP_SHADER);
            break;
        default:
            qWarning() << "No color shader type" << i;
            break;
        }

        if (!success) {
            qWarning() << "Failed to compile fragment shader" << i << "hence using defaults. Reason:";
            qWarning() << program->log();
            return false;
        }

        success = program->link();
        if (!success) {
            qWarning() << "Failed to link shader" << i << "hence using defaults. Reason:";
            qWarning() << program->log();
            return false;
        }

        ShaderBundle bundle{
            program,
            mutex,
            program->attributeLocation("vertexCoord"),
            program->attributeLocation("textureCoord"),
            program->uniformLocation("tex")
        };
        m_cachedShaders[(ColorShader)i] = bundle;

        if (m_logging) {
            qDebug() << "Shader" << i << "compiled:" << program->programId() << bundle.vertexCoord << bundle.textureCoord << bundle.texture;
            qDebug() << "Shader log:" << program->log();
        }
    }

    if (m_logging)
        qDebug() << "Using libui_compat_layer & shaders for Qt textures";

    return true;
}
