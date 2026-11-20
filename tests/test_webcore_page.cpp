#include "config.h"
#include "FontManagerYozora.h"
#include "AXObjectCache.h"
#include "AXNotifications.h"
#include "AccessibilityObjectInlines.h"
#include "DocumentView.h"
#include "Editor.h"
#include "GraphicsContextSkia.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "Settings.h"
#include "PlatformScreen.h"
#include "TestPageHarness.h"
#include <cstdio>
#include <wtf/RunLoop.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <skia/core/SkPixmap.h>
#include <skia/core/SkSurface.h>
#include <skia/encode/SkPngEncoder.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

// release WTF omits debug assertion helpers, enable C assertions after its headers
#undef NDEBUG
#include <cassert>

using namespace WebCore;
using namespace WTF::StringLiterals;

void testYozoraServices();

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) {
        std::fputs("usage: yozora-page FONT_FIXTURE_DIRECTORY [OUTPUT.png]\n", stderr);
        return 2;
    }
    char error[160];
    if (!initializeYozoraFonts(argv[1], error, sizeof(error))) {
        std::fprintf(stderr, "yozora page: %s\n", error);
        return 1;
    }
    JSC::initialize();
    WTF::initializeMainThread();
    ScreenData screen;
    screen.screenRect = { 0, 0, 320, 240 };
    screen.screenAvailableRect = screen.screenRect;
    screen.screenDepth = 24;
    screen.screenDepthPerComponent = 8;
    ScreenProperties properties;
    properties.primaryDisplayID = 1;
    properties.screenDataMap.add(1, screen);
    PlatformScreen::updateSingletonProperties(WTF::move(properties));
    auto page = TestWebKitAPI::TestPageHarness::create({ { 320, 240 }, [](Settings& settings) {
        settings.setScriptEnabled(true);
        settings.setJavaScriptCanAccessClipboard(true);
        settings.setDOMPasteAllowed(true);
    } });
    // the empty-page fixture blocks scripts independently of Settings
    auto sandbox_flags = page.frame().effectiveSandboxFlags();
    sandbox_flags.remove(SandboxFlag::Scripts);
    page.frame().updateSandboxFlags(sandbox_flags, Frame::NotifyUIProcess::No);
    testYozoraServices();
    page.loadHTML("<!doctype html><meta charset=utf-8><style>html,body{margin:0;background:white}"
        "#box{position:absolute;left:10px;top:20px;width:80px;height:40px;background:red}"
        "p{position:absolute;top:80px;font:24px sans-serif}</style><div id=box></div>"
        "<p>Yozora</p><button id=action aria-label='Yozora action' style='position:absolute;top:160px' "
        "onclick=\"document.getElementById('box').textContent='pressed'\">press</button>"
        "<script>document.getElementById('box').style.backgroundColor='blue';"
        "document.getElementById('box').textContent='ready';</script>"_s);
    auto box = page.getElementById("box"_s);
    assert(box && box->textContent() == "ready"_s);
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, 240));
    assert(surface);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    GraphicsContextSkia context(*surface->getCanvas(), RenderingMode::Unaccelerated, RenderingPurpose::Snapshot);
    page.frame().view()->paintContents(context, { 0, 0, 320, 240 });
    SkPixmap pixels;
    assert(surface->peekPixels(&pixels));
    assert(pixels.getColor(30, 40) == SK_ColorBLUE);
    assert(pixels.getColor(100, 40) == SK_ColorWHITE);
    size_t text_pixels = 0;
    for (int y = 100; y < 155; ++y) {
        for (int x = 0; x < 100; ++x) {
            auto color = pixels.getColor(x, y);
            if (SkColorGetR(color) < 100 && SkColorGetG(color) < 100 && SkColorGetB(color) < 100)
                ++text_pixels;
        }
    }
    assert(text_pixels > 40);
    if (argc == 3) {
        auto png = SkPngEncoder::Encode(pixels, { });
        if (!png) {
            std::fputs("yozora page: PNG encoding failed\n", stderr);
            return 1;
        }
        FILE* output = std::fopen(argv[2], "wb");
        if (!output) {
            std::perror("yozora page: cannot open PNG output");
            return 1;
        }
        bool written = std::fwrite(png->data(), 1, png->size(), output) == png->size();
        if (std::fclose(output))
            written = false;
        if (!written) {
            std::fputs("yozora page: PNG output write failed\n", stderr);
            return 1;
        }
    }
    AXObjectCache::enableAccessibility();
    auto* cache = page.document().axObjectCache();
    assert(cache);
    auto button = page.getElementById("action"_s);
    RefPtr object = cache->getOrCreate(*button);
    assert(object && object->wrapper());
    RefPtr wrapper = object->wrapper();
    assert(!wrapper->isDetached() && wrapper->objectID() == object->objectID());
    assert(wrapper->label() == "Yozora action"_s);
    auto revision = wrapper->revision();
    cache->postNotification(*object, AXNotification::ValueChanged);
    for (unsigned i = 0; i < 16 && wrapper->revision() == revision; ++i)
        RunLoop::cycle();
    assert(wrapper->revision() > revision);
    assert(wrapper->press());
    assert(box->textContent() == "pressed"_s);
    page.document().clearAXObjectCache();
    assert(wrapper->isDetached() && wrapper->label().isEmpty() && !wrapper->press());

    page.loadHTML("<!doctype html><div id=source contenteditable><b>copied</b></div>"
        "<div id=destination contenteditable></div><script>"
        "function select(id){const node=document.getElementById(id);node.focus();"
        "const range=document.createRange();range.selectNodeContents(node);"
        "const selection=getSelection();selection.removeAllRanges();selection.addRange(range)}"
        "select('source');document.execCommand('copy');select('destination');document.execCommand('paste');"
        "</script>"_s);
    auto destination = page.getElementById("destination"_s);
    assert(destination && destination->textContent() == "copied"_s);
    assert(destination->firstElementChild() && destination->firstElementChild()->localName() == "b"_s);

    page.loadHTML("<!doctype html><div id=source contenteditable>copy</div><div id=result></div>"
        "<script>document.addEventListener('copy',e=>{"
        "e.clipboardData.setData('application/x-yozora-test','secret');"
        "e.clipboardData.setData('text/plain','public');e.preventDefault()});"
        "document.addEventListener('paste',e=>{document.getElementById('result').textContent="
        "e.clipboardData.getData('application/x-yozora-test')+'|'+e.clipboardData.getData('text/plain');"
        "e.preventDefault()});const source=document.getElementById('source');source.focus();"
        "const range=document.createRange();range.selectNodeContents(source);"
        "getSelection().removeAllRanges();getSelection().addRange(range);"
        "document.execCommand('copy');document.execCommand('paste');</script>"_s);
    assert(page.getElementById("result"_s)->textContent() == "secret|public"_s);
    page.loadHTML("<!doctype html><div id=destination contenteditable>paste</div><div id=result></div>"
        "<script>document.addEventListener('paste',e=>{document.getElementById('result').textContent="
        "e.clipboardData.getData('application/x-yozora-test')+'|'+e.clipboardData.getData('text/plain');"
        "e.preventDefault()});document.getElementById('destination').focus();"
        "document.execCommand('paste');</script>"_s);
    assert(page.getElementById("result"_s)->textContent() == "|public"_s);

    page.loadHTML("<!doctype html><style>html,body{margin:0;background:white}"
        "#flex{display:flex;width:120px;height:30px}#flex div{flex:1}"
        "#grid{display:grid;position:absolute;top:50px;width:120px;height:60px;"
        "grid-template-columns:60px 60px;grid-template-rows:30px 30px}"
        "input{position:absolute;top:10px;width:20px;height:20px;margin:0}</style>"
        "<div id=flex><div style=background:red></div><div style=background:blue></div></div>"
        "<div id=grid><div style=background:lime></div><div style=background:blue></div>"
        "<div style=background:red></div><div style=background:lime></div></div>"
        "<input type=checkbox style=left:160px><input type=checkbox checked style=left:200px>"_s);
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    page.frame().view()->paintContents(context, { 0, 0, 320, 240 });
    assert(surface->peekPixels(&pixels));
    assert(pixels.getColor(30, 15) == SK_ColorRED && pixels.getColor(90, 15) == SK_ColorBLUE);
    assert(pixels.getColor(30, 65) == SK_ColorGREEN && pixels.getColor(90, 65) == SK_ColorBLUE);
    assert(pixels.getColor(30, 95) == SK_ColorRED && pixels.getColor(90, 95) == SK_ColorGREEN);
    unsigned different_pixels = 0;
    for (int y = 10; y < 30; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (pixels.getColor(160 + x, y) != pixels.getColor(200 + x, y))
                ++different_pixels;
        }
    }
    assert(different_pixels > 20);
    assert(std::puts("page: local HTML, CSS layout, JavaScript DOM mutation and CPU pixels passed") >= 0);
    return 0;
}
