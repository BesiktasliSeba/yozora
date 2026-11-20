#include "config.h"
#include "YozoraCoreInternal.h"
#include "YozoraCoreThread.h"

#include "Color.h"
#include "ColorChooser.h"
#include "ColorChooserClient.h"
#include "ColorSerialization.h"
#include "DateTimeChooser.h"
#include "DateTimeChooserClient.h"
#include "DateTimeChooserParameters.h"
#include "Document.h"
#include "Event.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "EventTarget.h"
#include "FileChooser.h"
#include "HTMLOptGroupElement.h"
#include "HTMLOptionElement.h"
#include "HTMLSelectElement.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "LocalFrame.h"
#include "NodeInlines.h"
#include "LocalFrameView.h"
#include "PopupMenu.h"
#include "PopupMenuClient.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <wtf/CheckedPtr.h>
#include <wtf/text/StringBuilder.h>

using namespace WebCore;

// controls the person has to operate: what the page lists goes to the interface under a new id and the answer
// returns on the core thread through the core's map. every answer looks its object up again, so the id of a
// gone page or list does nothing

// copies text into a fixed buffer without cutting a character in half
static void copy_utf8(char* out, size_t capacity, const String& text)
{
    auto utf8 = text.utf8();
    size_t length = std::min<size_t>(utf8.length(), capacity - 1);
    while (length && length < utf8.length() && (static_cast<unsigned char>(utf8.data()[length]) & 0xC0) == 0x80)
        --length;
    std::memcpy(out, utf8.data(), length);
    out[length] = 0;
}

static void view_rect(const IntRect& rect, LocalFrameView& view, int& x, int& y, int& width, int& height)
{
    IntRect root = view.contentsToRootView(rect);
    x = root.x();
    y = root.y();
    width = root.width();
    height = root.height();
}

namespace {

// a select element: the list is read when it opens, a choice goes back through valueChanged like a click of a native menu would
class YozoraPopupMenu final : public PopupMenu {
public:
    YozoraPopupMenu(yozora_core& core, PopupMenuClient& client) : m_core(core), m_client(&client) { }
    // no destructor work: while the question is open the core's map holds a reference, so the list is
    // destroyed only after its entry is gone or while the core itself is destroyed, and the map must not be
    // touched then

    void show(const IntRect& rect, LocalFrameView& view, int selectedIndex) final
    {
        if (!m_client)
            return;
        cancel();
        auto choice = std::make_unique<yozora_choice>();
        std::memset(choice.get(), 0, sizeof(yozora_choice));
        choice->id = m_core.next_control_id++;
        int size = std::min(m_client->listSize(), YOZORA_CHOICE_MAX);
        choice->count = size;
        choice->selected = selectedIndex >= 0 && selectedIndex < size ? selectedIndex : -1;
        for (int i = 0; i < size; ++i) {
            auto& item = choice->items[i];
            copy_utf8(item.text, sizeof(item.text), m_client->itemText(i));
            if (m_client->itemIsSeparator(i))
                item.flags |= YOZORA_CHOICE_SEPARATOR;
            if (m_client->itemIsLabel(i))
                item.flags |= YOZORA_CHOICE_GROUP;
            if (!m_client->itemIsEnabled(i))
                item.flags |= YOZORA_CHOICE_DISABLED;
        }
        view_rect(rect, view, choice->x, choice->y, choice->width, choice->height);
        m_pending = choice->id;
        Ref protectedThis { *this };
        m_core.choice_waiting[m_pending] = [protectedThis](int index) {
            protectedThis->answered(index);
        };
        pthread_mutex_lock(&m_core.lock);
        m_core.shared.choices.push_back(WTF::move(choice));
        pthread_mutex_unlock(&m_core.lock);
    }
    // the page closes the list itself (a click elsewhere, the select was removed), the question stays with
    // the interface and its answer is ignored
    void hide() final { cancel(); }
    void updateFromElement() final { }
    void disconnectClient() final
    {
        cancel();
        m_client = nullptr;
    }

private:
    void cancel()
    {
        if (m_pending)
            m_core.choice_waiting.erase(m_pending);
        m_pending = 0;
    }
    void answered(int index)
    {
        m_pending = 0;
        if (!m_client)
            return;
        // the list can have changed while the person chose, an index past its end is dropped
        if (index >= 0 && index < m_client->listSize() && m_client->itemIsEnabled(index) && !m_client->itemIsLabel(index) && !m_client->itemIsSeparator(index))
            m_client->valueChanged(static_cast<unsigned>(index));
        // the change event of the select can run a script that removes it, which disconnects this list
        if (m_client)
            m_client->popupDidHide();
    }

