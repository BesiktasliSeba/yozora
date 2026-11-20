#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkFontMgr.h>
#include <skia/core/SkGraphics.h>
#include <skia/core/SkImageInfo.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkString.h>
#include <skia/core/SkSurface.h>
#include <skia/core/SkTypeface.h>
#include <skia/ports/SkFontMgr_directory.h>
#include <cstdio>

static bool check_color(const SkPixmap& pixels, int x, int y, SkColor expected)
{
    SkColor actual = pixels.getColor(x, y);
    if (actual == expected)
        return true;
    std::fprintf(stderr, "yozora-skia: pixel (%d,%d) is %08x, expected %08x\n",
                 x, y, actual, expected);
    return false;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fputs("usage: yozora-skia FONT_FIXTURE_DIRECTORY\n", stderr);
        return 2;
    }
    SkGraphics::Init();
    auto manager = SkFontMgr_New_Custom_Directory(argv[1]);
    auto typeface = manager ? manager->matchFamilyStyle("Roboto", SkFontStyle()) : nullptr;
    SkString family;
    if (typeface)
        typeface->getFamilyName(&family);
    if (!typeface || !family.equals("Roboto")) {
        std::fputs("yozora-skia: fixture directory must contain Roboto\n", stderr);
        return 2;
    }
    SkFont font(typeface, 24);
    SkGlyphID glyphs[5] = {};
    if (font.textToGlyphs("Yozora", 5, SkTextEncoding::kUTF8, SkSpan<SkGlyphID>(glyphs)) != 5) {
        std::fputs("yozora-skia: unexpected glyph count\n", stderr);
        return 1;
    }
    for (auto glyph : glyphs) {
        if (!glyph) {
            std::fputs("yozora-skia: fixture font has a missing glyph\n", stderr);
            return 1;
        }
    }
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(128, 96));
    if (!surface || !surface->getCanvas()) {
        std::fputs("yozora-skia: cannot allocate raster surface\n", stderr);
        return 1;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);
    SkPaint paint;
    paint.setColor(SK_ColorRED);
    canvas->drawRect(SkRect::MakeXYWH(8, 8, 32, 32), paint);
    canvas->save();
    canvas->clipRect(SkRect::MakeXYWH(48, 8, 24, 24));
    paint.setColor(SK_ColorBLUE);
    canvas->drawRect(SkRect::MakeXYWH(40, 0, 48, 48), paint);
    canvas->restore();
    canvas->save();
    canvas->translate(88, 8);
    paint.setColor(SK_ColorGREEN);
    canvas->drawRect(SkRect::MakeWH(24, 24), paint);
    canvas->restore();
    paint.setColor(SK_ColorBLACK);
    canvas->drawString("Yozora", 8, 80, font, paint);
    SkPixmap pixels;
    if (!surface->peekPixels(&pixels)) {
        std::fputs("yozora-skia: raster pixels unavailable\n", stderr);
        return 1;
    }
    if (!check_color(pixels, 0, 0, SK_ColorWHITE) ||
        !check_color(pixels, 16, 16, SK_ColorRED) ||
        !check_color(pixels, 56, 16, SK_ColorBLUE) ||
        !check_color(pixels, 44, 16, SK_ColorWHITE) ||
        !check_color(pixels, 80, 16, SK_ColorWHITE) ||
        !check_color(pixels, 96, 16, SK_ColorGREEN))
        return 1;
    unsigned ink = 0;
    for (int y = 48; y < 88; ++y) {
        for (int x = 0; x < 128; ++x) {
            if (pixels.getColor(x, y) != SK_ColorWHITE)
                ++ink;
        }
    }
    if (!ink) {
        std::fputs("yozora-skia: font rasterization produced no pixels\n", stderr);
        return 1;
    }
    if (std::printf("{\"raster\":true,\"clip\":true,\"transform\":true,\"glyphs\":5,\"text_pixels\":%u}\n", ink) < 0 ||
        std::fflush(stdout)) {
        std::fputs("yozora-skia: result write failed\n", stderr);
        return 1;
    }
    return 0;
}
