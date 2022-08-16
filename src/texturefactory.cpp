#include "texturefactory.h"

#include "gralloctexture.h"

TextureFactory::TextureFactory(RenderContext* renderContext, const QImage& image) : QQuickTextureFactory(),
    m_renderContext(renderContext), m_image(image)
{

}

QSGTexture* TextureFactory::createTexture(QQuickWindow *window) const
{
    Q_UNUSED(window);

    QSGTexture* texture = m_renderContext->createTexture(m_image, 0);
    GrallocTexture* grallocTexture = static_cast<GrallocTexture*>(texture);

    if (!grallocTexture)
        return nullptr;

    m_size = grallocTexture->textureSize();
    m_byteCount = grallocTexture->textureByteCount();
    return texture;
}

int TextureFactory::textureByteCount() const
{
    return m_byteCount;
}

QSize TextureFactory::textureSize() const
{
    return m_size;
}