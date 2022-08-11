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

#ifndef GRALLOCTEXTURE_H
#define GRALLOCTEXTURE_H

#include <QObject>
#include <QImage>
#include <QSize>
#include <QSGTexture>
#include <QSGDynamicTexture>
#include <QDebug>
#include <QMutex>
#include <QMatrix4x4>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

#include <memory>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <hybris/ui/ui_compatibility_layer.h>
#include <hybris/gralloc/gralloc.h>
#include <hardware/gralloc.h>

#undef Bool

enum ColorShader {
    ColorShader_Unknown = -1,
    ColorShader_Passthrough,
    ColorShader_FlipColorChannels,
    ColorShader_FlipColorChannelsWithAlpha,

    ColorShader_First = ColorShader_Passthrough,
    ColorShader_Last = ColorShader_FlipColorChannelsWithAlpha,
    ColorShader_Count =  + ColorShader_Last + 1
};

static const GLchar* COLOR_CONVERSION_VERTEX = {
    "#version 100\n"
    "attribute highp vec3 vertexCoord;\n"
    "attribute highp vec2 textureCoord;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    uv = textureCoord.xy;\n"
    "    gl_Position = vec4(vertexCoord,1.0);\n"
    "}\n"
};

static const GLchar* PASSTHROUGH_SHADER = {
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "uniform samplerExternalOES tex;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor.argb = texture2D(tex, uv).argb;\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_SHADER = {
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "uniform samplerExternalOES tex;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor.rgb = texture2D(tex, uv).bgr;\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_WITH_ALPHA_SHADER = {
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "uniform samplerExternalOES tex;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor.argb = texture2D(tex, uv).bgra;\n"
    "}\n"
};

struct ShaderBundle {
    std::shared_ptr<QOpenGLShaderProgram> program;
    std::shared_ptr<QMutex> mutex;
};

typedef std::map<ColorShader, ShaderBundle> ShaderCache;

class GrallocTexture;
class GrallocTextureCreator
{
public:
    static GrallocTexture* createTexture(const QImage& image, ShaderCache& cachedShaders);

private:
    static uint32_t convertUsage(const QImage& image);
    static uint32_t convertLockUsage(const QImage& image);
    static int convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader);
};

class GrallocTexture : public QSGTexture
{
    Q_OBJECT

public:
    GrallocTexture();

    int textureId() const override;
    QSize textureSize() const override;
    bool hasAlphaChannel() const override;
    bool hasMipmaps() const override;
    void bind() override;
    bool updateTexture() const;

private:
    GrallocTexture(struct graphic_buffer* handle, const QSize& size, const bool& hasAlphaChannel, ShaderBundle conversionShader);
    ~GrallocTexture();

    void initializeEgl(struct graphic_buffer* handle);

    void renderShader(QOpenGLFunctions* gl) const;

    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

    EGLImageKHR mutable m_image;
    GLuint mutable m_texture;
    QSize m_size;
    bool m_hasAlphaChannel;
    ShaderBundle m_shaderCode;
    bool mutable m_bound;
    bool mutable m_valid;

    friend class GrallocTextureCreator;
};

#endif
