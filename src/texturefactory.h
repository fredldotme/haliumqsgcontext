#ifndef TEXTUREFACTORY_H
#define TEXTUREFACTORY_H

#include <QQuickTextureFactory>
#include <QImage>
#include <QSGTexture>

#include <memory>

#include "rendercontext.h"

class TextureFactory : public QQuickTextureFactory
{
    Q_OBJECT

public:
    TextureFactory(RenderContext* renderContext, const QImage& image);

    virtual QSGTexture* createTexture(QQuickWindow *window) const override;
    virtual int textureByteCount() const override;
    virtual QSize textureSize() const override;

private:
    RenderContext* m_renderContext;
    QImage m_image;
    mutable QSGTexture* m_texture;
};

#endif