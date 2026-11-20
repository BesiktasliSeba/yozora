#include "config.h"
#include "FontManagerYozora.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <thread>
#include <wtf/MainThread.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkCanvas.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPath.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkString.h>
#include <skia/core/SkSurface.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

// release WTF omits debug assertion helpers, enable C assertions after its headers
#undef NDEBUG
#include <cassert>

using namespace WebCore;

static size_t descriptor_count()
{
    DIR* directory = opendir("/proc/self/fd");
    assert(directory);
    size_t count = 0;
    while (auto* entry = readdir(directory)) {
        if (entry->d_name[0] != '.')
            ++count;
    }
    assert(!closedir(directory));
    return count;
}

static bool family_is(const sk_sp<SkTypeface>& face, const char* expected)
{
    if (!face)
        return false;
    SkString name;
    face->getFamilyName(&name);
    return name.equals(expected);
}

static void matched_fonts(SkFontMgr& manager)
{
    assert(family_is(matchYozoraFont(manager, "rObOtO", { }), "Roboto"));
    assert(family_is(matchYozoraFont(manager, "sans-serif", { }), "Roboto"));
    assert(family_is(matchYozoraFont(manager, "-apple-system", { }), "Roboto"));
    assert(family_is(matchYozoraFont(manager, "system-ui", { }), "Roboto"));
    assert(family_is(matchYozoraFont(manager, nullptr, { }), "Roboto"));
    assert(family_is(matchYozoraFont(manager, "serif", { }), "Amiri"));
    auto mono = matchYozoraFont(manager, "monospace", { });
    assert(mono && mono->isFixedPitch());
    assert(!matchYozoraFont(manager, "not an installed family", { }));
    const char32_t arabic[] = { 0x0633, 0x0644, 0x0627, 0x0645, 0x064e, 0x200d };
    auto face = matchYozoraFontCluster(manager, { }, "ar", arabic);
    assert(family_is(face, "Amiri"));
    const char32_t accented[] = { U'A', 0x0301 };
    assert(matchYozoraFontCluster(manager, { }, "en", accented));
    const char32_t mixed[] = { U'A', 0x0633, 0x064e };
    assert(family_is(matchYozoraFontCluster(manager, { }, "ar", mixed), "Amiri"));
    assert(!matchYozoraFontCluster(manager, { }, "en", { }));
    for (char32_t character : { char32_t(0x10ffff), char32_t(0x110000), char32_t(0xd800), char32_t(0x200d), char32_t(0xfe0f) })
        assert(!matchYozoraFontCluster(manager, { }, nullptr, std::span(&character, 1)));
    std::array<char32_t, 257> large;
    large.fill(U'A');
    assert(matchYozoraFontCluster(manager, { }, "en", std::span(large).first(256)));
    assert(!matchYozoraFontCluster(manager, { }, "en", large));

    SkFont font(face, 28);
    auto path = font.getPath(face->unicharToGlyph(0x0633));
    assert(path && !path->isEmpty());
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
    assert(surface);
    auto* canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);
    canvas->translate(8, 40);
    SkPaint paint;
    paint.setColor(SK_ColorBLACK);
    canvas->drawPath(*path, paint);
    SkPixmap pixels;
    assert(surface->peekPixels(&pixels));
    unsigned ink = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x)
            ink += pixels.getColor(x, y) != SK_ColorWHITE;
    }
    assert(ink);
}

int main(int argc, char** argv)
{
    WTF::initializeMainThread();
    char error[160];
    if (argc == 3 && !strcmp(argv[1], "--math")) {
        auto manager = createYozoraFontManager(argv[2], error, sizeof(error));
        if (!manager) {
            std::fprintf(stderr, "yozora fonts: %s\n", error);
            return 1;
        }
        for (char32_t character : { char32_t(0x1d453), char32_t(0x1d465), char32_t(0x210e) }) {
            auto face = matchYozoraFontCluster(*manager, { }, "en", std::span(&character, 1));
            assert(face && face->unicharToGlyph(character));
            SkFont font(face, 28);
            auto path = font.getPath(face->unicharToGlyph(character));
            assert(path && !path->isEmpty());
        }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--reject")) {
        auto before = descriptor_count();
        for (int i = 0; i < 4; ++i) {
            assert(!createYozoraFontManager(argv[2], error, sizeof(error)));
            assert(error[0]);
        }
        assert(before == descriptor_count());
        assert(std::printf("rejected: %s\n", error) > 0);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--catalog")) {
        auto manager = createYozoraFontManager(argv[2], error, sizeof(error));
        if (!manager) {
            std::fprintf(stderr, "yozora fonts: %s\n", error);
            return 1;
        }
        int faces = 0;
        for (int i = 0; i < manager->countFamilies(); ++i)
            faces += manager->createStyleSet(i)->count();
        assert(std::printf("families=%d faces=%d\n", manager->countFamilies(), faces) > 0);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--snapshot")) {
        auto manager = createYozoraFontManager(argv[2], error, sizeof(error));
        assert(manager);
        assert(std::puts("ready") >= 0 && !std::fflush(stdout));
        assert(std::getchar() == 'x');
        matched_fonts(*manager);
        assert(std::puts("font snapshot passed") >= 0);
        return 0;
    }
    if (argc != 2) {
        std::fputs("usage: yozora-fonts FONT_FIXTURE_DIRECTORY\n", stderr);
        return 2;
    }
    assert(!yozoraFontManager());
    assert(!createYozoraFontManager(nullptr, error, sizeof(error)) && error[0]);
    assert(!createYozoraFontManager(nullptr, nullptr, 0));
    char short_error[] = { 'x', 'x', 'x', 'x', 'x', 'x' };
    assert(!createYozoraFontManager(nullptr, short_error + 1, 4));
    assert(short_error[0] == 'x' && short_error[4] == 0 && short_error[5] == 'x');
    assert(initializeYozoraFonts(argv[1], error, sizeof(error)) && !error[0]);
    auto manager = yozoraFontManager();
    assert(manager && manager->countFamilies() >= 3);
    assert(!initializeYozoraFonts(argv[1], error, sizeof(error)) && error[0]);
    matched_fonts(*manager);
    std::thread worker([manager] {
        auto another = yozoraFontManager();
        assert(another.get() == manager.get());
        matched_fonts(*another);
    });
    matched_fonts(*manager);
    worker.join();
    assert(std::puts("fonts: families, aliases, clusters, glyph pixels, shared catalog and ownership passed") >= 0);
    return 0;
}
