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
    return texture;
}

int TextureFactory::textureByteCount() const
{
    return m_image.byteCount();
}

QSize TextureFactory::textureSize() const
{
    return m_image.size();
}

QImage TextureFactory::image() const
{
    return m_image;
}