    yozora_core& m_core;
    PopupMenuClient* m_client;
    unsigned m_pending { 0 };
};

class YozoraDateChooser final : public DateTimeChooser {
public:
    // the core thread owns every reference so the count needs no atomics. a new object starts with the one
    // reference adoptRef takes over
    void ref() const final { ++m_refCount; }
    void deref() const final
    {
        if (!--m_refCount)
            delete this;
    }
    YozoraDateChooser(yozora_core& core, DateTimeChooserClient& client) : m_core(core), m_client(&client) { }
    ~YozoraDateChooser() final { cancel(); }

    void showChooser(const DateTimeChooserParameters& parameters) final
    {
        if (!m_client)
            return;
        cancel();
        yozora_date_request request { };
        request.id = m_core.next_control_id++;
        copy_utf8(request.type, sizeof(request.type), parameters.type.string());
        copy_utf8(request.value, sizeof(request.value), parameters.currentValue);
        request.minimum = parameters.minimum;
        request.maximum = parameters.maximum;
        request.step = parameters.step;
        request.x = parameters.anchorRectInRootView.x();
        request.y = parameters.anchorRectInRootView.y();
        request.width = parameters.anchorRectInRootView.width();
        request.height = parameters.anchorRectInRootView.height();
        m_pending = request.id;
        WeakPtr weak { *this };
        m_core.date_waiting[m_pending] = [weak](const char* value) {
            if (RefPtr chooser = weak.get())
                chooser->answered(value);
        };
        pthread_mutex_lock(&m_core.lock);
        m_core.shared.dates.push_back(request);
        pthread_mutex_unlock(&m_core.lock);
    }
    // the field lets go of its chooser: it blurred, was detached, or took another value
    void endChooser() final
    {
        cancel();
        m_client = nullptr;
    }

private:
    void cancel()
    {
        if (m_pending)
            m_core.date_waiting.erase(m_pending);
        m_pending = 0;
    }
    void answered(const char* value)
    {
        m_pending = 0;
        CheckedPtr client = m_client.get();
        if (!client)
            return;
        if (value)
            client->didChooseValue(StringView::fromLatin1(value));
        client->didEndChooser();
    }

    yozora_core& m_core;
    CheckedPtr<DateTimeChooserClient> m_client;
    unsigned m_pending { 0 };
    mutable unsigned m_refCount { 1 };
};

// a color field: the person picks in a sheet and the answer returns as a hex string. the core's entry keeps
// the chooser alive while the question is open
class YozoraColorChooser final : public ColorChooser {
public:
    YozoraColorChooser(yozora_core& core, ColorChooserClient& client) : m_core(core), m_client(client) { }
    // the reference count belongs to the core thread, like every other object the page makes
    void ref() const final { ++m_refCount; }
    void deref() const final
    {
        if (!--m_refCount)
            delete this;
    }

