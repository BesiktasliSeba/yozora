#include "config.h"
#include "CommonAtomStrings.h"
#include "DeprecatedGlobalSettings.h"
#include "MIMETypeRegistry.h"
#include "Pasteboard.h"
#include "PlatformKeyboardEvent.h"
#include "PlatformScreen.h"
#include "SystemFontDatabase.h"
#include "StaticPasteboard.h"
#include <algorithm>
#include <array>
#include <string>
#include <pal/text/KillRing.h>
#include <wtf/text/MakeString.h>

#undef NDEBUG
#include <cassert>

using namespace WebCore;
using namespace WTF::StringLiterals;

static void test_mime_types()
{
    constexpr auto fixtures = std::to_array<TypeExtensionPair>({
        { "text/html"_s, "HTML"_s }, { "text/javascript"_s, "mjs"_s },
        { "image/png"_s, "png"_s }, { "image/jpeg"_s, "JpEg"_s },
        { "application/json"_s, "json"_s }, { "font/woff2"_s, "woff2"_s },
        { "video/mp4"_s, "mp4"_s }, { "application/wasm"_s, "wasm"_s },
    });
    for (auto& fixture : fixtures) {
        assert(MIMETypeRegistry::mimeTypeForExtension(fixture.extension) == fixture.type);
        auto extensions = MIMETypeRegistry::extensionsForMIMEType(fixture.type);
        auto preferred = MIMETypeRegistry::preferredExtensionForMIMEType(fixture.type);
        assert(!extensions.isEmpty() && extensions[0] == preferred);
        assert(MIMETypeRegistry::mimeTypeForExtension(preferred) == fixture.type);
    }
    assert(MIMETypeRegistry::extensionsForMIMEType("IMAGE/JPEG"_s) == Vector<String>({ "jpg"_s, "jpeg"_s, "jpe"_s }));
    for (auto input : { ""_s, ".html"_s, "html?x"_s, "../html"_s, "png/"_s, "unknown"_s })
        assert(MIMETypeRegistry::mimeTypeForExtension(input).isEmpty());
    assert(MIMETypeRegistry::extensionsForMIMEType("text/html; charset=utf-8"_s).isEmpty());
    assert(MIMETypeRegistry::preferredExtensionForMIMEType("unknown/type"_s).isEmpty());
    assert(!MIMETypeRegistry::isApplicationPluginMIMEType("application/pdf"_s));
}

static void test_input_and_screen()
{
    using Modifier = PlatformEvent::Modifier;
    PlatformKeyboardEvent::setCurrentModifierState({ Modifier::ShiftKey, Modifier::ControlKey });
    bool shift = false, control = false, alt = true, meta = true;
    PlatformKeyboardEvent::getCurrentModifierState(shift, control, alt, meta);
    assert(shift && control && !alt && !meta);
    PlatformKeyboardEvent::setCurrentModifierState({ Modifier::CapsLockKey });
    assert(PlatformKeyboardEvent::currentCapsLockState());
    PlatformKeyboardEvent::setCurrentModifierState({ });
    assert(!PlatformKeyboardEvent::currentCapsLockState() && PlatformKeyboardEvent::currentStateOfModifierKeys().isEmpty());

    Ref original = PlatformScreen::singleton();
    ScreenData data;
    data.screenRect = { 4, 5, 640, 480 };
    data.screenAvailableRect = { 4, 25, 640, 460 };
    data.screenDepth = 24;
    data.screenDepthPerComponent = 8;
    data.screenHasInvertedColors = true;
    ScreenProperties properties;
    properties.primaryDisplayID = 7;
    properties.screenDataMap.add(7, data);
    PlatformScreen::updateSingletonProperties(WTF::move(properties));
    assert(screenRect(nullptr) == data.screenRect);
    assert(screenAvailableRect(nullptr) == data.screenAvailableRect);
    assert(screenDepth(nullptr) == 24 && screenDepthPerComponent(nullptr) == 8);
    assert(screenHasInvertedColors() && !screenIsMonochrome(nullptr));
    assert(!screenSupportsExtendedColor(nullptr));
    assert(screenColorSpace(nullptr) == DestinationColorSpace::SRGB());
    assert(original->screenData(0)->screenRect == FloatRect(0, 0, 320, 240));
    PlatformScreen::updateSingletonProperties(ScreenProperties(original->screenProperties()));

    auto& fonts = SystemFontDatabase::singleton();
    auto family = fonts.systemFontShorthandFamily(SystemFontDatabase::FontShorthand::Menu);
    assert(!family.isEmpty());
    assert(fonts.systemFontShorthandSize(SystemFontDatabase::FontShorthand::Menu) == 16);
    assert(fonts.systemFontShorthandSize(SystemFontDatabase::FontShorthand::WebkitMiniControl) == 11);
    assert(fonts.systemFontShorthandSize(SystemFontDatabase::FontShorthand::WebkitSmallControl) == 13);
    fonts.invalidate();
    assert(fonts.systemFontShorthandFamily(SystemFontDatabase::FontShorthand::Menu) == family);
}

