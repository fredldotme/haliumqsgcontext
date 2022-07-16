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

RenderContext::RenderContext(QSGContext* context) : QSGDefaultRenderContext(context)
{
	
}

void RenderContext::messageReceived(const QOpenGLDebugMessage &debugMessage)
{
    qWarning() << "OpenGL log:" << debugMessage.message();
}

bool RenderContext::init() const
{
    connect(&m_glLogger, &QOpenGLDebugLogger::messageLogged, this, &RenderContext::messageReceived);

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    m_glLogger.initialize();
    m_glLogger.startLogging();

    return compileColorShaders();
}

QSGTexture* RenderContext::createTexture(const QImage &image, uint flags) const
{
    QSGTexture* texture = nullptr;

    static bool colorShadersBuilt = init();

    if (!colorShadersBuilt)
        goto default_method;

    texture = GrallocTextureCreator::createTexture(image, m_cachedShaders);
    if (texture)
        return texture;

default_method:
    return QSGDefaultRenderContext::createTexture(image, flags);
}

bool RenderContext::compileColorShaders() const
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();

    for (int i = (int)ColorShader::ColorShader_First; i < ColorShader::ColorShader_Count; i++) {
        GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &COLOR_CONVERSION_VERTEX, nullptr);
        glCompileShader(vertex);

        GLint compiledShader;
        glGetShaderiv(vertex, GL_COMPILE_STATUS, &compiledShader);
        if (compiledShader != GL_TRUE) {
            qWarning() << "Failed to compile vertex shader" << i << "hence using defaults.";
            return false;
        }

        GLuint shader = gl->glCreateShader(GL_FRAGMENT_SHADER);
        switch(i) {
        case ColorShader_ArgbToRgba:
            gl->glShaderSource(shader, 1, &ARGB32_TO_RGBA8888, nullptr);
            break;
        case ColorShader_FixupRgb32:
            gl->glShaderSource(shader, 1, &FIXUP_RGB32, nullptr);
            break;
        default:
            qWarning() << "No color shader type" << i;
            break;
        }

        gl->glCompileShader(shader);

        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiledShader);
        if (compiledShader != GL_TRUE) {
            qWarning() << "Failed to compile fragment shader" << i << "hence using defaults.";
            return false;
        }

        GLuint program = gl->glCreateProgram();

        gl->glAttachShader(program, vertex);
        gl->glAttachShader(program, shader);
        gl->glLinkProgram(program);

        GLint linkedProgram;
        gl->glGetProgramiv(program, GL_LINK_STATUS, &linkedProgram);
        if (linkedProgram != GL_TRUE) {
            qWarning() << "Failed to link shader" << i << "hence using defaults.";
            return false;
        }

        m_cachedShaders[(ColorShader)i] = ShaderBundle{vertex, shader, program};
    }
    return true;
}