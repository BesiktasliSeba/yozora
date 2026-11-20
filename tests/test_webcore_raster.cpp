#include "config.h"
#include "DestinationColorSpace.h"
#include "GraphicsTypes.h"
#include "IntSize.h"
#include "NativeImage.h"
#include "SkiaUtilities.h"
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkCanvas.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPicture.h>
#include <skia/core/SkPictureRecorder.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkSurface.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <wtf/MainThread.h>

// release WTF omits debug assertion helpers, enable C assertions after its headers
#undef NDEBUG
#include <cassert>

#if USE(SKIA_GPU)
#error the raster probe requires CPU Skia
#endif

using namespace WebCore;

static void check_color(const SkPixmap& pixels, int x, int y, SkColor expected)
{
    auto actual = pixels.getColor(x, y);
    if (actual != expected)
        std::fprintf(stderr, "yozora-raster: pixel (%d,%d) is %08x, expected %08x\n", x, y, actual, expected);
    assert(actual == expected);
}

static void blend_modes()
{
    const struct {
        CompositeOperator operation;
        SkBlendMode expected;
    } operations[] = {
        { CompositeOperator::Clear, SkBlendMode::kClear },
        { CompositeOperator::Copy, SkBlendMode::kSrc },
        { CompositeOperator::SourceOver, SkBlendMode::kSrcOver },
        { CompositeOperator::SourceIn, SkBlendMode::kSrcIn },
        { CompositeOperator::SourceOut, SkBlendMode::kSrcOut },
        { CompositeOperator::SourceAtop, SkBlendMode::kSrcATop },
        { CompositeOperator::DestinationOver, SkBlendMode::kDstOver },
        { CompositeOperator::DestinationIn, SkBlendMode::kDstIn },
        { CompositeOperator::DestinationOut, SkBlendMode::kDstOut },
        { CompositeOperator::DestinationAtop, SkBlendMode::kDstATop },
        { CompositeOperator::XOR, SkBlendMode::kXor },
        { CompositeOperator::PlusLighter, SkBlendMode::kPlus },
        { CompositeOperator::Difference, SkBlendMode::kDifference },
    };
    for (const auto& item : operations)
        assert(SkiaUtilities::toSkiaBlendMode(BlendMode::Normal, item.operation) == item.expected);
    const struct {
        BlendMode mode;
        SkBlendMode expected;
    } modes[] = {
        { BlendMode::Normal, SkBlendMode::kSrcOver },
        { BlendMode::Multiply, SkBlendMode::kMultiply },
        { BlendMode::Screen, SkBlendMode::kScreen },
        { BlendMode::Overlay, SkBlendMode::kOverlay },
        { BlendMode::Darken, SkBlendMode::kDarken },
        { BlendMode::Lighten, SkBlendMode::kLighten },
        { BlendMode::ColorDodge, SkBlendMode::kColorDodge },
        { BlendMode::ColorBurn, SkBlendMode::kColorBurn },
        { BlendMode::HardLight, SkBlendMode::kHardLight },
        { BlendMode::SoftLight, SkBlendMode::kSoftLight },
        { BlendMode::Difference, SkBlendMode::kDifference },
        { BlendMode::Exclusion, SkBlendMode::kExclusion },
        { BlendMode::Hue, SkBlendMode::kHue },
        { BlendMode::Saturation, SkBlendMode::kSaturation },
        { BlendMode::Color, SkBlendMode::kColor },
        { BlendMode::Luminosity, SkBlendMode::kLuminosity },
    };
    for (const auto& item : modes)
        assert(SkiaUtilities::toSkiaBlendMode(item.mode) == item.expected);
}

static void sampling()
{
    const struct {
        SkMatrix matrix;
        SkFilterMode expected;
    } cases[] = {
        { SkMatrix::I(), SkFilterMode::kNearest },
        { SkMatrix::Translate(2, -3), SkFilterMode::kNearest },
        { SkMatrix::Translate(0.25f, 0), SkFilterMode::kLinear },
        { SkMatrix::Scale(-1, 1), SkFilterMode::kNearest },
        { SkMatrix::Scale(2, 1), SkFilterMode::kLinear },
        { SkMatrix::Scale(1.00001f, 1), SkFilterMode::kNearest },
        { SkMatrix::Scale(1.001f, 1), SkFilterMode::kLinear },
        { SkMatrix::RotateDeg(45), SkFilterMode::kLinear },
    };
    for (const auto& item : cases)
        assert(SkiaUtilities::samplingOptionsForMatrix(item.matrix).filter == item.expected);
    auto source = SkRect::MakeWH(8, 8);
    assert(SkiaUtilities::samplingOptionsForImageDraw(SkMatrix::Scale(0.5f, 0.5f), source, SkRect::MakeWH(16, 16)).filter == SkFilterMode::kNearest);
    assert(SkiaUtilities::samplingOptionsForImageDraw(SkMatrix::I(), source, SkRect::MakeWH(16, 16)).filter == SkFilterMode::kLinear);
}

