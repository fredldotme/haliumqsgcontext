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
    ColorShader_Count = ColorShader_Last + 1
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
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(textureSampler, uv).rgba);\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(textureSampler, uv).bgr, 1.0);\n"
    "}\n"
};

static const GLchar* FLIP_COLOR_CHANNELS_WITH_ALPHA_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(textureSampler, uv).bgra;\n"
    "}\n"
};

static const GLchar* RGB32_TO_RGBA8888_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"   
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(textureSampler, uv).rgba;\n"
    "}\n"
};

static const GLchar* RED_AND_BLUE_SWAP_SHADER = {
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D textureSampler;\n"
    "varying highp vec2 uv;\n"
    "\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(textureSampler, uv).bgra;\n"
    "}\n"
};

struct ShaderBundle {
    ShaderBundle(GLuint program, int vertexCoord, int textureCoord, int textureSampler) :
        program(program), vertexCoord(vertexCoord), textureCoord(textureCoord), texture(textureSampler) {}
    const GLuint program;
    const int vertexCoord = -1;
    const int textureCoord = -1;
    const int texture = -1;
};

typedef std::map<ColorShader, std::shared_ptr<ShaderBundle>> ShaderCache;

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
    static GrallocTexture* createTexture(const QImage& image, ShaderCache& cachedShaders, const int& maxTextureSize);
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

    void provideSizeInfo(const QSize& size);
    void createEglImage(struct graphic_buffer* handle, const int textureSize) const;

private:
    GrallocTexture(const bool& hasAlphaChannel, std::shared_ptr<ShaderBundle> conversionShader, EglImageFunctions eglImageFunctions);
    ~GrallocTexture();

    void ensureEmptyTexture(QOpenGLFunctions* gl) const;
    void renderShader(QOpenGLFunctions* gl) const;
    void bindImageOnly(QOpenGLFunctions* gl) const;
    bool renderTexture() const;
    void awaitUpload() const;

    bool m_hasAlphaChannel;
    std::shared_ptr<ShaderBundle> m_shaderCode;

    mutable struct graphic_buffer* m_buffer;
    mutable EGLImageKHR m_image;
    mutable int m_textureSize;
    mutable QSize m_size;
    mutable GLuint m_texture;
    mutable bool m_bound;
    mutable bool m_valid;
    mutable bool m_rendered;
    mutable bool m_uploadInProgress;

    EglImageFunctions m_eglImageFunctions;

    mutable std::condition_variable m_infoCondition;
    mutable std::condition_variable m_uploadCondition;
    mutable std::mutex m_infoMutex;
    mutable std::mutex m_uploadMutex;

    friend class GrallocTextureCreator;
};

#endif