    void open(const Color& color)
    {
        cancel();
        yozora_color_request request { };
        request.id = m_core.next_control_id++;
        copy_utf8(request.value, sizeof(request.value), serializationForHTML(color.opaqueColor()));
        auto rect = m_client.get() ? m_client->elementRectRelativeToRootView() : IntRect();
        request.x = rect.x();
        request.y = rect.y();
        request.width = rect.width();
        request.height = rect.height();
        m_pending = request.id;
        Ref protectedThis { *this };
        m_core.color_waiting[m_pending] = [protectedThis](const char* value) { protectedThis->answered(value); };
        pthread_mutex_lock(&m_core.lock);
        m_core.shared.colors.push_back(request);
        pthread_mutex_unlock(&m_core.lock);
    }
    // the field shows the chooser again for the same element: the question is asked again
    void reattachColorChooser(const Color& color) final { open(color); }
    void setSelectedColor(const Color&) final { }
    void endChooser() final
    {
        cancel();
        m_client = nullptr;
    }

private:
    void cancel()
    {
        if (m_pending)
            m_core.color_waiting.erase(m_pending);
        m_pending = 0;
    }
    void answered(const char* value)
    {
        m_pending = 0;
        RefPtr client = m_client.get();
        if (!client)
            return;
        // "#rrggbb", read by hand: anything else is not a color the field can take
        unsigned red = 0, green = 0, blue = 0;
        char tail = 0;
        if (value && std::sscanf(value, "#%2x%2x%2x%c", &red, &green, &blue, &tail) == 3)
            client->didChooseColor(Color(SRGBA<uint8_t> { static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue), 255 }));
        client->didEndChooser();
    }

    yozora_core& m_core;
    WeakPtr<ColorChooserClient> m_client;
    unsigned m_pending { 0 };
    mutable unsigned m_refCount { 1 };
};

}

Ref<ColorChooser> yozora_create_color_chooser(yozora_core& core, ColorChooserClient& client, const Color& color)
{
    auto chooser = adoptRef(*new YozoraColorChooser(core, client));
    chooser->open(color);
    return chooser;
}

// a list with several choices: the page draws it in its own layout and a tap would replace the choice with
// one item, so the core hands the whole list to the interface and writes the answer back into the options
bool yozora_tap_multiple_select(yozora_core& core, LocalFrame& frame, const IntPoint& point)
{
    HitTestRequest request({ HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active });
    auto result = frame.eventHandler().hitTestResultAtPoint(point, request.type());
    RefPtr<HTMLSelectElement> select;
    for (RefPtr node = result.innerNode(); node; node = node->parentOrShadowHostNode()) {
        if (RefPtr candidate = dynamicDowncast<HTMLSelectElement>(node.get())) {
            select = WTF::move(candidate);
            break;
        }
    }
    if (!select || !select->multiple() || select->isDisabledFormControl())
        return false;

    auto choice = std::make_unique<yozora_choice>();
    std::memset(choice.get(), 0, sizeof(yozora_choice));
    choice->id = core.next_control_id++;
    choice->multiple = 1;
    choice->selected = -1;
    // the list items as the select holds them: options, group labels and rules. an index of the answer is an index into this vector
    Vector<RefPtr<HTMLElement>> items;
    for (auto& weak : select->listItems()) {
        if (RefPtr item = weak.get())
            items.append(WTF::move(item));
    }
    int count = static_cast<int>(std::min<size_t>(items.size(), YOZORA_CHOICE_MAX));
    choice->count = count;
    for (int i = 0; i < count; ++i) {
        auto& entry = choice->items[i];
        if (RefPtr option = dynamicDowncast<HTMLOptionElement>(items[i].get())) {
            copy_utf8(entry.text, sizeof(entry.text), option->label());
            if (option->isDisabledFormControl())
                entry.flags |= YOZORA_CHOICE_DISABLED;
            if (option->selected())
                entry.flags |= YOZORA_CHOICE_SELECTED;
        } else if (RefPtr group = dynamicDowncast<HTMLOptGroupElement>(items[i].get())) {
            copy_utf8(entry.text, sizeof(entry.text), group->groupLabelText());
            entry.flags |= YOZORA_CHOICE_GROUP;
        } else
            entry.flags |= YOZORA_CHOICE_SEPARATOR;
    }
    IntRect box = select->boundingBoxInRootViewCoordinates();
    choice->x = box.x();
    choice->y = box.y();
    choice->width = box.width();
    choice->height = box.height();
    unsigned id = choice->id;
    core.multi_waiting[id] = [select, items](std::vector<int>&& chosen, bool cancelled) {
        if (cancelled)
            return;
        bool changed = false;
        for (size_t i = 0; i < items.size(); ++i) {
            RefPtr option = dynamicDowncast<HTMLOptionElement>(items[i].get());
            if (!option || option->isDisabledFormControl())
                continue;
            bool wanted = std::find(chosen.begin(), chosen.end(), static_cast<int>(i)) != chosen.end();
            if (option->selected() != wanted) {
                option->setSelected(wanted);
                changed = true;
            }
        }
        // a change that the person made is announced like the one of a native list
        if (changed) {
            select->dispatchEvent(Event::create(eventNames().inputEvent, Event::CanBubble::Yes, Event::IsCancelable::No));
            select->dispatchEvent(Event::create(eventNames().changeEvent, Event::CanBubble::Yes, Event::IsCancelable::No));
        }
    };
    pthread_mutex_lock(&core.lock);
    core.shared.choices.push_back(WTF::move(choice));
    pthread_mutex_unlock(&core.lock);
    return true;
}