static void native_images()
{
    assert(!NativeImage::create(nullptr));
    assert(!NativeImage::createTransient(nullptr));
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(1, 1));
    assert(surface);
    surface->getCanvas()->clear(SK_ColorMAGENTA);
    auto image = NativeImage::create(surface->makeImageSnapshot());
    assert(image && image->size() == IntSize(1, 1) && image->hasAlpha());
    assert(image->sizeInBytes() == 4 && !image->gainMap());
    assert(image->colorSpace() == DestinationColorSpace::SRGB());
    auto color = image->singlePixelSolidColor();
    assert((color && *color == Color { SRGBA<uint8_t> { 255, 0, 255, 255 } }));
    surface->getCanvas()->clear(SK_ColorCYAN);
    assert(image->singlePixelSolidColor() == color);
    auto transient = NativeImage::createTransient(sk_ref_sp(image->platformImage().get()));
    assert(transient && transient->uniqueID() == image->uniqueID());
    image = nullptr;
    surface = nullptr;
    assert(transient->singlePixelSolidColor());
}

static sk_sp<SkPicture> record(std::atomic<unsigned>& released)
{
    auto info = SkImageInfo::MakeN32Premul(2, 2);
    auto* pixels = static_cast<uint32_t*>(std::malloc(16));
    assert(pixels);
    SkPixmap pixmap(info, pixels, 8);
    assert(pixmap.erase(SK_ColorGREEN));
    auto image = SkImages::RasterFromPixmap(pixmap, [](const void* data, void* context) {
        std::free(const_cast<void*>(data));
        ++*static_cast<std::atomic<unsigned>*>(context);
    }, &released);
    assert(image && !image->isTextureBacked());
    auto native = NativeImage::create(sk_ref_sp(image.get()));
    assert(native && native->size() == IntSize(2, 2));
    assert(!native->singlePixelSolidColor());

    SkPictureRecorder recorder;
    auto* canvas = recorder.beginRecording(SkRect::MakeWH(32, 32));
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    canvas->drawRect(SkRect::MakeWH(32, 32), paint);
    paint.setColor(SK_ColorRED);
    canvas->drawRect(SkRect::MakeWH(16, 16), paint);
    canvas->save();
    canvas->clipRect(SkRect::MakeXYWH(8, 8, 8, 8));
    canvas->translate(8, 8);
    paint.setColor(SK_ColorBLUE);
    paint.setBlendMode(SkiaUtilities::toSkiaBlendMode(BlendMode::Multiply));
    canvas->drawRect(SkRect::MakeWH(16, 16), paint);
    canvas->restore();

    paint.setBlendMode(SkBlendMode::kSrcOver);
    paint.setAlpha(128);
    canvas->saveLayer(nullptr, &paint);
    paint.setAlpha(255);
    canvas->drawRect(SkRect::MakeXYWH(16, 0, 8, 8), paint);
    canvas->restore();
    paint.setBlendMode(SkiaUtilities::toSkiaBlendMode(BlendMode::Normal, CompositeOperator::Clear));
    canvas->drawRect(SkRect::MakeXYWH(0, 24, 8, 8), paint);
    canvas->drawImageRect(native->platformImage(), SkRect::MakeXYWH(24, 24, 4, 4), SkSamplingOptions(SkFilterMode::kNearest));
    auto picture = recorder.finishRecordingAsPicture();
    assert(picture);
    return picture;
}

using Pixels = std::array<SkColor, 32 * 32>;

static Pixels replay(const SkPicture& picture)
{
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(32, 32));
    assert(surface);
    auto* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    int saves = canvas->getSaveCount();
    canvas->drawPicture(&picture);
    assert(canvas->getSaveCount() == saves && canvas->getTotalMatrix().isIdentity());
    SkPixmap pixmap;
    assert(surface->peekPixels(&pixmap));
    check_color(pixmap, 4, 4, SK_ColorRED);
    check_color(pixmap, 12, 12, SK_ColorBLACK);
    check_color(pixmap, 20, 12, SK_ColorWHITE);
    auto translucent = pixmap.getColor(20, 4);
    // premultiplied layer blends can round a color channel by one byte
    auto red = SkColorGetR(translucent);
    assert(SkColorGetA(translucent) == 255 && SkColorGetB(translucent) == 255);
    assert(red >= 126 && red <= 128 && SkColorGetG(translucent) == red);
    check_color(pixmap, 4, 28, SK_ColorTRANSPARENT);
    check_color(pixmap, 25, 25, SK_ColorGREEN);
    check_color(pixmap, 28, 28, SK_ColorWHITE);
    Pixels result;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x)
            result[y * 32 + x] = pixmap.getColor(x, y);
    }
    return result;
}

int main()
{
    WTF::initializeMainThread();
    blend_modes();
    sampling();
    native_images();
    std::atomic<unsigned> released { 0 };
    auto picture = record(released);
    assert(!released.load());
    auto first = replay(*picture);
    Pixels second;
    std::thread worker([picture, &second] { second = replay(*picture); });
    worker.join();
    assert(first == second && !released.load());
    picture.reset();
    assert(released.load() == 1);
    assert(std::puts("webcore raster: native images, blend modes, sampling, recording, layers, clip, clear, cross-thread replay and image ownership passed") >= 0);
    return 0;
}
