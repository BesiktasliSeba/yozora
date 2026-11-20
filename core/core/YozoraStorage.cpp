#include "config.h"
#include "YozoraStorage.h"
#include "YozoraPrivateSession.h"
#include "Page.h"
#include "SecurityOrigin.h"
#include "SecurityOriginData.h"
#include "StorageArea.h"
#include "StorageNamespace.h"
#include "StorageNamespaceProvider.h"
#include "StorageType.h"
#include <cstdio>
#include <string>
#include <unistd.h>
#include <pal/SessionID.h>
#include <wtf/HashMap.h>
#include <wtf/RunLoop.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>

using namespace WebCore;

namespace {

// the page quota of the storage specification, counted in bytes of keys and values as UTF-16
constexpr size_t quota_bytes = 5 * 1024 * 1024;
// a change is written this long after the last one, a page that stores on every keystroke writes once
constexpr Seconds write_delay { 2_s };

class Persistence;
Persistence& persistence();

class Area final : public StorageArea {
public:
    Area(StorageType type, String&& origin, bool persistent) : m_type(type), m_origin(WTF::move(origin)), m_persistent(persistent) { }
    unsigned length() final { return m_items.size(); }
    String key(unsigned index) final { return index < m_items.size() ? m_items[index].first : String(); }
    String item(const String& key) final
    {
        for (auto& entry : m_items) {
            if (entry.first == key)
                return entry.second;
        }
        return { };
    }
    void setItem(LocalFrame&, const String& key, const String& value, bool& quotaException) final;
    void removeItem(LocalFrame&, const String& key) final;
    void clear(LocalFrame&) final;
    bool contains(const String& key) final { return !item(key).isNull(); }
    StorageType storageType() const final { return m_type; }
    size_t memoryBytesUsedByCache() final { return m_bytes; }

    const String& origin() const { return m_origin; }
    const Vector<std::pair<String, String>>& items() const { return m_items; }
    // loading from the file: no quota, no write back
    void load(const String& key, const String& value)
    {
        m_items.append({ key, value });
        m_bytes += (key.length() + value.length()) * 2;
    }
    void wipe() { m_items.clear(); m_bytes = 0; }

private:
    void changed();
    StorageType m_type;
    String m_origin;
    // an area of the private session never schedules a write of the profile file
    bool m_persistent;
    Vector<std::pair<String, String>> m_items;
    size_t m_bytes { 0 };
};

class Namespace final : public StorageNamespace {
public:
    Namespace(StorageType type, PAL::SessionID sessionID, bool persistent = true) : m_type(type), m_sessionID(sessionID), m_persistent(persistent) { }
    Ref<StorageArea> storageArea(const SecurityOrigin& origin) final
    {
        auto key = origin.data().toString();
        auto found = m_areas.find(key);
        if (found != m_areas.end())
            return found->value.copyRef();
        auto area = adoptRef(*new Area(m_type, String(key), m_persistent));
        m_areas.add(key, area.copyRef());
        return area;
    }
    const SecurityOrigin* topLevelOrigin() const final { return nullptr; }
    // a window opened by the page starts with the storage of its opener
    Ref<StorageNamespace> copy(Page&) final
    {
        auto result = adoptRef(*new Namespace(m_type, m_sessionID, m_persistent));
        for (auto& entry : m_areas) {
            auto area = adoptRef(*new Area(m_type, String(entry.key), m_persistent));
            for (auto& item : entry.value->items())
                area->load(item.first, item.second);
            result->m_areas.add(entry.key, WTF::move(area));
        }
        return result;
    }
    PAL::SessionID sessionID() const final { return m_sessionID; }
    void setSessionIDForTesting(PAL::SessionID id) final { m_sessionID = id; }
    HashMap<String, Ref<Area>>& areas() { return m_areas; }

private:
    StorageType m_type;
    PAL::SessionID m_sessionID;
    bool m_persistent;
    HashMap<String, Ref<Area>> m_areas;
};

class Persistence {
public:
    Namespace& local(PAL::SessionID sessionID)
    {
        if (!m_local) {
            m_local = adoptRef(*new Namespace(StorageType::Local, sessionID));
            read();
        }
        return *m_local;
    }
    // the local storage of the private session: the same kind of namespace, with no file behind it
    Namespace& privateLocal(PAL::SessionID sessionID)
    {
        if (!m_private)
            m_private = adoptRef(*new Namespace(StorageType::Local, sessionID, false));
        return *m_private;
    }
    void clearPrivate()
    {
        if (!m_private)
            return;
        for (auto& entry : m_private->areas())
            entry.value->wipe();
    }
    void setFile(const char* path) { m_path = path ? path : ""; }
    void changed()
    {
        if (m_path.empty() || m_scheduled)
            return;
        m_scheduled = true;
        RunLoop::currentSingleton().dispatchAfter(write_delay, [this] {
            m_scheduled = false;
            write();
        });
    }
    void write();
    void clear()
    {
        if (m_local) {
            for (auto& entry : m_local->areas())
                entry.value->wipe();
        }
        if (!m_path.empty())
            unlink(m_path.c_str());
    }

private:
    static String escape(const String& text)
    {
        StringBuilder out;
        for (auto character : StringView(text).codeUnits()) {
            if (character == '%' || character == '\t' || character == '\n' || character == '\r')
                out.append('%', hex(character, 2));
            else
                out.append(character);
        }
        return out.toString();
    }
    static String unescape(StringView text)
    {
        StringBuilder out;
        for (size_t i = 0; i < text.length(); ++i) {
            if (text[i] == '%' && i + 2 < text.length() && isASCIIHexDigit(text[i + 1]) && isASCIIHexDigit(text[i + 2])) {
                out.append(static_cast<char16_t>(toASCIIHexValue(text[i + 1], text[i + 2])));
                i += 2;
            } else
                out.append(text[i]);
        }
        return out.toString();
    }
    void read();

