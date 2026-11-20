// the image extractor and pixel buffer conversion of WebGL as
// platform/graphics/skia/GraphicsContextGLSkia.cpp makes them, without reading a texture through Skia's GL
// context, which this port does not have
#include "config.h"
#include "GraphicsContextGL.h"

#if ENABLE(WEBGL) && USE(SKIA)
#include "BitmapImage.h"
#include "GraphicsContextGLImageExtractor.h"
#include "NativeImage.h"
#include "NotImplemented.h"
#include "PixelBuffer.h"
#include "SharedBuffer.h"
#include "SkiaSpanExtras.h"
#include <skia/core/SkData.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkImage.h>
#include <skia/core/SkPixmap.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

namespace WebCore {

GraphicsContextGLImageExtractor::~GraphicsContextGLImageExtractor() = default;

bool GraphicsContextGLImageExtractor::extractImage(bool premultiplyAlpha, bool ignoreGammaAndColorProfile, bool ignoreNativeImageAlphaPremultiplication)
{
    RefPtr<NativeImage> nativeImage;
    bool hasAlpha = !m_image->currentFrameKnownToBeOpaque();
    if ((ignoreGammaAndColorProfile || (hasAlpha && !premultiplyAlpha)) && m_image->data()) {
        auto image = BitmapImage::create(nullptr,  AlphaOption::NotPremultiplied, ignoreGammaAndColorProfile ? GammaAndColorProfileOption::Ignored : GammaAndColorProfileOption::Applied);
        image->setData(m_image->data(), true);
        if (!image->frameCount())
            return false;

        nativeImage = image->currentNativeImage();
    } else
        nativeImage = m_image->currentNativeImage();

    if (!nativeImage)
        return false;

    auto platformImage = nativeImage->platformImage();
    if (!platformImage)
        return false;

    m_imageWidth = platformImage->width();
    m_imageHeight = platformImage->height();
    if (!m_imageWidth || !m_imageHeight)
        return false;

    const auto& imageInfo = platformImage->imageInfo();
    m_alphaOp = AlphaOp::DoNothing;
    switch (imageInfo.alphaType()) {
    case kUnknown_SkAlphaType:
    case kOpaque_SkAlphaType:
        break;
    case kPremul_SkAlphaType:
        if (!premultiplyAlpha)
            m_alphaOp = AlphaOp::DoUnmultiply;
        else if (ignoreNativeImageAlphaPremultiplication)
            m_alphaOp = AlphaOp::DoPremultiply;
        break;
    case kUnpremul_SkAlphaType:
        if (premultiplyAlpha)
            m_alphaOp = AlphaOp::DoPremultiply;
        break;
    }

    unsigned srcUnpackAlignment = 1;
    size_t bytesPerRow = imageInfo.minRowBytes();
    size_t bytesPerPixel = imageInfo.bytesPerPixel();
    unsigned padding = bytesPerRow - bytesPerPixel * m_imageWidth;
    if (padding) {
        srcUnpackAlignment = padding + 1;
        while (bytesPerRow % srcUnpackAlignment)
            ++srcUnpackAlignment;
    }

    // this port has no images in GPU memory (the Skia raster is the only one), so a texture backed image is
    // not one it can read
    if (platformImage->isTextureBacked())
        return false;
    {
        SkPixmap pixmap;
        if (!platformImage->peekPixels(&pixmap))
            return false;

        m_skImage = WTF::move(platformImage);
        m_imagePixelData = span(pixmap);

        // raster SkSurfaces have BGRA format
        m_imageSourceFormat = DataFormat::BGRA8;
    }

    m_imageSourceUnpackAlignment = srcUnpackAlignment;
    return true;
}

RefPtr<NativeImage> GraphicsContextGL::createNativeImageFromPixelBuffer(const GraphicsContextGLAttributes& sourceContextAttributes, Ref<PixelBuffer>&& pixelBuffer)
{
    ASSERT(!pixelBuffer->size().isEmpty());
    auto imageSize = pixelBuffer->size();
    SkAlphaType alphaType = kUnpremul_SkAlphaType;
    if (!sourceContextAttributes.alpha)
        alphaType = kOpaque_SkAlphaType;
    else if (sourceContextAttributes.premultipliedAlpha)
        alphaType = kPremul_SkAlphaType;
    auto imageInfo = SkImageInfo::Make(imageSize.width(), imageSize.height(), kRGBA_8888_SkColorType, alphaType, SkColorSpace::MakeSRGB());

    Ref protectedPixelBuffer = pixelBuffer;
    SkPixmap pixmap(imageInfo, pixelBuffer->bytes().data(), imageInfo.minRowBytes());
    auto image = SkImages::RasterFromPixmap(pixmap, [](const void*, void* context) {
        static_cast<PixelBuffer*>(context)->deref();
    }, &protectedPixelBuffer.leakRef());
    return NativeImage::create(WTF::move(image));
}

} // namespace WebCore

#endif // ENABLE(WEBGL) && USE(SKIA)
