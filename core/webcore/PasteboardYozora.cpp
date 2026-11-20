#include "config.h"
#include "Pasteboard.h"

#include "ColorSerialization.h"
#include "CommonAtomStrings.h"
#include "Image.h"
#include "MIMETypeRegistry.h"
#include "StaticPasteboard.h"
#include <algorithm>
#include <limits>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/StdLibExtras.h>

namespace WebCore {

struct YozoraClipboardData {
    Vector<PasteboardCustomData> items;
    bool smartReplace { false };
    int64_t revision { 0 };
};

static YozoraClipboardData& clipboard()
{
    RELEASE_ASSERT(isMainThread());
    // the headless port owns one process-local clipboard, there is no system clipboard access
    static NeverDestroyed<YozoraClipboardData> data;
    return data;
}

static constexpr size_t byte_limit = 1024 * 1024;
static constexpr size_t item_limit = 16;
static constexpr size_t type_limit = 32;

static Vector<PasteboardCustomData> items_for(const Pasteboard& pasteboard)
{
    if (auto* staging = dynamicDowncast<StaticPasteboard>(pasteboard)) {
        if (!staging->customData().hasData())
            return { };
        return { staging->customData() };
    }
    return clipboard().items;
}

static bool add_size(size_t& total, size_t size)
{
    if (size > byte_limit - total)
        return false;
    total += size;
    return true;
}

static bool valid_items(const Vector<PasteboardCustomData>& items)
{
    if (items.size() > item_limit)
        return false;
    size_t size = 0;
    for (auto& item : items) {
        if (item.data().size() > type_limit || item.origin().length() > 4096 || !add_size(size, item.origin().sizeInBytes()))
            return false;
        for (auto& entry : item.data()) {
            if (entry.type.isEmpty() || entry.type.length() > 256
                || entry.customData.length() > byte_limit
                || !add_size(size, entry.type.sizeInBytes()) || !add_size(size, entry.customData.sizeInBytes()))
                return false;
            size_t bytes = WTF::switchOn(entry.platformData,
                [](const String& text) -> size_t { return text.length() > byte_limit ? byte_limit + 1 : text.sizeInBytes(); },
                [](const Ref<SharedBuffer>& buffer) { return buffer->size(); });
            if (!add_size(size, bytes))
                return false;
        }
    }
    return true;
}

static void commit(Vector<PasteboardCustomData>&& items, bool smart_replace = false)
{
    if (!valid_items(items)) {
        WTFLogAlways("yozora clipboard: rejected data exceeding item, type or byte bounds");
        return;
    }
    auto& data = clipboard();
    data.items = WTF::move(items);
    data.smartReplace = smart_replace;
    // revisions never wrap to a previously issued value
    RELEASE_ASSERT(data.revision < std::numeric_limits<int64_t>::max());
    ++data.revision;
}

static void commit(PasteboardCustomData&& item, bool smart_replace = false)
{
    Vector<PasteboardCustomData> items;
    items.append(WTF::move(item));
    commit(WTF::move(items), smart_replace);
}

static PasteboardCustomData first_item()
{
    auto& items = clipboard().items;
    return items.isEmpty() ? PasteboardCustomData() : items[0];
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context)
    : m_context(WTF::move(context))
{
}

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&& context)
{
    return makeUnique<Pasteboard>(WTF::move(context));
}

int64_t Pasteboard::changeCount() const { return isStatic() ? 0 : clipboard().revision; }

bool Pasteboard::hasData()
{
    return std::ranges::any_of(clipboard().items, [](auto& item) { return item.hasData(); });
}

Vector<String> Pasteboard::typesSafeForBindings(const String& origin)
{
    OrderedHashSet<String> types;
    for (auto& item : clipboard().items) {
        for (auto& type : item.orderedTypes()) {
            if (isSafeTypeForDOMToReadAndWrite(type) || (!origin.isEmpty() && origin == item.origin()))
                types.add(type);
        }
    }
    return copyToVector(types);
}

Vector<String> Pasteboard::typesForLegacyUnsafeBindings()
{
    OrderedHashSet<String> types;
    for (auto& item : clipboard().items) {
        for (auto& type : item.orderedTypes())
            types.add(type);
    }
    return copyToVector(types);
}

