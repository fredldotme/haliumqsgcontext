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
#include <QDebug>

#include <QOpenGLContext>
#include <QOpenGLFunctions>

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

static const GLchar* COLOR_CONVERSION_VERTEX = {
    "#version 100\n"
    "attribute vec4 position;\n"
    "\n"
    "void main() {\n"
    "   gl_Position = position;\n"
    "}\n"
};

static const GLchar* ARGB32_TO_RGBA8888 = {
    "#version 100\n"
    "uniform sampler2D tex;\n"
    "\n"
    "void main() {\n"
    "    vec4 c = texture2D(tex, gl_FragCoord.xy);\n"
    "    gl_FragColor.rgba = vec4(c.a, c.r, c.g, c.b);\n"
    "}\n"
};

static const GLchar* FIXUP_RGB32 = {
    "#version 100\n"
    "uniform sampler2D tex;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor.rgb = texture2D(tex, gl_FragCoord.xy).rgb;\n"
    "}\n"
};

struct ShaderBundle {
    GLuint vertex;
    GLuint fragment;
    GLuint program;
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

private:
    GrallocTexture(struct graphic_buffer* handle, const QSize& size, const bool& hasAlphaChannel, ShaderBundle conversionShader);
    ~GrallocTexture();

    void preprocess(QOpenGLFunctions* gl, GLint* tex, GLint* prog) const;
    void postprocess(QOpenGLFunctions* gl, GLint tex, GLint prog) const;

    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

    struct graphic_buffer* m_handle;
    EGLImageKHR m_image;
    GLuint mutable m_texture;
    GLuint mutable m_fbo;
    QSize m_size;
    bool m_hasAlphaChannel;
    const bool m_usesShader;
    ShaderBundle m_shaderCode;

    friend class GrallocTextureCreator;
};

#endif