Ref<PopupMenu> yozora_create_popup_menu(yozora_core& core, PopupMenuClient& client)
{
    return adoptRef(*new YozoraPopupMenu(core, client));
}

Ref<DateTimeChooser> yozora_create_date_chooser(yozora_core& core, DateTimeChooserClient& client)
{
    return adoptRef(*new YozoraDateChooser(core, client));
}

void yozora_open_file_panel(yozora_core& core, FileChooser& chooser)
{
    yozora_file_request request { };
    request.id = core.next_control_id++;
    request.multiple = chooser.settings().allowsMultipleFiles ? 1 : 0;
    StringBuilder accept;
    for (auto& type : chooser.settings().acceptMIMETypes) {
        if (!accept.isEmpty())
            accept.append(',');
        accept.append(type);
    }
    for (auto& extension : chooser.settings().acceptFileExtensions) {
        if (!accept.isEmpty())
            accept.append(',');
        accept.append(extension);
    }
    copy_utf8(request.accept, sizeof(request.accept), accept.toString());
    core.file_waiting[request.id] = [chooser = Ref { chooser }](std::vector<std::string>&& paths) {
        Vector<String> chosen;
        for (auto& path : paths)
            chosen.append(String::fromUTF8(path.c_str()));
        if (!chosen.isEmpty())
            chooser->chooseFiles(chosen);
    };
    pthread_mutex_lock(&core.lock);
    core.shared.files.push_back(request);
    pthread_mutex_unlock(&core.lock);
}

extern "C" int yozora_core_take_choice(yozora_core* core, yozora_choice* out)
{
    if (!core || !out)
        return 0;
    pthread_mutex_lock(&core->lock);
    bool taken = !core->shared.choices.empty();
    if (taken) {
        *out = *core->shared.choices.front();
        core->shared.choices.pop_front();
    }
    pthread_mutex_unlock(&core->lock);
    return taken ? 1 : 0;
}

extern "C" void yozora_core_choice_respond(yozora_core* core, unsigned id, int index)
{
    if (!core)
        return;
    YozoraCoreThread::post([core, id, index] {
        auto found = core->choice_waiting.find(id);
        if (found == core->choice_waiting.end())
            return;
        auto answer = WTF::move(found->second);
        core->choice_waiting.erase(found);
        answer(index);
        core->dirty = true;
        core->rendering = true;
    });
}

