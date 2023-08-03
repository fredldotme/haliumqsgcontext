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
#include <QMutex>
#include <QMatrix4x4>
#include <QThread>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

#include <memory>
#include <mutex>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <hybris/ui/ui_compatibility_layer.h>
#include <hybris/gralloc/gralloc.h>
#include <hardware/gralloc.h>

#undef Bool

enum ColorShader {
    ColorShader_None = 0,
    ColorShader_Passthrough,
    ColorShader_FlipColorChannels,
    ColorShader_FlipColorChannelsWithAlpha,
    ColorShader_RGB32ToRGBX8888,
    ColorShader_RedAndBlueSwap,

    ColorShader_First = ColorShader_Passthrough,
    ColorShader_Last = ColorShader_RedAndBlueSwap,
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
    "precision mediump float;\n"
    "uniform samplerExternalOES tex;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    float r = texture2D(tex, uv).r;\n"
    "    float g = texture2D(tex, uv).g;\n"
    "    float b = texture2D(tex, uv).b;\n"
    "    float a = texture2D(tex, uv).a;\n"
    "    gl_FragColor = vec4(r, g, b, a);\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_SHADER = {
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES tex;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    float r = texture2D(tex, uv).r;\n"
    "    float g = texture2D(tex, uv).g;\n"
    "    float b = texture2D(tex, uv).b;\n"
    "    float a = texture2D(tex, uv).a;\n"
    "    gl_FragColor = vec4(b, g, r, 1.0);\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_WITH_ALPHA_SHADER = {
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES tex;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    float r = texture2D(tex, uv).r;\n"
    "    float g = texture2D(tex, uv).g;\n"
    "    float b = texture2D(tex, uv).b;\n"
    "    float a = texture2D(tex, uv).a;\n"
    "    gl_FragColor = vec4(b, g, r, a);\n"
    "}\n"
};

static const GLchar* RGB32_TO_RGBA8888_SHADER = {
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES tex;\n"   
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    float r = texture2D(tex, uv).r;\n"
    "    float g = texture2D(tex, uv).g;\n"
    "    float b = texture2D(tex, uv).b;\n"
    "    float a = texture2D(tex, uv).a;\n"
    "    gl_FragColor = vec4(r, g, b, a);\n"
    "}\n"
};

static const GLchar* RED_AND_BLUE_SWAP_SHADER = {
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES tex;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    float r = texture2D(tex, uv).r;\n"
    "    float g = texture2D(tex, uv).g;\n"
    "    float b = texture2D(tex, uv).b;\n"
    "    float a = texture2D(tex, uv).a;\n" 
    "    gl_FragColor = vec4(b, g, r, a);\n"
    "}\n"
};

struct ShaderBundle {
    std::shared_ptr<QOpenGLShaderProgram> program;
    std::shared_ptr<QMutex> mutex;
    int vertexCoord = -1;
    int textureCoord = -1;
    int texture = -1;
};

typedef std::map<ColorShader, ShaderBundle> ShaderCache;

struct EglImageFunctions {
    EglImageFunctions();
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
};

class GrallocTexture;
class GrallocTextureCreator
{
public:
    static GrallocTexture* createTexture(const QImage& image, ShaderCache& cachedShaders);
    static int convertFormat(const QImage& image, int& numChannels, ColorShader& conversionShader);
    static uint32_t convertLockUsage(const QImage& image);

private:
    static uint32_t convertUsage(const QImage& image);
};

class GrallocTexture : public QSGTexture
{
    Q_OBJECT

public:
    GrallocTexture();

    virtual int textureId() const override;
    virtual QSize textureSize() const override;
    virtual bool hasAlphaChannel() const override;
    virtual bool hasMipmaps() const override;
    virtual void bind() override;

    void* buffer() const;
    int textureByteCount() const;

    void createEglImage() const;

private:
    GrallocTexture(struct graphic_buffer* handle, const QSize& size, const bool& hasAlphaChannel,
                   int textureSize, ShaderBundle conversionShader, EglImageFunctions eglImageFunctions);
    ~GrallocTexture();

    void renderShader(QOpenGLFunctions* gl) const;
    void bindImageOnly(QOpenGLFunctions* gl) const;
    bool renderTexture() const;
    void awaitUpload() const;

    mutable struct graphic_buffer* m_buffer;
    EGLImageKHR mutable m_image;
    GLuint mutable m_texture;
    QSize m_size;
    bool m_hasAlphaChannel;
    ShaderBundle m_shaderCode;
    bool mutable m_bound;
    bool mutable m_valid;
    EglImageFunctions m_eglImageFunctions;
    int m_textureSize;

    mutable std::condition_variable m_uploadCondition;
    mutable std::mutex m_uploadMutex;

    friend class GrallocTextureCreator;
};

#endif