String Pasteboard::readOrigin() { return first_item().origin(); }
String Pasteboard::readString(const String& type) { return first_item().readString(type); }
String Pasteboard::readStringInCustomData(const String& type) { return first_item().readStringInCustomData(type); }

void Pasteboard::writeString(const String& type, const String& value)
{
    if (type.isEmpty() || type.length() > 256 || value.length() > byte_limit || value.sizeInBytes() > byte_limit) {
        WTFLogAlways("yozora clipboard: rejected string exceeding type or byte bounds");
        return;
    }
    auto item = first_item();
    item.writeString(type, value);
    commit(WTF::move(item));
}

void Pasteboard::clear() { commit(Vector<PasteboardCustomData> { }); }

void Pasteboard::clear(const String& type)
{
    auto items = clipboard().items;
    for (auto& item : items)
        item.clear(type);
    commit(WTF::move(items));
}

void Pasteboard::read(PasteboardPlainText& result, PlainTextURLReadingPolicy policy, std::optional<size_t> index)
{
    result.text = { };
    if (index.value_or(0) >= clipboard().items.size())
        return;
    auto item = clipboard().items[index.value_or(0)];
    result.text = item.readString(textPlainContentTypeAtom());
    if (result.text.isNull() && policy == PlainTextURLReadingPolicy::AllowURL)
        result.text = item.readString("text/uri-list"_s);
}

void Pasteboard::read(PasteboardWebContentReader& reader, WebContentReadingPolicy policy, std::optional<size_t> index)
{
    if (index.value_or(0) >= clipboard().items.size())
        return;
    // callbacks may run script and replace the clipboard while this read is in progress
    auto item = clipboard().items[index.value_or(0)];
    reader.setContentOrigin(item.origin());
    auto markup = item.readString(textHTMLContentTypeAtom());
    if (!markup.isNull() && reader.readHTML(markup))
        return;
    for (auto& entry : item.data()) {
        if (!MIMETypeRegistry::isSupportedImageMIMEType(entry.type))
            continue;
        if (RefPtr buffer = item.readBuffer(entry.type)) {
            if (reader.readImage(buffer.releaseNonNull(), entry.type))
                return;
        }
    }
    if (policy == WebContentReadingPolicy::OnlyRichTextTypes)
        return;
    auto text = item.readString(textPlainContentTypeAtom());
    if (!text.isNull() && reader.readPlainText(text))
        return;
    auto url = item.readString("text/uri-list"_s);
    if (!url.isNull())
        reader.readURL(URL({ }, url), item.readString("application/x-yozora-url-title"_s));
}

void Pasteboard::read(PasteboardFileReader& reader, std::optional<size_t> index)
{
    auto items = items_for(*this);
    if (index && *index >= items.size())
        return;
    size_t begin = index.value_or(0);
    size_t end = index ? begin + 1 : items.size();
    for (size_t i = begin; i < end; ++i) {
        for (auto& entry : items[i].data()) {
            if (RefPtr buffer = items[i].readBuffer(entry.type); buffer && reader.shouldReadBuffer(entry.type))
                reader.readBuffer(emptyString(), entry.type, buffer.releaseNonNull());
        }
    }
}

void Pasteboard::write(const Color& color) { writePlainText(serializationForCSS(color), CannotSmartReplace); }

void Pasteboard::write(const PasteboardURL& url)
{
    if (!url.url.isValid()) {
        WTFLogAlways("yozora clipboard: rejected invalid URL");
        return;
    }
    PasteboardCustomData item;
    item.writeString("text/uri-list"_s, url.url.string());
    item.writeString(textPlainContentTypeAtom(), url.url.string());
    item.writeString("application/x-yozora-url-title"_s, url.title);
    if (!url.markup.isEmpty())
        item.writeString(textHTMLContentTypeAtom(), url.markup);
    commit(WTF::move(item));
}

void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL& url)
{
    write(url);
}

