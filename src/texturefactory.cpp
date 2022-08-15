#include "texturefactory.h"

#include "gralloctexture.h"

TextureFactory::TextureFactory(RenderContext* renderContext, const QImage& image) : QQuickTextureFactory(),
    m_renderContext(renderContext), m_image(image), m_texture(nullptr)
{

}

QSGTexture* TextureFactory::createTexture(QQuickWindow *window) const
{
    Q_UNUSED(window);

    m_texture = m_renderContext->createTexture(m_image);
    return m_texture;
}

int TextureFactory::textureByteCount() const
{
    if (!m_texture)
        return 0;

    GrallocTexture* grallocTexture = static_cast<GrallocTexture*>(m_texture);
    if (!grallocTexture)
        return 0;

    return grallocTexture->textureByteCount();
}

QSize TextureFactory::textureSize() const
{
    if (!m_texture)
        return QSize();

    GrallocTexture* grallocTexture = static_cast<GrallocTexture*>(m_texture);
    if (!grallocTexture)
        return QSize();

    return grallocTexture->textureSize();
}