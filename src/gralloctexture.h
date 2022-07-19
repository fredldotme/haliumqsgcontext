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
    ColorShader_None = -1,
    ColorShader_ArgbToRgba,
    ColorShader_FixupRgb32,

    ColorShader_First = ColorShader_ArgbToRgba,
    ColorShader_Last = ColorShader_FixupRgb32,
    ColorShader_Count =  + ColorShader_Last + 1
};

enum AlphaBehavior {
    AlphaBehavior_None = 0,
    AlphaBehavior_Premultiply,
    AlphaBehavior_Repremultiply
};

static const GLchar* COLOR_CONVERSION_VERTEX = {
    "#version 100\n"
    "attribute vec4 vertexCoord;\n"
    "attribute vec2 textureCoord;\n"
    "varying vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    uv = vec3(textureCoord, 1.0).xy;\n"
    "    gl_Position = vertexCoord;\n"
    "}\n"
};

static const GLchar* ARGB32_TO_RGBA8888 = {
    "#version 100\n"
    "uniform sampler2D tex;\n"
    "varying vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor.rgba = texture2D(tex, uv).bgra;\n"
    "}\n"
};

static const GLchar* FIXUP_RGB32 = {
    "#version 100\n"
    "uniform sampler2D tex;\n"
    "varying vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor.rgb = texture2D(tex, uv).rgb;\n"
    "}\n"
};

struct ShaderBundle {
    std::shared_ptr<QOpenGLShaderProgram> program;
    int samLocation;
    int texLocation;
    int posLocation;
};

typedef std::map<ColorShader, ShaderBundle> ShaderCache;

class GrallocTexture;
class GrallocTextureCreator
{
public:
    static QSGTexture* createTexture(const QImage& image, ShaderCache& cachedShaders);

private:
    static uint32_t convertUsage(const QImage& image);
    static uint32_t convertLockUsage(const QImage& image);
    static int convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader, AlphaBehavior& premultiply);
};

class GrallocTexture : public QSGDynamicTexture
{
    Q_OBJECT

public:
    GrallocTexture();

    int textureId() const override;
    QSize textureSize() const override;
    bool hasAlphaChannel() const override;
    bool hasMipmaps() const override;
    void bind() override;
    bool updateTexture() override;

private:
    GrallocTexture(struct graphic_buffer* handle, const QSize& size, const bool& hasAlphaChannel, ShaderBundle conversionShader);
    ~GrallocTexture();

    void renderShader(QOpenGLFunctions* gl) const;

    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

    struct graphic_buffer* m_handle;
    EGLImageKHR m_image;
    GLuint mutable m_texture;
    QSize m_size;
    bool m_hasAlphaChannel;
    const bool m_usesShader;
    ShaderBundle m_shaderCode;
    bool mutable m_drawn, m_bound;

    friend class GrallocTextureCreator;
};

#endif