static void test_kill_ring()
{
    PAL::KillRing first;
    PAL::KillRing second;
    assert(first.yank().isEmpty());
    first.append("bc"_s);
    first.prepend("a"_s);
    first.append("d"_s);
    assert(first.yank() == "abcd"_s && second.yank().isEmpty());
    first.startNewSequence();
    assert(first.yank() == "abcd"_s);
    first.append(emptyString());
    first.prepend(emptyString());
    assert(first.yank() == "abcd"_s);
    first.prepend("new"_s);
    assert(first.yank() == "new"_s);
    first.setToYankedState();
    first.append("after yank"_s);
    assert(first.yank() == "after yank"_s);
    first.startNewSequence();
    std::u16string characters(65536, u'x');
    String limit { std::span<const char16_t>(characters) };
    first.append(limit);
    assert(first.yank() == limit);
    first.append("x"_s);
    assert(first.yank() == limit);
    first.startNewSequence();
    first.prepend(makeString(limit, 'x'));
    assert(first.yank() == limit);
    first.prepend("recovered"_s);
    assert(first.yank() == "recovered"_s);
}

static void test_clipboard()
{
    assert(DeprecatedGlobalSettings::customPasteboardDataEnabled());
    auto first = Pasteboard::createForCopyAndPaste({ });
    auto second = Pasteboard::createForCopyAndPaste({ });
    first->clear();
    auto revision = first->changeCount();
    assert(!first->hasData() && first->allPasteboardItemInfo()->isEmpty());
    first->writePlainText("plain"_s, Pasteboard::CanSmartReplace);
    assert(second->hasData() && second->canSmartReplace() && second->changeCount() > revision);
    PasteboardPlainText result;
    second->read(result);
    assert(result.text == "plain"_s);
    second->read(result, PlainTextURLReadingPolicy::IgnoreURL, 16);
    assert(result.text.isNull());
    first->writeString(textHTMLContentTypeAtom(), "<b>rich</b>"_s);
    assert(!first->canSmartReplace());
    first->clear(textPlainContentTypeAtom());
    assert(first->readString(textPlainContentTypeAtom()).isNull());
    assert(first->readString(textHTMLContentTypeAtom()) == "<b>rich</b>"_s);

    PasteboardCustomData custom;
    custom.setOrigin("https://first.test"_s);
    custom.writeString(textPlainContentTypeAtom(), "public"_s);
    custom.writeStringInCustomData("application/x-test"_s, "private"_s);
    second->writeCustomData({ custom });
    assert(first->readOrigin() == "https://first.test"_s);
    assert(first->typesSafeForBindings("https://first.test"_s).contains("application/x-test"_s));
    assert(!first->typesSafeForBindings("https://second.test"_s).contains("application/x-test"_s));
    assert(!first->typesSafeForBindings(emptyString()).contains("application/x-test"_s));
    assert(first->readStringInCustomData("application/x-test"_s) == "private"_s);

    PasteboardCustomData other;
    other.writeString(textPlainContentTypeAtom(), "second item"_s);
    first->writeCustomData({ custom, other });
    assert(first->allPasteboardItemInfo()->size() == 2);
    assert(first->readString(1, textPlainContentTypeAtom()) == "second item"_s);
    second->read(result, PlainTextURLReadingPolicy::IgnoreURL, 1);
    assert(result.text == "second item"_s);
    assert(!first->pasteboardItemInfo(2));

    constexpr auto png = std::to_array<uint8_t>({
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00,
        0x00, 0x05, 0x00, 0x01, 0x7a, 0x5e, 0xab, 0x3f,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
        0xae, 0x42, 0x60, 0x82,
    });
    first->write(PasteboardBuffer { "image/png"_s, SharedBuffer::create(std::span<const uint8_t>(png)) });
    assert(first->fileContentState() == Pasteboard::FileContentState::InMemoryImage);
    assert(std::ranges::equal(second->readBuffer(0, "image/png"_s)->span(), png));
    struct Reader final : PasteboardFileReader {
        explicit Reader(Pasteboard& owner) : owner(owner) { }
        void readFilename(const String&) final { assert(false); }
        void readBuffer(const String&, const String& type, Ref<SharedBuffer>&& buffer) final
        {
            assert(type == "image/png"_s);
            bytes = buffer->size();
            owner.clear();
        }
        Pasteboard& owner;
        size_t bytes { 0 };
    } reader(*first);
    StaticPasteboard staging;
    Pasteboard& staging_base = staging;
    Reader staging_reader(staging);
    staging_base.read(staging_reader);
    assert(!staging_reader.bytes && first->hasData());
    assert(!staging_base.readBuffer(0, "image/png"_s));
    auto global_revision = first->changeCount();
    staging_base.write(PasteboardBuffer { "image/png"_s,
        SharedBuffer::create(std::span<const uint8_t>(png).first(8)) });
    assert(staging_base.readBuffer(0, "image/png"_s)->size() == 8);
    staging_base.read(staging_reader);
    assert(staging_reader.bytes == 8 && first->hasData());
    assert(first->changeCount() == global_revision);
    second->read(reader);
    assert(reader.bytes == png.size() && !first->hasData());

    PasteboardURL url { URL({ }, "https://example.test/path"_s), "title"_s, { } };
    first->write(url);
    String title;
    assert(second->readURL(0, title) == url.url && title == url.title);
    assert(first->fileContentState() == Pasteboard::FileContentState::NoFileOrImageData);

    auto old_revision = first->changeCount();
    std::u16string characters(1024 * 1024 + 1, u'x');
    second->writePlainText(String { std::span<const char16_t>(characters) }, Pasteboard::CannotSmartReplace);
    assert(first->changeCount() == old_revision && first->readURL(0, title) == url.url);
    Vector<PasteboardCustomData> excess;
    for (unsigned i = 0; i < 16; ++i)
        excess.append(other);
    first->writeCustomData(excess);
    assert(first->allPasteboardItemInfo()->size() == 16);
    old_revision = first->changeCount();
    excess.append(other);
    first->writeCustomData(excess);
    assert(first->changeCount() == old_revision);

    PasteboardCustomData many_types;
    for (unsigned i = 0; i < 32; ++i)
        many_types.writeString(makeString("application/x-"_s, i), "value"_s);
    first->writeCustomData({ many_types });
    assert(first->typesForLegacyUnsafeBindings().size() == 32);
    old_revision = first->changeCount();
    many_types.writeString("application/x-excess"_s, "value"_s);
    first->writeCustomData({ many_types });
    assert(first->changeCount() == old_revision && first->typesForLegacyUnsafeBindings().size() == 32);

    std::string byte_characters(1024 * 1024 - 10, 'x');
    first->writePlainText(String { std::span<const char>(byte_characters) }, Pasteboard::CannotSmartReplace);
    assert(first->readString(textPlainContentTypeAtom()).length() == byte_characters.size());
    old_revision = first->changeCount();
    byte_characters.push_back('x');
    first->writePlainText(String { std::span<const char>(byte_characters) }, Pasteboard::CannotSmartReplace);
    assert(first->changeCount() == old_revision);
    first->clear();
    assert(!second->hasData());
}

void testYozoraServices()
{
    test_mime_types();
    test_input_and_screen();
    test_kill_ring();
    test_clipboard();
}