extern "C" int yozora_core_take_files(yozora_core* core, yozora_file_request* out)
{
    if (!core || !out)
        return 0;
    pthread_mutex_lock(&core->lock);
    bool taken = !core->shared.files.empty();
    if (taken) {
        *out = core->shared.files.front();
        core->shared.files.pop_front();
    }
    pthread_mutex_unlock(&core->lock);
    return taken ? 1 : 0;
}

extern "C" void yozora_core_files_respond(yozora_core* core, unsigned id, const char* const* paths, int count)
{
    if (!core)
        return;
    std::vector<std::string> chosen;
    for (int i = 0; paths && i < std::min(count, 64); ++i) {
        if (paths[i] && paths[i][0])
            chosen.emplace_back(paths[i]);
    }
    YozoraCoreThread::post([core, id, chosen = WTF::move(chosen)]() mutable {
        auto found = core->file_waiting.find(id);
        if (found == core->file_waiting.end())
            return;
        auto answer = WTF::move(found->second);
        core->file_waiting.erase(found);
        answer(WTF::move(chosen));
    });
}

extern "C" int yozora_core_take_date(yozora_core* core, yozora_date_request* out)
{
    if (!core || !out)
        return 0;
    pthread_mutex_lock(&core->lock);
    bool taken = !core->shared.dates.empty();
    if (taken) {
        *out = core->shared.dates.front();
        core->shared.dates.pop_front();
    }
    pthread_mutex_unlock(&core->lock);
    return taken ? 1 : 0;
}

extern "C" void yozora_core_date_respond(yozora_core* core, unsigned id, const char* value)
{
    if (!core)
        return;
    // a value in the format of the type is short, anything longer is not one
    std::optional<std::string> chosen;
    if (value && std::strlen(value) < 64)
        chosen = std::string(value);
    YozoraCoreThread::post([core, id, chosen = WTF::move(chosen)] {
        auto found = core->date_waiting.find(id);
        if (found == core->date_waiting.end())
            return;
        auto answer = WTF::move(found->second);
        core->date_waiting.erase(found);
        answer(chosen ? chosen->c_str() : nullptr);
        core->dirty = true;
        core->rendering = true;
    });
}

extern "C" void yozora_core_choices_respond(yozora_core* core, unsigned id, const int* indexes, int count)
{
    if (!core)
        return;
    bool cancelled = count < 0;
    std::vector<int> chosen;
    for (int i = 0; indexes && i < std::min(count, YOZORA_CHOICE_MAX); ++i)
        chosen.push_back(indexes[i]);
    YozoraCoreThread::post([core, id, cancelled, chosen = WTF::move(chosen)]() mutable {
        auto found = core->multi_waiting.find(id);
        if (found == core->multi_waiting.end())
            return;
        auto answer = WTF::move(found->second);
        core->multi_waiting.erase(found);
        answer(WTF::move(chosen), cancelled);
        core->dirty = true;
        core->rendering = true;
    });
}

extern "C" int yozora_core_take_color(yozora_core* core, yozora_color_request* out)
{
    if (!core || !out)
        return 0;
    pthread_mutex_lock(&core->lock);
    bool taken = !core->shared.colors.empty();
    if (taken) {
        *out = core->shared.colors.front();
        core->shared.colors.pop_front();
    }
    pthread_mutex_unlock(&core->lock);
    return taken ? 1 : 0;
}

extern "C" void yozora_core_color_respond(yozora_core* core, unsigned id, const char* value)
{
    if (!core)
        return;
    std::optional<std::string> chosen;
    if (value && std::strlen(value) < 16)
        chosen = std::string(value);
    YozoraCoreThread::post([core, id, chosen = WTF::move(chosen)] {
        auto found = core->color_waiting.find(id);
        if (found == core->color_waiting.end())
            return;
        auto answer = WTF::move(found->second);
        core->color_waiting.erase(found);
        answer(chosen ? chosen->c_str() : nullptr);
        core->dirty = true;
        core->rendering = true;
    });
}
