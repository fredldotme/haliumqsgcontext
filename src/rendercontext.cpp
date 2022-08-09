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
#include <QMutex>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

#include <dlfcn.h>
#include <hybris/common/dlfcn.h>

#include <sys/syscall.h>

RenderContext::RenderContext(QSGContext* context) : QSGDefaultRenderContext(context),
    m_logging(false)
{

}

void RenderContext::messageReceived(const QOpenGLDebugMessage &debugMessage)
{
    qWarning() << "OpenGL log:" << debugMessage.message();
}

bool RenderContext::init() const
{
    if (qEnvironmentVariableIsSet("LOMIRI_CONTEXT_OPENGL_LOG")) {
        m_logging = true;
        connect(&m_glLogger, &QOpenGLDebugLogger::messageLogged, this, &RenderContext::messageReceived);

        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        m_glLogger.initialize();
        m_glLogger.startLogging();
    }

    // Attempt to get FIFO scheduling from mechanicd
    {
        const int threadId = syscall(SYS_gettid);
        const QString connName = QStringLiteral("haliumqsgcontext");

        QDBusConnection shortConnection = QDBusConnection::connectToBus(QDBusConnection::SystemBus, connName);
        QDBusInterface interface(QStringLiteral("org.halium.mechanicd"),
                                 QStringLiteral("/org/halium/mechanicd/Scheduling"),
                                 QStringLiteral("org.halium.mechanicd.Scheduling"),
                                 shortConnection);

        QDBusReply<void> reply = interface.call(QStringLiteral("requestSchedulingChange"), threadId);
        if (!reply.isValid()) {
            qDebug() << "Failed to acquire realtime scheduling on thread" << threadId << reply.error().message();
        }
        QDBusConnection::disconnectFromBus(connName);
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

        hybris_dlclose(handle);
        qDebug() << "Using libui_compat_layer for textures";
    }

    return compileColorShaders();
}

QSGTexture* RenderContext::createTexture(const QImage &image, uint flags) const
{
    QSGTexture* texture = nullptr;

    static bool colorShadersBuilt = init();

    if (!colorShadersBuilt)
        goto default_method;

    if (image.width() * image.height() > m_maxTextureSize)
        goto default_method;

    texture = GrallocTextureCreator::createTexture(image, m_cachedShaders);
    if (texture) {
        // Render the color-corrected texture now if this thread has the GL context current,
        // let QSGTexture::textureId handle it otherwise.
        if (QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->thread() == this->thread()) {
            GrallocTexture* grallocTexture = static_cast<GrallocTexture*>(texture);
            if (grallocTexture)
                grallocTexture->updateTexture();
        }
        return texture;
    }

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

    for (int i = (int)ColorShader::ColorShader_First; i < ColorShader::ColorShader_Count; i++) {
        auto program = std::make_shared<QOpenGLShaderProgram>();
        auto mutex = std::make_shared<QMutex>();
        bool success = false;

        success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, COLOR_CONVERSION_VERTEX);

        if (!success) {
            qWarning() << "Failed to compile vertex shader hence using defaults. Reason:";
            qWarning() << program->log();
            return false;
        }

        switch (i) {
        case ColorShader_ArgbToRgba:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, ARGB32_TO_RGBA8888);
            break;
        case ColorShader_FixupRgb32:
            success = program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, FIXUP_RGB32);
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

        ShaderBundle bundle{program, mutex};

        m_cachedShaders[(ColorShader)i] = bundle;
    }
    return true;
}