    RefPtr<Namespace> m_local;
    RefPtr<Namespace> m_private;
    std::string m_path;
    bool m_scheduled { false };
};

Persistence& persistence()
{
    static Persistence* instance = new Persistence;
    return *instance;
}

void Area::changed()
{
    if (m_type == StorageType::Local && m_persistent)
        persistence().changed();
}

void Area::setItem(LocalFrame&, const String& key, const String& value, bool& quotaException)
{
    quotaException = false;
    size_t old_bytes = 0;
    for (auto& entry : m_items) {
        if (entry.first == key) {
            old_bytes = (entry.first.length() + entry.second.length()) * 2;
            if (entry.second == value)
                return;
            break;
        }
    }
    size_t new_bytes = (key.length() + value.length()) * 2;
    if (m_bytes - old_bytes + new_bytes > quota_bytes) {
        quotaException = true;
        return;
    }
    for (auto& entry : m_items) {
        if (entry.first == key) {
            entry.second = value;
            m_bytes = m_bytes - old_bytes + new_bytes;
            changed();
            return;
        }
    }
    m_items.append({ key, value });
    m_bytes += new_bytes;
    changed();
}

void Area::removeItem(LocalFrame&, const String& key)
{
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].first == key) {
            m_bytes -= (m_items[i].first.length() + m_items[i].second.length()) * 2;
            m_items.removeAt(i);
            changed();
            return;
        }
    }
}

void Area::clear(LocalFrame&)
{
    if (m_items.isEmpty())
        return;
    wipe();
    changed();
}

// one line for each entry: origin, key and value, tab separated, with tabs, newlines and percent signs escaped
void Persistence::write()
{
    if (m_path.empty() || !m_local)
        return;
    StringBuilder text;
    for (auto& entry : m_local->areas()) {
        for (auto& item : entry.value->items())
            text.append(escape(entry.value->origin()), '\t', escape(item.first), '\t', escape(item.second), '\n');
    }
    auto utf8 = text.toString().utf8();
    std::string temporary = m_path + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) {
        std::fprintf(stderr, "yozora: local storage not saved: %s\n", temporary.c_str());
        return;
    }
    bool ok = std::fwrite(utf8.data(), 1, utf8.length(), file) == utf8.length();
    ok = std::fflush(file) == 0 && ok;
    ok = std::fclose(file) == 0 && ok;
    if (!ok || std::rename(temporary.c_str(), m_path.c_str()) != 0) {
        std::fprintf(stderr, "yozora: local storage not saved: %s\n", m_path.c_str());
        unlink(temporary.c_str());
    }
}

void Persistence::read()
{
    if (m_path.empty())
        return;
    FILE* file = std::fopen(m_path.c_str(), "rb");
    if (!file)
        return;
    std::string content;
    char buffer[65536];
    size_t count;
    // the file holds at most what a few sites may keep, a larger one is not ours
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0 && content.size() < 64 * 1024 * 1024)
        content.append(buffer, count);
    std::fclose(file);
    auto all = String::fromUTF8(std::span { reinterpret_cast<const char8_t*>(content.data()), content.size() });
    for (auto line : StringView(all).splitAllowingEmptyEntries('\n')) {
        auto parts = line.splitAllowingEmptyEntries('\t');
        auto iterator = parts.begin();
        Vector<StringView> fields;
        for (; iterator != parts.end(); ++iterator)
            fields.append(*iterator);
        if (fields.size() != 3)
            continue;
        auto origin = unescape(fields[0]);
        auto found = m_local->areas().find(origin);
        if (found == m_local->areas().end())
            found = m_local->areas().add(origin, adoptRef(*new Area(StorageType::Local, String(origin), true))).iterator;
        found->value->load(unescape(fields[1]), unescape(fields[2]));
    }
}

class Provider final : public StorageNamespaceProvider {
public:
    RefPtr<StorageNamespace> sessionStorageNamespace(const SecurityOrigin&, Page& page, ShouldCreateNamespace) final
    {
        if (!m_session)
            m_session = adoptRef(*new Namespace(StorageType::Session, page.sessionID()));
        return m_session;
    }
    void cloneSessionStorageNamespaceForPage(Page& source, Page& destination) final
    {
        auto& origin = static_cast<Provider&>(source.storageNamespaceProvider());
        auto& target = static_cast<Provider&>(destination.storageNamespaceProvider());
        if (origin.m_session)
            target.m_session = origin.m_session->copy(destination);
    }

private:
    Ref<StorageNamespace> createLocalStorageNamespace(unsigned, PAL::SessionID sessionID) final
    {
        // a page made while the private session runs keeps its local storage in the namespace that has no file
        if (YozoraPrivateSession::active())
            return Ref<StorageNamespace> { persistence().privateLocal(sessionID) };
        return Ref<StorageNamespace> { persistence().local(sessionID) };
    }
    Ref<StorageNamespace> createTransientLocalStorageNamespace(SecurityOrigin&, unsigned, PAL::SessionID sessionID) final
    {
        return adoptRef(*new Namespace(StorageType::TransientLocal, sessionID, false));
    }
    RefPtr<StorageNamespace> m_session;
};

}

namespace YozoraStorage {

Ref<StorageNamespaceProvider> createProvider() { return adoptRef(*new Provider); }
void flush() { persistence().write(); }
void clear() { persistence().clear(); }
void clearPrivate() { persistence().clearPrivate(); }
void setFile(const char* path) { persistence().setFile(path); }

}
