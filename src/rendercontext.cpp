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

RenderContext::RenderContext(QSGContext* context) : QSGDefaultRenderContext(context)
{
	
}

void RenderContext::messageReceived(const QOpenGLDebugMessage &debugMessage)
{
    qWarning() << "OpenGL log:" << debugMessage.message();
}

bool RenderContext::init() const
{
    if (qEnvironmentVariableIsSet("LOMIRI_CONTEXT_OPENGL_LOG")) {
        connect(&m_glLogger, &QOpenGLDebugLogger::messageLogged, this, &RenderContext::messageReceived);

        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        m_glLogger.initialize();
        m_glLogger.startLogging();
    }
    return compileColorShaders();
}

QSGTexture* RenderContext::createTexture(const QImage &image, uint flags) const
{
    QSGTexture* texture = nullptr;

    static bool colorShadersBuilt = init();

    if (!colorShadersBuilt || QOpenGLContext::currentContext()->thread() != this->thread())
        goto default_method;

    texture = GrallocTextureCreator::createTexture(image, m_cachedShaders);
    if (texture) {
        GrallocTexture* dynamicTexture = static_cast<GrallocTexture*>(texture);
        if (dynamicTexture)
           dynamicTexture->updateTexture();
        return texture;
    }

default_method:
    return QSGDefaultRenderContext::createTexture(image, flags);
}

bool RenderContext::compileColorShaders() const
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

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