void Pasteboard::write(const PasteboardImage& image)
{
    RefPtr<SharedBuffer> buffer = image.resourceData;
    String type = image.resourceMIMEType;
    if (!buffer && image.image && image.image->data()) {
        if (image.image->data()->size() > byte_limit) {
            WTFLogAlways("yozora clipboard: image exceeds byte bounds");
            return;
        }
        buffer = image.image->data()->makeContiguous();
        type = image.image->mimeType();
    }
    if (!buffer || !MIMETypeRegistry::isSupportedImageMIMEType(type)) {
        WTFLogAlways("yozora clipboard: image has no supported encoded representation");
        return;
    }
    PasteboardCustomData item;
    item.writeData(type, buffer.releaseNonNull());
    if (canExposeURLToDOMWhenPasteboardContainsFiles(image.url.url.string()))
        item.writeString("text/uri-list"_s, image.url.url.string());
    commit(WTF::move(item));
}

void Pasteboard::write(const PasteboardBuffer& buffer)
{
    if (!buffer.data) {
        WTFLogAlways("yozora clipboard: rejected missing buffer");
        return;
    }
    if (auto* staging = dynamicDowncast<StaticPasteboard>(*this)) {
        auto item = staging->customData();
        item.writeData(buffer.type, Ref { *buffer.data });
        if (!valid_items({ item })) {
            WTFLogAlways("yozora clipboard: rejected staging data exceeding type or byte bounds");
            return;
        }
        staging->writeData(buffer.type, Ref { *buffer.data });
        return;
    }
    PasteboardCustomData item;
    item.writeData(buffer.type, Ref { *buffer.data });
    commit(WTF::move(item));
}

void Pasteboard::write(const PasteboardWebContent& content)
{
    PasteboardCustomData item;
    item.setOrigin(content.contentOrigin);
    item.writeString(textPlainContentTypeAtom(), content.text);
    item.writeString(textHTMLContentTypeAtom(), content.markup);
    commit(WTF::move(item), content.canSmartCopyOrDelete);
}

void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>& items)
{
    if (!valid_items(items)) {
        WTFLogAlways("yozora clipboard: rejected custom data exceeding item, type or byte bounds");
        return;
    }
    commit(Vector<PasteboardCustomData>(items));
}

Pasteboard::FileContentState Pasteboard::fileContentState()
{
    for (auto& item : clipboard().items) {
        for (auto& entry : item.data()) {
            if (MIMETypeRegistry::isSupportedImageMIMEType(entry.type) && item.readBuffer(entry.type))
                return FileContentState::InMemoryImage;
        }
    }
    return FileContentState::NoFileOrImageData;
}

bool Pasteboard::canSmartReplace() { return clipboard().smartReplace; }
void Pasteboard::writeMarkup(const String& markup) { writeString(textHTMLContentTypeAtom(), markup); }

void Pasteboard::writePlainText(const String& text, SmartReplaceOption option)
{
    PasteboardCustomData item;
    item.writeString(textPlainContentTypeAtom(), text);
    commit(WTF::move(item), option == CanSmartReplace);
}

static PasteboardItemInfo item_info(const PasteboardCustomData& item)
{
    PasteboardItemInfo info;
    for (auto& entry : item.data()) {
        info.platformTypesByFidelity.append(entry.type);
        if (Pasteboard::isSafeTypeForDOMToReadAndWrite(entry.type) || MIMETypeRegistry::isSupportedImageMIMEType(entry.type))
            info.webSafeTypesByFidelity.append(entry.type);
        if (item.readBuffer(entry.type))
            info.isNonTextType = true;
    }
    return info;
}

std::optional<PasteboardItemInfo> Pasteboard::pasteboardItemInfo(size_t index) const
{
    auto items = items_for(*this);
    if (index >= items.size())
        return std::nullopt;
    return item_info(items[index]);
}

std::optional<Vector<PasteboardItemInfo>> Pasteboard::allPasteboardItemInfo() const
{
    Vector<PasteboardItemInfo> result;
    for (auto& item : items_for(*this))
        result.append(item_info(item));
    return result;
}

String Pasteboard::readString(size_t index, const String& type)
{
    auto items = items_for(*this);
    return index < items.size() ? items[index].readString(type) : String();
}

RefPtr<SharedBuffer> Pasteboard::readBuffer(std::optional<size_t> index, const String& type)
{
    auto items = items_for(*this);
    return index.value_or(0) < items.size() ? items[index.value_or(0)].readBuffer(type) : nullptr;
}

URL Pasteboard::readURL(size_t index, String& title)
{
    title = readString(index, "application/x-yozora-url-title"_s);
    return URL({ }, readString(index, "text/uri-list"_s));
}

}
